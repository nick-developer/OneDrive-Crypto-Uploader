#pragma once
#include <QString>
#include <QByteArray>
#include <cstdint>

namespace odenc {

static constexpr const char* kMagic = "ODENC1";
static constexpr std::uint16_t kVersion = 0x0002;

enum class KdfId : std::uint8_t { PBKDF2_SHA256 = 1 };

struct Header {
  std::uint16_t version  = kVersion;
  KdfId         kdf      = KdfId::PBKDF2_SHA256;
  std::uint32_t chunkSize  = 1024 * 1024;
  std::uint32_t kdfRounds  = 200'000;   // PBKDF2 iteration count stored in file
  QByteArray salt;
  QByteArray fileNonceBase;
  QString originalName;
};

QByteArray serializeHeader(const Header& h);
bool parseHeader(const QByteArray& bytes, Header* out, QString* err);

}
