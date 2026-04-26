#pragma once
#include <QString>
#include <cstdint>

class CryptoEngine {
public:
  struct Params {
    std::uint32_t chunkSize = 1024 * 1024;
    int pbkdf2Iterations = 200000;
    int saltSize = 16;
  };

  static void encryptFile(const QString& inPath, const QString& outPath,
                          const QString& passphrase, const Params& p);

  // Convenience overload — uses default Params{}
  static void encryptFile(const QString& inPath, const QString& outPath,
                          const QString& passphrase)
  { encryptFile(inPath, outPath, passphrase, Params{}); }

  static QString peekOriginalName(const QString& encryptedPath);

  static void decryptFile(const QString& inPath, const QString& outPath,
                          const QString& passphrase);
};
