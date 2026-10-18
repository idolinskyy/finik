#include "fin/auth/Password.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace fin {
namespace {

QByteArray hmacSha256(const QByteArray & key, const QByteArray & message)
{
  QByteArray block = key;
  if (block.size() > 64) {
    block = QCryptographicHash::hash(block, QCryptographicHash::Sha256);
  }
  if (block.size() < 64) {
    block.append(QByteArray(64 - block.size(), '\0'));
  }
  QByteArray ipad(64, static_cast<char>(0x36));
  QByteArray opad(64, static_cast<char>(0x5c));
  for (int i = 0; i < 64; ++i) {
    const auto k = static_cast<unsigned char>(block.at(i));
    ipad[i] = static_cast<char>(static_cast<unsigned char>(ipad.at(i)) ^ k);
    opad[i] = static_cast<char>(static_cast<unsigned char>(opad.at(i)) ^ k);
  }
  QCryptographicHash inner(QCryptographicHash::Sha256);
  inner.addData(ipad);
  inner.addData(message);
  QCryptographicHash outer(QCryptographicHash::Sha256);
  outer.addData(opad);
  outer.addData(inner.result());
  return outer.result();
}

} // namespace

Result<QByteArray> pbkdf2Sha256(const QString & password, const QByteArray & salt, int iterations)
{
  if (iterations < 1000) {
    return Result<QByteArray>::fail(
        Error::invalidArgument(QStringLiteral("занадто мало ітерацій пароля")));
  }
  if (salt.size() < 8) {
    return Result<QByteArray>::fail(Error::invalidArgument(QStringLiteral("коротка сіль пароля")));
  }
  const QByteArray secret = password.toUtf8();
  QByteArray block = salt;
  block.append(char(0));
  block.append(char(0));
  block.append(char(0));
  block.append(char(1));
  QByteArray u = hmacSha256(secret, block);
  QByteArray output = u;
  for (int i = 1; i < iterations; ++i) {
    u = hmacSha256(secret, u);
    for (int b = 0; b < output.size(); ++b) {
      output[b] = static_cast<char>(static_cast<unsigned char>(output.at(b)) ^
                                    static_cast<unsigned char>(u.at(b)));
    }
  }
  return Result<QByteArray>::ok(std::move(output));
}

QByteArray randomSalt(int bytes)
{
  QByteArray salt;
  salt.resize(bytes);
  auto * rng = QRandomGenerator::system();
  for (int i = 0; i < bytes; ++i) {
    salt[i] = static_cast<char>(rng->bounded(256));
  }
  return salt;
}

bool passwordHashEquals(const QByteArray & left, const QByteArray & right)
{
  if (left.size() != right.size()) {
    return false;
  }
  unsigned char diff = 0;
  for (int i = 0; i < left.size(); ++i) {
    diff = static_cast<unsigned char>(
        diff | (static_cast<unsigned char>(left.at(i)) ^ static_cast<unsigned char>(right.at(i))));
  }
  return diff == 0;
}

} // namespace fin
