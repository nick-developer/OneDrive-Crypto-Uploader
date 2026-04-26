#include <QtTest/QtTest>
#include <QTimer>
#include "../src/graph/GraphClient.h"
#include <QNetworkReply>

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

class MockNam : public QNetworkAccessManager {
  Q_OBJECT
public:
  // All requests in order — lets tests check the first (upload session) vs last (chunk PUT)
  QList<QNetworkRequest> requests;

protected:
  QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                               QIODevice* outgoingData) override
  {
    Q_UNUSED(outgoingData);
    requests.append(request);
    const QString url = request.url().toString();

    if (op == PostOperation && url.contains("createUploadSession")) {
      const QByteArray resp = R"({"uploadUrl":"https://upload.example/session"})";
      return new MockReply(request, resp, 200, this);
    }

    if (op == PutOperation && url.startsWith("https://upload.example")) {
      return new MockReply(request, "{}", 202, this);
    }

    if (op == GetOperation && url.contains("/me/drive/root/children")) {
      const QByteArray resp = R"({
        "value": [
          {"id":"F1","name":"FolderA","folder":{"childCount":1}},
          {"id":"X1","name":"file.odenc","file":{},"size":123}
        ]
      })";
      return new MockReply(request, resp, 200, this);
    }

    if (op == GetOperation && url.contains("/me/drive/items/F1/children")) {
      const QByteArray resp = R"({
        "value": [
          {"id":"X2","name":"nested.odenc","file":{},"size":456}
        ]
      })";
      return new MockReply(request, resp, 200, this);
    }

    return new MockReply(request, "{}", 400, this);
  }
};

class GraphMockTests : public QObject {
  Q_OBJECT
private slots:
  void createUploadSession_requestHasGraphUrl();
  void listChildren_rootEmitsItems();
  void listChildren_folderEmitsItems();
};

void GraphMockTests::createUploadSession_requestHasGraphUrl() {
  MockNam nam;
  GraphClient client(&nam);
  client.setAccessToken("TOKEN");

  QSignalSpy spyFail(&client, &GraphClient::failed);

  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString fp = dir.path() + "/a.bin";
  QFile f(fp); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QByteArray(1000, 'a')); f.close();

  client.uploadLargeFileToPath(fp, "/Apps/Test", "a.bin");

  // Wait for at least the upload-session POST to complete; 500 ms is generous.
  QTRY_VERIFY_WITH_TIMEOUT(!nam.requests.isEmpty(), 500);

  QCOMPARE(spyFail.count(), 0);

  // The FIRST request must be the createUploadSession POST to graph.microsoft.com
  const QString firstUrl = nam.requests.first().url().toString();
  QVERIFY2(firstUrl.contains("graph.microsoft.com"),
           qPrintable("Expected graph.microsoft.com, got: " + firstUrl));
  QVERIFY2(firstUrl.contains("createUploadSession"),
           qPrintable("Expected createUploadSession, got: " + firstUrl));
}

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
  QVERIFY(vec[0].isFolder);
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

int runGraphMockTests(int argc, char** argv) {
  GraphMockTests tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "test_graph_mock.moc"
