#include "fin/auth/SecretStore.h"

#include "fin/auth/Password.h"

#include <QCryptographicHash>

namespace fin {
namespace {

constexpr char kPrefix[] = "v1:";
constexpr char kPepper[] = "finik.secret.v1";

QByteArray keystream(const QByteArray & key, int length)
{
  QByteArray stream;
  stream.reserve(length);
  int counter = 0;
  while (stream.size() < length) {
    QByteArray block = key;
    block.append(static_cast<char>((counter >> 24) & 0xff));
    block.append(static_cast<char>((counter >> 16) & 0xff));
    block.append(static_cast<char>((counter >> 8) & 0xff));
    block.append(static_cast<char>(counter & 0xff));
    const QByteArray digest = QCryptographicHash::hash(block, QCryptographicHash::Sha256);
    stream.append(digest);
    ++counter;
  }
  stream.resize(length);
  return stream;
}

QByteArray xorBytes(const QByteArray & data, const QByteArray & key)
{
  QByteArray out = data;
  const QByteArray stream = keystream(key, data.size());
  for (int i = 0; i < out.size(); ++i) {
    out[i] = static_cast<char>(static_cast<unsigned char>(out.at(i)) ^
                               static_cast<unsigned char>(stream.at(i)));
  }
  return out;
}

} // namespace

Result<QString> sealSecret(const QString & plain)
{
  const QString trimmed = plain.trimmed();
  if (trimmed.isEmpty()) {
    return Result<QString>::ok(QString());
  }
  const QByteArray salt = randomSalt(16);
  const Result<QByteArray> key = pbkdf2Sha256(QString::fromLatin1(kPepper), salt, 10000);
  if (!key) {
    return Result<QString>::fail(key.error());
  }
  const QByteArray cipher = xorBytes(trimmed.toUtf8(), key.value());
  return Result<QString>::ok(QString::fromLatin1(kPrefix) + QString::fromLatin1(salt.toHex()) +
                             QLatin1Char(':') + QString::fromLatin1(cipher.toHex()));
}

Result<QString> unsealSecret(const QString & stored)
{
  const QString trimmed = stored.trimmed();
  if (trimmed.isEmpty()) {
    return Result<QString>::ok(QString());
  }
  if (!trimmed.startsWith(QLatin1String(kPrefix))) {
    // Legacy plaintext until next save.
    return Result<QString>::ok(trimmed);
  }
  const QString payload = trimmed.mid(static_cast<int>(sizeof(kPrefix) - 1));
  const int sep = payload.indexOf(QLatin1Char(':'));
  if (sep <= 0 || sep >= payload.size() - 1) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("пошкоджений зашифрований секрет")));
  }
  const QByteArray salt = QByteArray::fromHex(payload.left(sep).toLatin1());
  const QByteArray cipher = QByteArray::fromHex(payload.mid(sep + 1).toLatin1());
  if (salt.size() < 8 || cipher.isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("пошкоджений зашифрований секрет")));
  }
  const Result<QByteArray> key = pbkdf2Sha256(QString::fromLatin1(kPepper), salt, 10000);
  if (!key) {
    return Result<QString>::fail(key.error());
  }
  const QByteArray plain = xorBytes(cipher, key.value());
  return Result<QString>::ok(QString::fromUtf8(plain));
}

} // namespace fin
