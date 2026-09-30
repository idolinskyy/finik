#include "fin/domain/BankRequisites.h"

namespace fin {
namespace {

QString digitsOnly(const QString & text)
{
  QString out;
  out.reserve(text.size());
  for (const QChar c : text) {
    if (c.isDigit()) {
      out += c;
    }
  }
  return out;
}

int mod97(const QString & numeric)
{
  int rem = 0;
  for (const QChar c : numeric) {
    rem = (rem * 10 + c.digitValue()) % 97;
  }
  return rem;
}

QString ibanToNumeric(const QString & iban)
{
  QString numeric;
  numeric.reserve(iban.size() * 2);
  for (const QChar raw : iban) {
    const QChar c = raw.toUpper();
    if (c.isDigit()) {
      numeric += c;
      continue;
    }
    if (c < QLatin1Char('A') || c > QLatin1Char('Z')) {
      continue;
    }
    const int value = c.unicode() - QLatin1Char('A').unicode() + 10;
    numeric += QLatin1Char(char('0' + value / 10));
    numeric += QLatin1Char(char('0' + value % 10));
  }
  return numeric;
}

} // namespace

QString normalizeIban(const QString & iban)
{
  QString out;
  out.reserve(iban.size());
  for (const QChar raw : iban) {
    if (raw.isSpace() || raw == QLatin1Char('-')) {
      continue;
    }
    out += raw.toUpper();
  }
  return out;
}

QString mfoFromIban(const QString & iban)
{
  const QString normalized = normalizeIban(iban);
  if (normalized.size() < 10 || !normalized.startsWith(QStringLiteral("UA"))) {
    return {};
  }
  return normalized.mid(4, 6);
}

Status validateIban(const QString & iban)
{
  const QString normalized = normalizeIban(iban);
  if (normalized.isEmpty()) {
    return Status::ok();
  }
  if (normalized.size() != 29 || !normalized.startsWith(QStringLiteral("UA"))) {
    return Status::fail(
        Error::validation(QStringLiteral("IBAN України має 29 символів і починається з UA")));
  }
  for (int i = 2; i < normalized.size(); ++i) {
    if (!normalized.at(i).isDigit()) {
      return Status::fail(
          Error::validation(QStringLiteral("після UA в IBAN мають бути лише цифри")));
    }
  }
  const QString rearranged = normalized.mid(4) + normalized.left(4);
  if (mod97(ibanToNumeric(rearranged)) != 1) {
    return Status::fail(Error::validation(QStringLiteral("контрольна сума IBAN невірна")));
  }
  return Status::ok();
}

Status validateMfo(const QString & mfo)
{
  const QString digits = digitsOnly(mfo);
  if (digits.isEmpty() && mfo.trimmed().isEmpty()) {
    return Status::ok();
  }
  if (digits.size() != 6) {
    return Status::fail(Error::validation(QStringLiteral("МФО має містити 6 цифр")));
  }
  if (digits != mfo.trimmed()) {
    return Status::fail(Error::validation(QStringLiteral("МФО має містити лише цифри")));
  }
  return Status::ok();
}

Status validateBankRequisites(const QString & iban, const QString & mfo)
{
  const Status ibanStatus = validateIban(iban);
  if (!ibanStatus) {
    return ibanStatus;
  }
  QString code = mfo.trimmed();
  if (code.isEmpty()) {
    code = mfoFromIban(iban);
  }
  const Status mfoStatus = validateMfo(code);
  if (!mfoStatus) {
    return mfoStatus;
  }
  const QString fromIban = mfoFromIban(iban);
  if (!fromIban.isEmpty() && !code.isEmpty() && fromIban != code) {
    return Status::fail(Error::validation(
        QStringLiteral("МФО не збігається з IBAN (очікується %1)").arg(fromIban)));
  }
  return Status::ok();
}

} // namespace fin
