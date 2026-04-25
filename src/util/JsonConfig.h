#pragma once
#include <QString>
#include <QStringList>

struct AppConfig {
  QString clientId;
  QString tenant;
  int redirectPort = 8400;
  QStringList scopes;
  QString defaultUploadFolder;
};

class JsonConfig {
public:
  static AppConfig load(const QString& path, QString* errorOut);
};
