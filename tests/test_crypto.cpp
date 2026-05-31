#include <QtTest/QtTest>
#include "../src/crypto/CryptoEngine.h"
#include <QTemporaryDir>
#include <QFile>

#define VERIFY_NO_THROW(expr) \
  do { \
    try { expr; } \
    catch (const std::exception& _e) { QFAIL(_e.what()); } \
    catch (...) { QFAIL("Unexpected exception thrown"); } \
  } while (false)

class CryptoTests : public QObject {
  Q_OBJECT
private slots:
  // Round-trip correctness
  void roundTrip_small();
  void roundTrip_multiChunk();
  void roundTrip_emptyFile();
  // Wrong key / tamper
  void wrongPassword_fails();
  void tamper_fails();
  // Header peek
  void peekOriginalName_readsHeader();
  void peekOriginalName_missingFile_returnsEmpty();
  void peekOriginalName_garbageFile_returnsEmpty();
  // Error paths
  void encryptFile_missingInput_throws();
};

// ── Helpers ────────────────────────────────────────────────────────────────

static CryptoEngine::Params fastParams(quint32 chunkBytes = 512) {
  CryptoEngine::Params p;
  p.chunkSize       = chunkBytes;
  p.pbkdf2Iterations = 1000;   // low iterations so tests run quickly
  return p;
}

// ── Tests ──────────────────────────────────────────────────────────────────

void CryptoTests::roundTrip_small() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString plain = dir.path() + "/plain.txt";
  const QString enc   = dir.path() + "/plain.txt.odenc";
  const QString out   = dir.path() + "/out.txt";

  QFile f(plain); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("hello world"); f.close();

  VERIFY_NO_THROW(CryptoEngine::encryptFile(plain, enc, "pass"));
  VERIFY_NO_THROW(CryptoEngine::decryptFile(enc, out, "pass"));

  QFile in2(plain), out2(out);
  QVERIFY(in2.open(QIODevice::ReadOnly));
  QVERIFY(out2.open(QIODevice::ReadOnly));
  QCOMPARE(in2.readAll(), out2.readAll());
}

void CryptoTests::roundTrip_multiChunk() {
  // Uses 512-byte chunks so a 2 KiB file produces 4 chunks.
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString plain = dir.path() + "/multi.bin";
  const QString enc   = dir.path() + "/multi.bin.odenc";
  const QString out   = dir.path() + "/out.bin";

  const QByteArray content(2048, 'X');
  QFile f(plain); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(content); f.close();

  const auto p = fastParams(512);
  VERIFY_NO_THROW(CryptoEngine::encryptFile(plain, enc, "testpass", p));
  VERIFY_NO_THROW(CryptoEngine::decryptFile(enc, out, "testpass"));

  QFile out2(out);
  QVERIFY(out2.open(QIODevice::ReadOnly));
  QCOMPARE(out2.readAll(), content);
}

void CryptoTests::roundTrip_emptyFile() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString plain = dir.path() + "/empty.dat";
  const QString enc   = dir.path() + "/empty.dat.odenc";
  const QString out   = dir.path() + "/out.dat";

  QFile f(plain); QVERIFY(f.open(QIODevice::WriteOnly)); f.close();

  const auto p = fastParams();
  VERIFY_NO_THROW(CryptoEngine::encryptFile(plain, enc, "pass", p));
  VERIFY_NO_THROW(CryptoEngine::decryptFile(enc, out, "pass"));

  QFile out2(out);
  QVERIFY(out2.open(QIODevice::ReadOnly));
  QCOMPARE(out2.readAll(), QByteArray());
}

void CryptoTests::wrongPassword_fails() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString plain = dir.path() + "/plain.bin";
  const QString enc   = dir.path() + "/plain.bin.odenc";
  const QString out   = dir.path() + "/out.bin";

  QFile f(plain); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QByteArray(1024, 'A')); f.close();
  const auto p = fastParams();
  VERIFY_NO_THROW(CryptoEngine::encryptFile(plain, enc, "good", p));

  bool threw = false;
  try { CryptoEngine::decryptFile(enc, out, "bad"); } catch (...) { threw = true; }
  QVERIFY(threw);
}

void CryptoTests::tamper_fails() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString plain = dir.path() + "/plain.bin";
  const QString enc   = dir.path() + "/plain.bin.odenc";
  const QString out   = dir.path() + "/out.bin";

  QFile f(plain); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QByteArray(2048, 'B')); f.close();
  const auto p = fastParams();
  VERIFY_NO_THROW(CryptoEngine::encryptFile(plain, enc, "pass", p));

  // Flip a bit 20 bytes before EOF (inside the last GCM tag or ciphertext).
  QFile ef(enc); QVERIFY(ef.open(QIODevice::ReadWrite));
  QVERIFY(ef.size() > 50);
  ef.seek(ef.size() - 20);
  char b; QVERIFY(ef.getChar(&b)); b ^= 0x01;
  ef.seek(ef.size() - 20);
  QVERIFY(ef.putChar(b));
  ef.close();

  bool threw = false;
  try { CryptoEngine::decryptFile(enc, out, "pass"); } catch (...) { threw = true; }
  QVERIFY(threw);
}

void CryptoTests::peekOriginalName_readsHeader() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString plain = dir.path() + "/photo.png";
  const QString enc   = dir.path() + "/photo.png.odenc";

  QFile f(plain); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QByteArray(10, 'Z')); f.close();
  const auto p = fastParams();
  VERIFY_NO_THROW(CryptoEngine::encryptFile(plain, enc, "pass", p));

  QCOMPARE(CryptoEngine::peekOriginalName(enc), QString("photo.png"));
}

void CryptoTests::peekOriginalName_missingFile_returnsEmpty() {
  QVERIFY(CryptoEngine::peekOriginalName("/does/not/exist.odenc").isEmpty());
}

void CryptoTests::peekOriginalName_garbageFile_returnsEmpty() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString path = dir.path() + "/garbage.odenc";
  QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QByteArray(64, '\xAB'));  // not a valid ODENC file
  f.close();
  QVERIFY(CryptoEngine::peekOriginalName(path).isEmpty());
}

void CryptoTests::encryptFile_missingInput_throws() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  bool threw = false;
  try {
    CryptoEngine::encryptFile(dir.path() + "/nonexistent.bin",
                              dir.path() + "/out.odenc", "pass");
  } catch (...) { threw = true; }
  QVERIFY(threw);
}

int runCryptoTests(int argc, char** argv) {
  CryptoTests tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "test_crypto.moc"
