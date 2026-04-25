#include "GraphClient.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <memory>
#include <algorithm>

GraphClient::GraphClient(QNetworkAccessManager* nam, QObject* parent)
  : QObject(parent), nam_(nam)
{
  qRegisterMetaType<DriveItemInfo>("DriveItemInfo");
  qRegisterMetaType<QVector<DriveItemInfo>>("QVector<DriveItemInfo>");
}

void GraphClient::setAccessToken(const QString& token) { token_ = token; }

QNetworkRequest GraphClient::authedRequest(const QUrl& url) const {
  QNetworkRequest req(url);
  req.setRawHeader("Authorization", ("Bearer " + token_).toUtf8());
  req.setHeader(QNetworkRequest::UserAgentHeader, "OneDriveCryptoUploader/1.2");
  return req;
}

void GraphClient::createUploadSessionByPath(const QString& folderPath,
                                            const QString& remoteFileName,
                                            std::function<void(QUrl uploadUrl)> onReady)
{
  QString path = folderPath;
  if (!path.startsWith('/')) path.prepend('/');
  if (path.endsWith('/')) path.chop(1);

  const QUrl url(QString("https://graph.microsoft.com/v1.0/me/drive/root:%1/%2:/createUploadSession")
                     .arg(path, remoteFileName));

  QNetworkRequest req = authedRequest(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  QJsonObject item;
  item.insert("@microsoft.graph.conflictBehavior", "rename");
  item.insert("name", remoteFileName);

  QJsonObject payload;
  payload.insert("item", item);

  auto reply = nam_->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
  connect(reply, &QNetworkReply::finished, this, [this, reply, onReady](){
    const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (code < 200 || code >= 300) {
      emit failed(QString("createUploadSession failed (%1): %2").arg(code).arg(QString::fromUtf8(body)));
      return;
    }
    const auto doc = QJsonDocument::fromJson(body);
    const auto obj = doc.object();
    const auto uploadUrl = obj.value("uploadUrl").toString();
    if (uploadUrl.isEmpty()) {
      emit failed("createUploadSession response missing uploadUrl");
      return;
    }
    onReady(QUrl(uploadUrl));
  });
}

void GraphClient::putChunk(const QUrl& uploadUrl,
                           QFile* file,
                           qint64 start,
                           qint64 end,
                           qint64 total,
                           std::function<void()> onDone)
{
  const qint64 len = end - start + 1;
  if (!file->seek(start)) { emit failed("Failed to seek file"); return; }
  QByteArray data = file->read(len);
  if (data.size() != len) { emit failed("Failed to read chunk"); return; }

  QNetworkRequest req(uploadUrl);
  req.setRawHeader("Content-Length", QByteArray::number(len));
  req.setRawHeader("Content-Range",
                   QByteArray("bytes ") + QByteArray::number(start) + "-" + QByteArray::number(end) + "/" + QByteArray::number(total));

  auto reply = nam_->put(req, data);
  connect(reply, &QNetworkReply::uploadProgress, this, [this, start, total](qint64 sent, qint64){
    emit progress(start + sent, total);
  });

  connect(reply, &QNetworkReply::finished, this, [this, reply, onDone](){
    const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (code == 202 || (code >= 200 && code < 300)) { onDone(); return; }
    emit failed(QString("Chunk upload failed (%1): %2").arg(code).arg(QString::fromUtf8(body)));
  });
}

void GraphClient::uploadLargeFileToPath(const QString& localPath,
                                        const QString& folderPath,
                                        const QString& remoteFileName)
{
  if (token_.isEmpty()) { emit failed("Not signed in"); return; }

  auto file = new QFile(localPath);
  if (!file->open(QIODevice::ReadOnly)) { emit failed("Cannot open file for upload"); delete file; return; }

  const qint64 total = file->size();
  const qint64 chunkSize = 320 * 1024 * 10; // multiple of 320KiB

  createUploadSessionByPath(folderPath, remoteFileName, [this, file, total, chunkSize](QUrl uploadUrl){
    auto uploadNext = std::make_shared<std::function<void(qint64)>>();
    *uploadNext = [this, file, total, chunkSize, uploadUrl, uploadNext](qint64 start){
      if (start >= total) {
        file->close();
        file->deleteLater();
        emit finished("Upload complete");
        return;
      }
      const qint64 end = std::min(start + chunkSize - 1, total - 1);
      putChunk(uploadUrl, file, start, end, total, [uploadNext, end](){ (*uploadNext)(end + 1); });
    };
    (*uploadNext)(0);
  });
}

void GraphClient::downloadItemContent(const QString& itemId, const QString& outPath) {
  if (token_.isEmpty()) { emit failed("Not signed in"); return; }

  const QUrl url(QString("https://graph.microsoft.com/v1.0/me/drive/items/%1/content").arg(itemId));
  auto reply = nam_->get(authedRequest(url));

  connect(reply, &QNetworkReply::finished, this, [this, reply, outPath](){
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (!redirect.isEmpty()) {
      reply->deleteLater();
      auto r2 = nam_->get(QNetworkRequest(redirect));
      connect(r2, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total){ emit progress(got, total); });
      connect(r2, &QNetworkReply::finished, this, [this, r2, outPath](){
        const int c2 = r2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (c2 < 200 || c2 >= 300) { emit failed(QString("Download failed (%1): %2").arg(c2).arg(QString::fromUtf8(r2->readAll()))); r2->deleteLater(); return; }
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { emit failed("Cannot write downloaded file"); r2->deleteLater(); return; }
        out.write(r2->readAll()); out.close(); r2->deleteLater();
        emit finished("Download complete");
      });
      return;
    }

    if (code == 302) {
      const QUrl loc(QString::fromUtf8(reply->rawHeader("Location")));
      reply->deleteLater();
      auto r2 = nam_->get(QNetworkRequest(loc));
      connect(r2, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total){ emit progress(got, total); });
      connect(r2, &QNetworkReply::finished, this, [this, r2, outPath](){
        const int c2 = r2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (c2 < 200 || c2 >= 300) { emit failed(QString("Download failed (%1): %2").arg(c2).arg(QString::fromUtf8(r2->readAll()))); r2->deleteLater(); return; }
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { emit failed("Cannot write downloaded file"); r2->deleteLater(); return; }
        out.write(r2->readAll()); out.close(); r2->deleteLater();
        emit finished("Download complete");
      });
      return;
    }

    if (code < 200 || code >= 300) {
      emit failed(QString("Download request failed (%1): %2").arg(code).arg(QString::fromUtf8(reply->readAll())));
      reply->deleteLater();
      return;
    }

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { emit failed("Cannot write downloaded file"); reply->deleteLater(); return; }
    out.write(reply->readAll()); out.close();
    reply->deleteLater();
    emit finished("Download complete");
  });
}

void GraphClient::listChildren(const QString& parentItemId) {
  if (token_.isEmpty()) { emit failed("Not signed in"); return; }

  QUrl url;
  if (parentItemId.isEmpty()) {
    url = QUrl("https://graph.microsoft.com/v1.0/me/drive/root/children?$select=id,name,folder,file,size");
  } else {
    url = QUrl(QString("https://graph.microsoft.com/v1.0/me/drive/items/%1/children?$select=id,name,folder,file,size").arg(parentItemId));
  }
  listChildrenPaged(parentItemId, url, {});
}

void GraphClient::listChildrenPaged(const QString& parentItemId, const QUrl& url, QVector<DriveItemInfo> accum) {
  auto reply = nam_->get(authedRequest(url));
  connect(reply, &QNetworkReply::finished, this, [this, reply, parentItemId, accum](){
    QVector<DriveItemInfo> items = accum;

    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (code < 200 || code >= 300) {
      emit failed(QString("List children failed (%1): %2").arg(code).arg(QString::fromUtf8(body)));
      return;
    }

    const auto doc = QJsonDocument::fromJson(body);
    const auto obj = doc.object();
    const auto arr = obj.value("value").toArray();

    for (const auto& v : arr) {
      const auto o = v.toObject();
      DriveItemInfo di;
      di.id = o.value("id").toString();
      di.name = o.value("name").toString();
      di.size = (qint64)o.value("size").toDouble(-1);
      di.isFolder = o.contains("folder") && o.value("folder").isObject();
      items.push_back(di);
    }

    const QString nextLink = obj.value("@odata.nextLink").toString();
    if (!nextLink.isEmpty()) {
      listChildrenPaged(parentItemId, QUrl(nextLink), items);
      return;
    }

    std::sort(items.begin(), items.end(), [](const DriveItemInfo& a, const DriveItemInfo& b){
      if (a.isFolder != b.isFolder) return a.isFolder > b.isFolder;
      return a.name.toLower() < b.name.toLower();
    });

    emit childrenListed(parentItemId, items);
  });
}
