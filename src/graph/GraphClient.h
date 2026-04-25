#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QVector>
#include <functional> // std::function

class QFile;

struct DriveItemInfo {
  QString id;
  QString name;
  bool isFolder = false;
  qint64 size = -1;
};
Q_DECLARE_METATYPE(DriveItemInfo)
Q_DECLARE_METATYPE(QVector<DriveItemInfo>)

class GraphClient : public QObject {
  Q_OBJECT
public:
  explicit GraphClient(QNetworkAccessManager* nam, QObject* parent=nullptr);

  void setAccessToken(const QString& token);

  void uploadLargeFileToPath(const QString& localPath,
                             const QString& folderPath,
                             const QString& remoteFileName);

  void downloadItemContent(const QString& itemId, const QString& outPath);

  void listChildren(const QString& parentItemId);

signals:
  void progress(qint64 done, qint64 total);
  void finished(const QString& message);
  void failed(const QString& message);
  void childrenListed(const QString& parentItemId, const QVector<DriveItemInfo>& items);

private:
  QNetworkAccessManager* nam_;
  QString token_;

  QNetworkRequest authedRequest(const QUrl& url) const;

  void createUploadSessionByPath(const QString& folderPath,
                                 const QString& remoteFileName,
                                 std::function<void(QUrl uploadUrl)> onReady);

  void putChunk(const QUrl& uploadUrl,
                QFile* file,
                qint64 start,
                qint64 end,
                qint64 total,
                std::function<void()> onDone);

  void listChildrenPaged(const QString& parentItemId, const QUrl& url, QVector<DriveItemInfo> accum);
};
