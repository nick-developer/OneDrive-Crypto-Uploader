#include "JsonConfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

AppConfig JsonConfig::load(const QString& path, QString* errorOut) {
  AppConfig cfg;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (errorOut) *errorOut = "Failed to open config: " + path;
    return cfg;
  }
  const auto doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isObject()) {
    if (errorOut) *errorOut = "Config is not a JSON object";
    return cfg;
  }
  const auto o = doc.object();
  cfg.clientId = o.value("clientId").toString();
  cfg.tenant = o.value("tenant").toString("common");
  cfg.redirectPort = o.value("redirectPort").toInt(8400);
  cfg.defaultUploadFolder = o.value("defaultUploadFolder").toString("/Apps/OneDriveCryptoUploader");

  const auto arr = o.value("scopes").toArray();
  for (const auto& v : arr) cfg.scopes.push_back(v.toString());
  if (cfg.scopes.isEmpty()) cfg.scopes = {"Files.ReadWrite", "User.Read"};

  return cfg;
}
