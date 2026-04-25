#include <QtTest/QtTest>
#include "../src/crypto/OdencFormat.h"

class FormatTests : public QObject {
  Q_OBJECT
private slots:
  void header_roundTrip();
  void header_badMagic();
};

void FormatTests::header_roundTrip() {
  odenc::Header h;
  h.salt = QByteArray::fromHex("00112233445566778899aabbccddeeff");
  h.fileNonceBase = QByteArray::fromHex("0102030405060708090a0b0c");
  h.chunkSize = 12345;
  h.originalName = "example.txt";

  const auto bytes = odenc::serializeHeader(h);
  odenc::Header out;
  QString err;
  QVERIFY(odenc::parseHeader(bytes, &out, &err));
  QCOMPARE(out.chunkSize, h.chunkSize);
  QCOMPARE(out.salt, h.salt);
  QCOMPARE(out.fileNonceBase, h.fileNonceBase);
  QCOMPARE(out.originalName, h.originalName);
}

void FormatTests::header_badMagic() {
  odenc::Header h;
  h.salt = QByteArray(16, 'x');
  h.fileNonceBase = QByteArray(12, 'y');
  h.originalName = "a";

  auto bytes = odenc::serializeHeader(h);
  bytes[0] = 'X';

  odenc::Header out;
  QString err;
  QVERIFY(!odenc::parseHeader(bytes, &out, &err));
}

int runFormatTests(int argc, char** argv) {
  FormatTests tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "test_format.moc"
