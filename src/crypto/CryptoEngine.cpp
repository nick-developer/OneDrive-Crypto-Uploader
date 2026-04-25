#include "CryptoEngine.h"
#include "OdencFormat.h"
#include "../util/SecureZero.h"

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDataStream>
#include <QtEndian>
#include <stdexcept>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace {

QByteArray randomBytes(int n) {
  QByteArray b(n, '\0');
  if (RAND_bytes(reinterpret_cast<unsigned char*>(b.data()), n) != 1)
    throw std::runtime_error("RAND_bytes failed");
  return b;
}

QByteArray pbkdf2Key(const QString& pass, const QByteArray& salt, int iterations, int keyLen) {
  QByteArray key(keyLen, '\0');
  const QByteArray passUtf8 = pass.toUtf8();
  if (PKCS5_PBKDF2_HMAC(passUtf8.constData(), passUtf8.size(),
                        reinterpret_cast<const unsigned char*>(salt.constData()), salt.size(),
                        iterations, EVP_sha256(), keyLen,
                        reinterpret_cast<unsigned char*>(key.data())) != 1) {
    throw std::runtime_error("PBKDF2 failed");
  }
  return key;
}

QByteArray deriveChunkNonce(const QByteArray& baseNonce12, quint32 chunkIndex) {
  QByteArray tmp;
  tmp.reserve(12 + 4);
  tmp.append(baseNonce12);
  const quint32 le = qToLittleEndian(chunkIndex);
  tmp.append(reinterpret_cast<const char*>(&le), 4);
  const auto hash = QCryptographicHash::hash(tmp, QCryptographicHash::Sha256);
  return hash.left(12);
}

struct GcmResult { QByteArray ciphertext; QByteArray tag; };

GcmResult aes256gcmEncrypt(const QByteArray& key32, const QByteArray& nonce12, const QByteArray& plaintext) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

  GcmResult r;
  r.ciphertext.resize(plaintext.size());
  r.tag.resize(16);

  int len = 0, outLen = 0;

  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
    throw std::runtime_error("EncryptInit failed");
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce12.size(), nullptr) != 1)
    throw std::runtime_error("SET_IVLEN failed");
  if (EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                         reinterpret_cast<const unsigned char*>(key32.constData()),
                         reinterpret_cast<const unsigned char*>(nonce12.constData())) != 1)
    throw std::runtime_error("EncryptInit key/iv failed");

  if (EVP_EncryptUpdate(ctx,
                        reinterpret_cast<unsigned char*>(r.ciphertext.data()), &len,
                        reinterpret_cast<const unsigned char*>(plaintext.constData()), plaintext.size()) != 1)
    throw std::runtime_error("EncryptUpdate failed");
  outLen = len;

  if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(r.ciphertext.data()) + outLen, &len) != 1)
    throw std::runtime_error("EncryptFinal failed");
  outLen += len;
  r.ciphertext.resize(outLen);

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, r.tag.data()) != 1)
    throw std::runtime_error("GET_TAG failed");

  EVP_CIPHER_CTX_free(ctx);
  return r;
}

QByteArray aes256gcmDecrypt(const QByteArray& key32, const QByteArray& nonce12,
                           const QByteArray& ciphertext, const QByteArray& tag16) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

  QByteArray plaintext(ciphertext.size(), '\0');
  int len = 0, outLen = 0;

  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
    throw std::runtime_error("DecryptInit failed");
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce12.size(), nullptr) != 1)
    throw std::runtime_error("SET_IVLEN failed");
  if (EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                         reinterpret_cast<const unsigned char*>(key32.constData()),
                         reinterpret_cast<const unsigned char*>(nonce12.constData())) != 1)
    throw std::runtime_error("DecryptInit key/iv failed");

  if (EVP_DecryptUpdate(ctx,
                        reinterpret_cast<unsigned char*>(plaintext.data()), &len,
                        reinterpret_cast<const unsigned char*>(ciphertext.constData()), ciphertext.size()) != 1)
    throw std::runtime_error("DecryptUpdate failed");
  outLen = len;

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<char*>(tag16.constData())) != 1)
    throw std::runtime_error("SET_TAG failed");

  const int finalOk = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + outLen, &len);
  EVP_CIPHER_CTX_free(ctx);
  if (finalOk != 1) throw std::runtime_error("Authentication failed (wrong password or tampered data)");

  outLen += len;
  plaintext.resize(outLen);
  return plaintext;
}

}

QString CryptoEngine::peekOriginalName(const QString& encryptedPath) {
  QFile in(encryptedPath);
  if (!in.open(QIODevice::ReadOnly)) return {};

  QDataStream ds(&in);
  ds.setByteOrder(QDataStream::LittleEndian);
  quint32 headerLen = 0;
  ds >> headerLen;
  if (headerLen == 0 || headerLen > 1024 * 1024) return {};

  const QByteArray headerBytes = in.read(headerLen);
  if ((quint32)headerBytes.size() != headerLen) return {};

  odenc::Header h;
  QString err;
  if (!odenc::parseHeader(headerBytes, &h, &err)) return {};
  return h.originalName;
}

void CryptoEngine::encryptFile(const QString& inPath, const QString& outPath,
                              const QString& passphrase, const Params& p) {
  QFile in(inPath);
  if (!in.open(QIODevice::ReadOnly)) throw std::runtime_error("Failed to open input file");

  QFile out(outPath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) throw std::runtime_error("Failed to open output file");

  const QByteArray salt = randomBytes(p.saltSize);
  const QByteArray nonceBase = randomBytes(12);
  const QByteArray key = pbkdf2Key(passphrase, salt, p.pbkdf2Iterations, 32);

  odenc::Header h;
  h.salt = salt;
  h.fileNonceBase = nonceBase;
  h.chunkSize = p.chunkSize;
  h.originalName = QFileInfo(in).fileName();

  const QByteArray headerBytes = odenc::serializeHeader(h);

  QDataStream ds(&out);
  ds.setByteOrder(QDataStream::LittleEndian);
  ds << (quint32)headerBytes.size();
  out.write(headerBytes);

  std::vector<char> buf(p.chunkSize);
  quint32 chunkIndex = 0;
  while (true) {
    const qint64 n = in.read(buf.data(), buf.size());
    if (n < 0) throw std::runtime_error("Read error");
    if (n == 0) break;

    QByteArray plain(buf.data(), (int)n);
    const QByteArray nonce = deriveChunkNonce(nonceBase, chunkIndex);
    auto enc = aes256gcmEncrypt(key, nonce, plain);

    ds << (quint32)chunkIndex;
    ds << (quint32)enc.ciphertext.size();
    out.write(enc.ciphertext);
    out.write(enc.tag);

    chunkIndex++;
  }

  util::secureZero(const_cast<char*>(key.constData()), (std::size_t)key.size());
}

void CryptoEngine::decryptFile(const QString& inPath, const QString& outPath,
                              const QString& passphrase) {
  QFile in(inPath);
  if (!in.open(QIODevice::ReadOnly)) throw std::runtime_error("Failed to open encrypted file");

  QFile out(outPath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) throw std::runtime_error("Failed to open output file");

  QDataStream ds(&in);
  ds.setByteOrder(QDataStream::LittleEndian);

  quint32 headerLen = 0;
  ds >> headerLen;
  if (headerLen == 0 || headerLen > 1024*1024) throw std::runtime_error("Invalid header length");

  const QByteArray headerBytes = in.read(headerLen);
  if ((quint32)headerBytes.size() != headerLen) throw std::runtime_error("Truncated header");

  odenc::Header h;
  QString err;
  if (!odenc::parseHeader(headerBytes, &h, &err))
    throw std::runtime_error(("Header parse error: " + err).toStdString());

  const int iterations = 200000;
  const QByteArray key = pbkdf2Key(passphrase, h.salt, iterations, 32);

  while (!ds.atEnd()) {
    quint32 chunkIndex = 0;
    quint32 ctLen = 0;
    ds >> chunkIndex;
    if (ds.status() != QDataStream::Ok) break;
    ds >> ctLen;
    if (ctLen > (64u*1024u*1024u)) throw std::runtime_error("Ciphertext length too large");

    QByteArray ct = in.read(ctLen);
    if ((quint32)ct.size() != ctLen) throw std::runtime_error("Truncated ciphertext");

    QByteArray tag = in.read(16);
    if (tag.size() != 16) throw std::runtime_error("Truncated tag");

    const QByteArray nonce = deriveChunkNonce(h.fileNonceBase, chunkIndex);
    const QByteArray plain = aes256gcmDecrypt(key, nonce, ct, tag);
    out.write(plain);
  }

  util::secureZero(const_cast<char*>(key.constData()), (std::size_t)key.size());
}
