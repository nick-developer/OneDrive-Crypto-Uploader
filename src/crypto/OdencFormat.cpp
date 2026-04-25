#include "OdencFormat.h"
#include <QDataStream>

namespace odenc {

QByteArray serializeHeader(const Header& h) {
  QByteArray buf;
  QDataStream ds(&buf, QIODevice::WriteOnly);
  ds.setByteOrder(QDataStream::LittleEndian);

  ds.writeRawData(kMagic, 6);
  ds << (quint16)h.version;
  ds << (quint8)h.kdf;
  ds << (quint32)h.chunkSize;

  ds << (quint16)h.salt.size();
  ds.writeRawData(h.salt.constData(), h.salt.size());

  ds << (quint16)h.fileNonceBase.size();
  ds.writeRawData(h.fileNonceBase.constData(), h.fileNonceBase.size());

  const auto nameUtf8 = h.originalName.toUtf8();
  ds << (quint16)nameUtf8.size();
  ds.writeRawData(nameUtf8.constData(), nameUtf8.size());

  return buf;
}

bool parseHeader(const QByteArray& bytes, Header* out, QString* err) {
  if (bytes.size() < 6 + 2 + 1 + 4 + 2) {
    if (err) *err = "Header too small";
    return false;
  }
  QDataStream ds(bytes);
  ds.setByteOrder(QDataStream::LittleEndian);

  char magic[6];
  if (ds.readRawData(magic, 6) != 6 || QByteArray(magic, 6) != QByteArray(kMagic, 6)) {
    if (err) *err = "Bad magic";
    return false;
  }

  quint16 ver; quint8 kdf; quint32 chunk;
  ds >> ver >> kdf >> chunk;
  if (ver != kVersion) { if (err) *err = "Unsupported version"; return false; }
  if (chunk == 0 || chunk > (64u * 1024u * 1024u)) { if (err) *err = "Invalid chunk size"; return false; }

  quint16 saltLen; ds >> saltLen;
  if (saltLen > 1024) { if (err) *err = "Salt too large"; return false; }
  QByteArray salt(saltLen, '\0');
  if (ds.readRawData(salt.data(), saltLen) != saltLen) { if (err) *err = "Salt truncated"; return false; }

  quint16 nbLen; ds >> nbLen;
  if (nbLen > 64) { if (err) *err = "Nonce base too large"; return false; }
  QByteArray nonceBase(nbLen, '\0');
  if (ds.readRawData(nonceBase.data(), nbLen) != nbLen) { if (err) *err = "Nonce truncated"; return false; }

  quint16 nameLen; ds >> nameLen;
  if (nameLen > 4096) { if (err) *err = "Name too large"; return false; }
  QByteArray nameBytes(nameLen, '\0');
  if (ds.readRawData(nameBytes.data(), nameLen) != nameLen) { if (err) *err = "Name truncated"; return false; }

  if (out) {
    out->version = ver;
    out->kdf = static_cast<KdfId>(kdf);
    out->chunkSize = chunk;
    out->salt = salt;
    out->fileNonceBase = nonceBase;
    out->originalName = QString::fromUtf8(nameBytes);
  }
  return true;
}

}
