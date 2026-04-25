#include <QtTest/QtTest>
#include "../src/crypto/CryptoEngine.h"
#include <QTemporaryDir>
#include <QFile>

class CryptoTests : public QObject {
  Q_OBJECT
private slots:
  void roundTrip_small();
  void wrongPassword_fails();
  void tamper_fails();
  void peekOriginalName_readsHeader();
};

void CryptoTests::roundTrip_small() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString plainPath = dir.path() + "/plain.txt";
  const QString encPath   = dir.path() + "/plain.txt.odenc";
  const QString outPath   = dir.path() + "/out.txt";

  QFile f(plainPath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("hello world"); f.close();

  QVERIFY_NO_THROW(CryptoEngine::encryptFile(plainPath, encPath, "pass"));
  QVERIFY_NO_THROW(CryptoEngine::decryptFile(encPath, outPath, "pass"));

  QFile in(plainPath), out(outPath);
  QVERIFY(in.open(QIODevice::ReadOnly));
  QVERIFY(out.open(QIODevice::ReadOnly));
  QCOMPARE(in.readAll(), out.readAll());
}

void CryptoTests::wrongPassword_fails() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString plainPath = dir.path() + "/plain.bin";
  const QString encPath   = dir.path() + "/plain.bin.odenc";
  const QString outPath   = dir.path() + "/out.bin";

  QFile f(plainPath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QByteArray(1024,'A')); f.close();
  QVERIFY_NO_THROW(CryptoEngine::encryptFile(plainPath, encPath, "good"));

  bool threw = false;
  try { CryptoEngine::decryptFile(encPath, outPath, "bad"); } catch (...) { threw = true; }
  QVERIFY(threw);
}

void CryptoTests::tamper_fails() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString plainPath = dir.path() + "/plain.bin";
  const QString encPath   = dir.path() + "/plain.bin.odenc";
  const QString outPath   = dir.path() + "/out.bin";

  QFile f(plainPath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QByteArray(2048,'B')); f.close();
  QVERIFY_NO_THROW(CryptoEngine::encryptFile(plainPath, encPath, "pass"));

  QFile ef(encPath); QVERIFY(ef.open(QIODevice::ReadWrite));
  QVERIFY(ef.size() > 50);
  ef.seek(ef.size() - 20);
  char b; QVERIFY(ef.getChar(&b)); b ^= 0x01;
  ef.seek(ef.size() - 20);
  QVERIFY(ef.putChar(b));
  ef.close();

  bool threw = false;
  try { CryptoEngine::decryptFile(encPath, outPath, "pass"); } catch (...) { threw = true; }
  QVERIFY(threw);
}

void CryptoTests::peekOriginalName_readsHeader() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString plainPath = dir.path() + "/photo.png";
  const QString encPath   = dir.path() + "/photo.png.odenc";

  QFile f(plainPath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QByteArray(10,'Z')); f.close();
  QVERIFY_NO_THROW(CryptoEngine::encryptFile(plainPath, encPath, "pass"));

  const QString name = CryptoEngine::peekOriginalName(encPath);
  QCOMPARE(name, QString("photo.png"));
}

int runCryptoTests(int argc, char** argv) {
  CryptoTests tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "test_crypto.moc"
