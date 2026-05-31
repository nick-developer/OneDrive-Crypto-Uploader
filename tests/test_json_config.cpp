#include <QtTest/QtTest>
#include "../src/util/JsonConfig.h"
#include <QTemporaryDir>
#include <QFile>

class JsonConfigTests : public QObject {
  Q_OBJECT
private slots:
  void load_validFile_parsesAllFields();
  void load_missingFile_setsError();
  void load_defaults_appliedForMissingOptionals();
  void load_defaultScopes_whenArrayAbsent();
  void load_invalidJson_setsError();
};

// ── Helpers ────────────────────────────────────────────────────────────────

static QString writeTemp(const QTemporaryDir& dir, const char* name, const QByteArray& content) {
  const QString path = dir.path() + "/" + name;
  QFile f(path);
  if (f.open(QIODevice::WriteOnly)) f.write(content);
  return path;
}

// ── Tests ──────────────────────────────────────────────────────────────────

void JsonConfigTests::load_validFile_parsesAllFields() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QByteArray json = R"({
    "clientId":           "my-client-id",
    "tenant":             "my-tenant",
    "redirectPort":       9000,
    "scopes":             ["Files.ReadWrite", "User.Read", "offline_access"],
    "defaultUploadFolder":"/Apps/MyApp"
  })";
  const QString path = writeTemp(dir, "config.json", json);

  QString err;
  const AppConfig cfg = JsonConfig::load(path, &err);

  QVERIFY(err.isEmpty());
  QCOMPARE(cfg.clientId,            QString("my-client-id"));
  QCOMPARE(cfg.tenant,              QString("my-tenant"));
  QCOMPARE(cfg.redirectPort,        9000);
  QCOMPARE(cfg.scopes,              QStringList({"Files.ReadWrite", "User.Read", "offline_access"}));
  QCOMPARE(cfg.defaultUploadFolder, QString("/Apps/MyApp"));
}

void JsonConfigTests::load_missingFile_setsError() {
  QString err;
  const AppConfig cfg = JsonConfig::load("/does/not/exist/config.json", &err);

  QVERIFY(!err.isEmpty());
  QVERIFY(cfg.clientId.isEmpty());
}

void JsonConfigTests::load_defaults_appliedForMissingOptionals() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  // Only required clientId provided; everything else must fall back to defaults.
  const QByteArray json = R"({"clientId":"only-id"})";
  const QString path = writeTemp(dir, "minimal.json", json);

  QString err;
  const AppConfig cfg = JsonConfig::load(path, &err);

  QVERIFY(err.isEmpty());
  QCOMPARE(cfg.clientId,   QString("only-id"));
  QCOMPARE(cfg.tenant,     QString("common"));
  QCOMPARE(cfg.redirectPort, 8400);
  QCOMPARE(cfg.defaultUploadFolder, QString("/Apps/OneDriveCryptoUploader"));
}

void JsonConfigTests::load_defaultScopes_whenArrayAbsent() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QByteArray json = R"({"clientId":"x"})";
  const QString path = writeTemp(dir, "noscopes.json", json);

  QString err;
  const AppConfig cfg = JsonConfig::load(path, &err);

  QVERIFY(!cfg.scopes.isEmpty());
  QVERIFY(cfg.scopes.contains("Files.ReadWrite"));
  QVERIFY(cfg.scopes.contains("User.Read"));
}

void JsonConfigTests::load_invalidJson_setsError() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString path = writeTemp(dir, "bad.json", QByteArray("not { json at all"));

  QString err;
  const AppConfig cfg = JsonConfig::load(path, &err);

  QVERIFY(!err.isEmpty());
  QVERIFY(cfg.clientId.isEmpty());
}

int runJsonConfigTests(int argc, char** argv) {
  JsonConfigTests tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "test_json_config.moc"
