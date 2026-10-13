#include "fin/auth/SecretStore.h"

#include "fin/auth/Password.h"
#include "fin/db/AppPaths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace fin {
namespace {

constexpr char kPrefixV1[] = "v1:";
constexpr char kPrefixV2[] = "v2:";
constexpr char kPepperV1[] = "finik.secret.v1";
constexpr int kInstallKeyBytes = 32;
constexpr int kPbkdf2Rounds = 10000;

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

Result<QByteArray> readInstallKeyFile()
{
  const QString path = secretKeyFilePath();
  QFile in(path);
  if (!in.exists()) {
    return Result<QByteArray>::fail(Error::validation(
        QStringLiteral("ключ секретів не знайдено — введіть токен знову")));
  }
  if (!in.open(QIODevice::ReadOnly)) {
    return Result<QByteArray>::fail(
        Error::database(QStringLiteral("не вдалося прочитати ключ секретів")));
  }
  const QByteArray key = in.readAll();
  if (key.size() != kInstallKeyBytes) {
    return Result<QByteArray>::fail(
        Error::validation(QStringLiteral("пошкоджений ключ секретів")));
  }
  return Result<QByteArray>::ok(key);
}

Result<QByteArray> loadOrCreateInstallKey()
{
  const QString path = secretKeyFilePath();
  if (QFileInfo::exists(path)) {
    return readInstallKeyFile();
  }

  QDir().mkpath(QFileInfo(path).absolutePath());
  const QByteArray key = randomSalt(kInstallKeyBytes);
  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly)) {
    return Result<QByteArray>::fail(
        Error::database(QStringLiteral("не вдалося записати ключ секретів")));
  }
  if (out.write(key) != key.size()) {
    return Result<QByteArray>::fail(
        Error::database(QStringLiteral("не вдалося записати ключ секретів")));
  }
  if (!out.commit()) {
    return Result<QByteArray>::fail(
        Error::database(QStringLiteral("не вдалося записати ключ секретів")));
  }
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return Result<QByteArray>::ok(key);
}

Result<QByteArray> deriveXorKey(const QString & material, const QByteArray & salt)
{
  return pbkdf2Sha256(material, salt, kPbkdf2Rounds);
}

Result<QString> unsealPrefixed(const QString & trimmed, const QLatin1String prefix,
                               const QString & material)
{
  const QString payload = trimmed.mid(prefix.size());
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
  const Result<QByteArray> key = deriveXorKey(material, salt);
  if (!key) {
    return Result<QString>::fail(key.error());
  }
  const QByteArray plain = xorBytes(cipher, key.value());
  return Result<QString>::ok(QString::fromUtf8(plain));
}

} // namespace

Result<QString> sealSecret(const QString & plain)
{
  const QString trimmed = plain.trimmed();
  if (trimmed.isEmpty()) {
    return Result<QString>::ok(QString());
  }
  const Result<QByteArray> installKey = loadOrCreateInstallKey();
  if (!installKey) {
    return Result<QString>::fail(installKey.error());
  }
  const QByteArray salt = randomSalt(16);
  const Result<QByteArray> key =
      deriveXorKey(QString::fromLatin1(installKey.value().toHex()), salt);
  if (!key) {
    return Result<QString>::fail(key.error());
  }
  const QByteArray cipher = xorBytes(trimmed.toUtf8(), key.value());
  return Result<QString>::ok(QString::fromLatin1(kPrefixV2) + QString::fromLatin1(salt.toHex()) +
                             QLatin1Char(':') + QString::fromLatin1(cipher.toHex()));
}

Result<QString> unsealSecret(const QString & stored)
{
  const QString trimmed = stored.trimmed();
  if (trimmed.isEmpty()) {
    return Result<QString>::ok(QString());
  }
  if (trimmed.startsWith(QLatin1String(kPrefixV2))) {
    const Result<QByteArray> installKey = readInstallKeyFile();
    if (!installKey) {
      return Result<QString>::fail(installKey.error());
    }
    return unsealPrefixed(trimmed, QLatin1String(kPrefixV2),
                          QString::fromLatin1(installKey.value().toHex()));
  }
  if (trimmed.startsWith(QLatin1String(kPrefixV1))) {
    return unsealPrefixed(trimmed, QLatin1String(kPrefixV1), QString::fromLatin1(kPepperV1));
  }
  // Legacy plaintext until next save.
  return Result<QString>::ok(trimmed);
}

} // namespace fin
