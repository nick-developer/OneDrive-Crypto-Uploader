#include <QtTest/QtTest>
#include "../src/crypto/OdencFormat.h"

class FormatTests : public QObject {
  Q_OBJECT
private slots:
  void header_roundTrip();
  void header_kdfRoundsPreserved();
  void header_unicodeName();
  void header_emptyName();
  void header_badMagic();
  void header_unsupportedVersion();
  void header_tooShort();
  void header_nullOutPointer();
};

// ── Helpers ────────────────────────────────────────────────────────────────

static odenc::Header makeHeader(const QString& name = "test.bin",
                                quint32 rounds = 5000) {
  odenc::Header h;
  h.salt         = QByteArray(16, 's');
  h.fileNonceBase = QByteArray(12, 'n');
  h.chunkSize    = 1024 * 1024;
  h.kdfRounds    = rounds;
  h.originalName = name;
  return h;
}

// ── Tests ──────────────────────────────────────────────────────────────────

void FormatTests::header_roundTrip() {
  odenc::Header h;
  h.salt         = QByteArray::fromHex("00112233445566778899aabbccddeeff");
  h.fileNonceBase = QByteArray::fromHex("0102030405060708090a0b0c");
  h.chunkSize    = 12345;
  h.kdfRounds    = 99999;
  h.originalName = "example.txt";

  const auto bytes = odenc::serializeHeader(h);
  odenc::Header out;
  QString err;
  QVERIFY(odenc::parseHeader(bytes, &out, &err));
  QCOMPARE(out.chunkSize,    h.chunkSize);
  QCOMPARE(out.kdfRounds,    h.kdfRounds);
  QCOMPARE(out.salt,         h.salt);
  QCOMPARE(out.fileNonceBase, h.fileNonceBase);
  QCOMPARE(out.originalName, h.originalName);
}

void FormatTests::header_kdfRoundsPreserved() {
  const quint32 rounds = 12345;
  const auto bytes = odenc::serializeHeader(makeHeader("a.bin", rounds));

  odenc::Header out;
  QString err;
  QVERIFY(odenc::parseHeader(bytes, &out, &err));
  QCOMPARE(out.kdfRounds, rounds);
}

void FormatTests::header_unicodeName() {
  // Cyrillic characters require multi-byte UTF-8 encoding.
  const QString name = QString::fromUtf8("\xD1\x84\xD0\xBE\xD1\x82\xD0\xBE.png"); // фото.png
  const auto bytes = odenc::serializeHeader(makeHeader(name));

  odenc::Header out;
  QString err;
  QVERIFY(odenc::parseHeader(bytes, &out, &err));
  QCOMPARE(out.originalName, name);
}

void FormatTests::header_emptyName() {
  const auto bytes = odenc::serializeHeader(makeHeader(""));

  odenc::Header out;
  QString err;
  QVERIFY(odenc::parseHeader(bytes, &out, &err));
  QCOMPARE(out.originalName, QString());
}

void FormatTests::header_badMagic() {
  auto bytes = odenc::serializeHeader(makeHeader());
  bytes[0] = 'X';  // corrupt first magic byte

  odenc::Header out;
  QString err;
  QVERIFY(!odenc::parseHeader(bytes, &out, &err));
  QVERIFY(!err.isEmpty());
}

void FormatTests::header_unsupportedVersion() {
  auto bytes = odenc::serializeHeader(makeHeader());
  // Version is a little-endian quint16 at offset 6 (after 6-byte magic).
  bytes[6] = static_cast<char>(0x99);
  bytes[7] = static_cast<char>(0x99);

  odenc::Header out;
  QString err;
  QVERIFY(!odenc::parseHeader(bytes, &out, &err));
  QVERIFY(err.contains("version", Qt::CaseInsensitive));
}

void FormatTests::header_tooShort() {
  odenc::Header out;
  QString err;

  QVERIFY(!odenc::parseHeader(QByteArray(), &out, &err));
  QVERIFY(!odenc::parseHeader(QByteArray(3, 'x'), &out, &err));
  // Exactly kMagic length — still too short for the fixed fields that follow.
  QVERIFY(!odenc::parseHeader(QByteArray(6, 'O'), &out, &err));
}

void FormatTests::header_nullOutPointer() {
  // parseHeader must not crash when out == nullptr (validation-only call).
  const auto bytes = odenc::serializeHeader(makeHeader());
  QString err;
  QVERIFY(odenc::parseHeader(bytes, nullptr, &err));  // valid header, null out — should return true
}

int runFormatTests(int argc, char** argv) {
  FormatTests tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "test_format.moc"
