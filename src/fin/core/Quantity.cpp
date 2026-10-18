#include "fin/core/Quantity.h"

#include "fin/core/Checked.h"

#include <algorithm>
#include <limits>

#include <QChar>

namespace fin {

Result<Quantity> Quantity::fromDisplayString(QStringView text)
{
  QString s = text.toString().trimmed();
  s.replace(QChar::Nbsp, QLatin1Char(' '));
  s.remove(QLatin1Char(' '));
  if (s.isEmpty()) {
    return Result<Quantity>::ok(Quantity::zero());
  }

  s.replace(QLatin1Char(','), QLatin1Char('.'));
  int sign = 1;
  if (s.startsWith(QLatin1Char('+'))) {
    s.remove(0, 1);
  } else if (s.startsWith(QLatin1Char('-'))) {
    sign = -1;
    s.remove(0, 1);
  }
  if (s.isEmpty()) {
    return Result<Quantity>::fail(Error::invalidArgument(QStringLiteral("некоректна кількість")));
  }

  const qsizetype dot = s.indexOf(QLatin1Char('.'));
  QString whole;
  QString frac;
  if (dot < 0) {
    whole = s;
  } else {
    whole = s.left(dot);
    frac = s.mid(dot + 1);
    if (frac.contains(QLatin1Char('.'))) {
      return Result<Quantity>::fail(
          Error::invalidArgument(QStringLiteral("зайва десяткова частина")));
    }
    if (frac.size() > 3) {
      return Result<Quantity>::fail(Error::invalidArgument(
          QStringLiteral("кількість має містити не більше трьох знаків після коми")));
    }
  }

  if (whole.isEmpty() ||
      !std::all_of(whole.cbegin(), whole.cend(), [](QChar c) { return c.isDigit(); })) {
    return Result<Quantity>::fail(Error::invalidArgument(QStringLiteral("некоректна кількість")));
  }
  if (!frac.isEmpty() &&
      !std::all_of(frac.cbegin(), frac.cend(), [](QChar c) { return c.isDigit(); })) {
    return Result<Quantity>::fail(Error::invalidArgument(QStringLiteral("некоректна кількість")));
  }

  bool ok = false;
  const qlonglong major = whole.toLongLong(&ok);
  if (!ok) {
    return Result<Quantity>::fail(Error::overflow(QStringLiteral("кількість занадто велика")));
  }
  while (frac.size() < 3) {
    frac += QLatin1Char('0');
  }
  const int minor = frac.isEmpty() ? 0 : frac.toInt(&ok);
  if (!ok) {
    return Result<Quantity>::fail(Error::invalidArgument(QStringLiteral("некоректна кількість")));
  }
  if (major > std::numeric_limits<std::int64_t>::max() / kScale) {
    return Result<Quantity>::fail(Error::overflow(QStringLiteral("кількість занадто велика")));
  }
  std::int64_t milli = major * kScale + minor;
  if (sign < 0) {
    milli = -milli;
  }
  return Result<Quantity>::ok(Quantity::fromMilli(milli));
}

Result<Quantity> Quantity::negated() const
{
  if (milli_ == std::numeric_limits<std::int64_t>::min()) {
    return Result<Quantity>::fail(Error::overflow(QStringLiteral("переповнення кількості")));
  }
  return Result<Quantity>::ok(Quantity::fromMilli(-milli_));
}

Result<Quantity> Quantity::plus(Quantity other) const
{
  if (addOverflows(milli_, other.milli_)) {
    return Result<Quantity>::fail(Error::overflow(QStringLiteral("переповнення кількості")));
  }
  return Result<Quantity>::ok(Quantity::fromMilli(milli_ + other.milli_));
}

Result<Money> Quantity::times(Money unitPrice) const
{
  if (milli_ < 0 || unitPrice.isNegative()) {
    return Result<Money>::fail(Error::invalidArgument(
        QStringLiteral("ціна і кількість для множення мають бути невід'ємні")));
  }
  if (milli_ == 0 || unitPrice.isZero()) {
    return Result<Money>::ok(Money::zero());
  }
  const std::int64_t price = unitPrice.cents();
  if (price > 0 && milli_ > std::numeric_limits<std::int64_t>::max() / price) {
    return Result<Money>::fail(Error::overflow(QStringLiteral("переповнення суми")));
  }
  const std::int64_t prod = milli_ * price;
  if (prod > std::numeric_limits<std::int64_t>::max() - 500) {
    return Result<Money>::fail(Error::overflow(QStringLiteral("переповнення суми")));
  }
  return Result<Money>::ok(Money::fromCents((prod + 500) / kScale));
}

QString Quantity::toDisplay() const
{
  const bool negative = milli_ < 0;
  std::int64_t abs = milli_;
  if (negative) {
    if (milli_ == std::numeric_limits<std::int64_t>::min()) {
      return QStringLiteral("-9223372036854775.808");
    }
    abs = -milli_;
  }
  const std::int64_t whole = abs / kScale;
  int frac = static_cast<int>(abs % kScale);
  QString result = QString::number(whole);
  if (frac != 0) {
    QString decimals = QStringLiteral("%1").arg(frac, 3, 10, QLatin1Char('0'));
    while (decimals.endsWith(QLatin1Char('0'))) {
      decimals.chop(1);
    }
    result += QLatin1Char('.');
    result += decimals;
  }
  if (negative) {
    result.prepend(QLatin1Char('-'));
  }
  return result;
}

} // namespace fin
