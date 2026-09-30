#include "fin/core/Money.h"

#include "fin/core/Checked.h"

#include <algorithm>
#include <limits>

#include <QChar>

namespace fin {
namespace {

QString groupThousands(std::int64_t major)
{
  const bool negative = major < 0;
  std::int64_t absMajor = major;
  if (negative) {
    if (major == std::numeric_limits<std::int64_t>::min()) {
      absMajor = 0; // unreachable for grouped hryvnia from cents
    } else {
      absMajor = -major;
    }
  }

  QString digits = QString::number(absMajor);
  QString grouped;
  grouped.reserve(digits.size() + digits.size() / 3 + 1);
  const qsizetype n = digits.size();
  for (qsizetype i = 0; i < n; ++i) {
    if (i > 0 && (n - i) % 3 == 0) {
      grouped += QChar::Nbsp;
    }
    grouped += digits[i];
  }
  if (negative) {
    return QLatin1Char('-') + grouped;
  }
  return grouped;
}

} // namespace

Result<Money> Money::fromDisplayString(QStringView text)
{
  QString s = text.toString().trimmed();
  s.replace(QChar::Nbsp, QLatin1Char(' '));
  s.remove(QLatin1Char(' '));
  if (s.isEmpty()) {
    return Result<Money>::fail(Error::invalidArgument(QStringLiteral("порожня сума")));
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
    return Result<Money>::fail(Error::invalidArgument(QStringLiteral("некоректна сума")));
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
      return Result<Money>::fail(Error::invalidArgument(QStringLiteral("зайва десяткова частина")));
    }
    if (frac.size() > 2) {
      return Result<Money>::fail(Error::invalidArgument(
          QStringLiteral("сума має містити не більше двох знаків після коми")));
    }
  }

  if (whole.isEmpty() ||
      !std::all_of(whole.cbegin(), whole.cend(), [](QChar c) { return c.isDigit(); })) {
    return Result<Money>::fail(Error::invalidArgument(QStringLiteral("некоректна сума")));
  }
  if (!frac.isEmpty() &&
      !std::all_of(frac.cbegin(), frac.cend(), [](QChar c) { return c.isDigit(); })) {
    return Result<Money>::fail(Error::invalidArgument(QStringLiteral("некоректна сума")));
  }

  bool ok = false;
  const qlonglong major = whole.toLongLong(&ok);
  if (!ok) {
    return Result<Money>::fail(Error::overflow(QStringLiteral("сума занадто велика")));
  }
  if (frac.size() == 1) {
    frac += QLatin1Char('0');
  }
  const int minor = frac.isEmpty() ? 0 : frac.toInt(&ok);
  if (!ok) {
    return Result<Money>::fail(Error::invalidArgument(QStringLiteral("некоректна сума")));
  }

  if (major > std::numeric_limits<std::int64_t>::max() / 100) {
    return Result<Money>::fail(Error::overflow(QStringLiteral("сума занадто велика")));
  }

  std::int64_t cents = major * 100 + minor;
  if (sign < 0) {
    cents = -cents;
  }
  return Result<Money>::ok(Money::fromCents(cents));
}

Result<Money> Money::negated() const
{
  if (cents_ == std::numeric_limits<std::int64_t>::min()) {
    return Result<Money>::fail(Error::overflow(QStringLiteral("переповнення суми")));
  }
  return Result<Money>::ok(Money::fromCents(-cents_));
}

Result<Money> Money::plus(Money other) const
{
  if (addOverflows(cents_, other.cents_)) {
    return Result<Money>::fail(Error::overflow(QStringLiteral("переповнення суми")));
  }
  return Result<Money>::ok(Money::fromCents(cents_ + other.cents_));
}

Result<Money> Money::minus(Money other) const
{
  const Result<Money> negatedOther = other.negated();
  if (!negatedOther) {
    return negatedOther;
  }
  return plus(negatedOther.value());
}

QString Money::toDisplay() const
{
  const bool negative = cents_ < 0;
  std::int64_t absCents = cents_;
  if (negative) {
    if (cents_ == std::numeric_limits<std::int64_t>::min()) {
      return QStringLiteral("-92233720368547758.08");
    }
    absCents = -cents_;
  }

  const std::int64_t major = absCents / 100;
  const int minor = static_cast<int>(absCents % 100);
  QString result = groupThousands(major);
  result += QLatin1Char('.');
  result += QStringLiteral("%1").arg(minor, 2, 10, QLatin1Char('0'));
  if (negative) {
    result.prepend(QLatin1Char('-'));
  }
  return result;
}

} // namespace fin
