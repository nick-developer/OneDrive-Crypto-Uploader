#include <QtTest/QtTest>
#include <QTimer>
#include <QTemporaryDir>
#include "../src/graph/GraphClient.h"
#include <QNetworkReply>

// ── MockReply ──────────────────────────────────────────────────────────────

class MockReply : public QNetworkReply {
  Q_OBJECT
public:
  MockReply(const QNetworkRequest& req, const QByteArray& body, int status,
            QObject* parent = nullptr)
    : QNetworkReply(parent), data_(body)
  {
    setRequest(req);
    setOpenMode(QIODevice::ReadOnly);
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
    QTimer::singleShot(0, this, &MockReply::finish);
  }

  void abort() override {}

  qint64 bytesAvailable() const override {
    return (data_.size() - offset_) + QIODevice::bytesAvailable();
  }

protected:
  qint64 readData(char* data, qint64 maxlen) override {
    const qint64 n = qMin<qint64>(maxlen, data_.size() - offset_);
    if (n <= 0) return -1;
    memcpy(data, data_.constData() + offset_, static_cast<size_t>(n));
    offset_ += n;
    return n;
  }

private slots:
  void finish() { emit finished(); }

private:
  QByteArray data_;
  qint64 offset_ = 0;
};

// ── MockNam — handles all patterns used by the test suite ─────────────────

class MockNam : public QNetworkAccessManager {
  Q_OBJECT
public:
  QList<QNetworkRequest> requests;

protected:
  QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                               QIODevice* outgoingData) override
  {
    Q_UNUSED(outgoingData)
    requests.append(request);
    const QString url = request.url().toString();

    // Upload session creation
    if (op == PostOperation && url.contains("createUploadSession")) {
      const QByteArray resp = R"({"uploadUrl":"https://upload.example/session"})";
      return new MockReply(request, resp, 200, this);
    }

    // Chunk upload
    if (op == PutOperation && url.startsWith("https://upload.example")) {
      return new MockReply(request, "{}", 202, this);
    }

    // Root listing
    if (op == GetOperation && url.contains("/me/drive/root/children")) {
      const QByteArray resp = R"({
        "value": [
          {"id":"F1","name":"FolderA","folder":{"childCount":1}},
          {"id":"X1","name":"file.odenc","file":{},"size":123}
        ]
      })";
      return new MockReply(request, resp, 200, this);
    }

    // Folder listing by id
    if (op == GetOperation && url.contains("/me/drive/items/F1/children")) {
      const QByteArray resp = R"({
        "value": [
          {"id":"X2","name":"nested.odenc","file":{},"size":456}
        ]
      })";
      return new MockReply(request, resp, 200, this);
    }

    // File download (direct 200)
    if (op == GetOperation && url.contains("/me/drive/items/") && url.contains("/content")) {
      return new MockReply(request, QByteArray("FILE_CONTENT"), 200, this);
    }

    return new MockReply(request, "{}", 400, this);
  }
};

// ── ErrorMockNam — returns 500 for every request ──────────────────────────

class ErrorMockNam : public QNetworkAccessManager {
  Q_OBJECT
protected:
  QNetworkReply* createRequest(Operation, const QNetworkRequest& req,
                               QIODevice*) override {
    return new MockReply(req, "server error", 500, this);
  }
};

// ── PaginatedMockNam — first call has nextLink, second has final results ───

class PaginatedMockNam : public QNetworkAccessManager {
  Q_OBJECT
  int callCount_ = 0;
protected:
  QNetworkReply* createRequest(Operation, const QNetworkRequest& req,
                               QIODevice*) override {
    if (callCount_++ == 0) {
      const QByteArray resp =
        R"({"value":[{"id":"P1","name":"page1.odenc","file":{},"size":1}],)"
        R"("@odata.nextLink":"https://graph.microsoft.com/v1.0/me/drive/root/children?$skiptoken=p2"})";
      return new MockReply(req, resp, 200, this);
    }
    const QByteArray resp =
      R"({"value":[{"id":"P2","name":"page2.odenc","file":{},"size":2}]})";
    return new MockReply(req, resp, 200, this);
  }
};

// ── Test class ────────────────────────────────────────────────────────────

class GraphMockTests : public QObject {
  Q_OBJECT
private slots:
  // Upload
  void createUploadSession_requestHasGraphUrl();
  void uploadLargeFile_noToken_emitsFailed();
  // Listing
  void listChildren_rootEmitsItems();
  void listChildren_folderEmitsItems();
  void listChildren_noToken_emitsFailed();
  void listChildren_serverError_emitsFailed();
  void listChildren_pagination_combinesPages();
  // Download
  void downloadItemContent_noToken_emitsFailed();
  void downloadItemContent_success_writesFile();
};

// ── Upload tests ──────────────────────────────────────────────────────────

void GraphMockTests::createUploadSession_requestHasGraphUrl() {
  MockNam nam;
  GraphClient client(&nam);
  client.setAccessToken("TOKEN");

  QSignalSpy spyFail(&client, &GraphClient::failed);

  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString fp = dir.path() + "/a.bin";
  QFile f(fp); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QByteArray(1000, 'a')); f.close();

  client.uploadLargeFileToPath(fp, "/Apps/Test", "a.bin");

  QTRY_VERIFY_WITH_TIMEOUT(!nam.requests.isEmpty(), 500);
  QCOMPARE(spyFail.count(), 0);

  const QString firstUrl = nam.requests.first().url().toString();
  QVERIFY2(firstUrl.contains("graph.microsoft.com"),
           qPrintable("Expected graph.microsoft.com, got: " + firstUrl));
  QVERIFY2(firstUrl.contains("createUploadSession"),
           qPrintable("Expected createUploadSession, got: " + firstUrl));
}

void GraphMockTests::uploadLargeFile_noToken_emitsFailed() {
  MockNam nam;
  GraphClient client(&nam);
  // intentionally no setAccessToken()

  QSignalSpy spy(&client, &GraphClient::failed);

  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString fp = dir.path() + "/a.bin";
  QFile f(fp); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); f.close();

  client.uploadLargeFileToPath(fp, "/Apps/Test", "a.bin");

  QCOMPARE(spy.count(), 1);
  QVERIFY(spy.first().first().toString().contains("sign", Qt::CaseInsensitive));
}

// ── Listing tests ─────────────────────────────────────────────────────────

void GraphMockTests::listChildren_rootEmitsItems() {
  MockNam nam;
  GraphClient client(&nam);
  client.setAccessToken("TOKEN");

  QSignalSpy spy(&client, &GraphClient::childrenListed);
  client.listChildren(QString());
  QTRY_VERIFY(spy.count() == 1);

  const auto args = spy.takeFirst();
  QCOMPARE(args.at(0).toString(), QString());
  const auto vec = qvariant_cast<QVector<DriveItemInfo>>(args.at(1));
  QCOMPARE(vec.size(), 2);
  QVERIFY(vec[0].isFolder);   // folders sorted first
}

void GraphMockTests::listChildren_folderEmitsItems() {
  MockNam nam;
  GraphClient client(&nam);
  client.setAccessToken("TOKEN");

  QSignalSpy spy(&client, &GraphClient::childrenListed);
  client.listChildren("F1");
  QTRY_VERIFY(spy.count() == 1);

  const auto args = spy.takeFirst();
  QCOMPARE(args.at(0).toString(), QString("F1"));
  const auto vec = qvariant_cast<QVector<DriveItemInfo>>(args.at(1));
  QCOMPARE(vec.size(), 1);
  QCOMPARE(vec[0].name, QString("nested.odenc"));
}

void GraphMockTests::listChildren_noToken_emitsFailed() {
  MockNam nam;
  GraphClient client(&nam);
  // no token

  QSignalSpy spy(&client, &GraphClient::failed);
  client.listChildren(QString());

  // failed is emitted synchronously before listChildren returns
  QCOMPARE(spy.count(), 1);
  QVERIFY(spy.first().first().toString().contains("sign", Qt::CaseInsensitive));
}

void GraphMockTests::listChildren_serverError_emitsFailed() {
  ErrorMockNam nam;
  GraphClient client(&nam);
  client.setAccessToken("TOKEN");

  QSignalSpy spyFail(&client, &GraphClient::failed);
  client.listChildren(QString());
  QTRY_VERIFY(spyFail.count() == 1);
  QVERIFY(spyFail.first().first().toString().contains("500"));
}

void GraphMockTests::listChildren_pagination_combinesPages() {
  PaginatedMockNam nam;
  GraphClient client(&nam);
  client.setAccessToken("TOKEN");

  QSignalSpy spy(&client, &GraphClient::childrenListed);
  client.listChildren(QString());
  QTRY_VERIFY(spy.count() == 1);

  const auto vec = qvariant_cast<QVector<DriveItemInfo>>(spy.first().at(1));
  QCOMPARE(vec.size(), 2);  // one item from each page
}

// ── Download tests ────────────────────────────────────────────────────────

void GraphMockTests::downloadItemContent_noToken_emitsFailed() {
  MockNam nam;
  GraphClient client(&nam);
  // no token

  QSignalSpy spy(&client, &GraphClient::failed);
  client.downloadItemContent("ITEM123", "/tmp/out.bin");

  QCOMPARE(spy.count(), 1);
  QVERIFY(spy.first().first().toString().contains("sign", Qt::CaseInsensitive));
}

void GraphMockTests::downloadItemContent_success_writesFile() {
  MockNam nam;
  GraphClient client(&nam);
  client.setAccessToken("TOKEN");

  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString outPath = dir.path() + "/downloaded.bin";

  QSignalSpy spyDone(&client, &GraphClient::finished);
  QSignalSpy spyFail(&client, &GraphClient::failed);

  client.downloadItemContent("ITEM123", outPath);
  QTRY_VERIFY(spyDone.count() == 1 || spyFail.count() == 1);

  QCOMPARE(spyFail.count(), 0);
  QCOMPARE(spyDone.count(), 1);

  QFile out(outPath);
  QVERIFY(out.open(QIODevice::ReadOnly));
  QCOMPARE(out.readAll(), QByteArray("FILE_CONTENT"));
}

int runGraphMockTests(int argc, char** argv) {
  GraphMockTests tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "test_graph_mock.moc"
