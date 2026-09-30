#pragma once

// Валюта та курс (сотні копійок за одиницю).

#include "fin/core/Money.h"
#include "fin/core/Result.h"

#include <cstdint>
#include <QDate>
#include <QString>

namespace fin {

// Опис валюти
struct Currency
{
  QString code;        // 3-літерний ISO 4217 код: "USD", "EUR", "PLN", "UAH"
  int numericCode = 0; // Числовий ISO код: 840, 978, 985, 980
  QString nameUa;      // Назва українською: "Долар США", "Євро"
  QString symbol;      // Графічний символ: "$", "€", "zł", "₴"
  bool isActive = true;
};

// Запис курсу валюти на певну дату
struct CurrencyRate
{
  QString currencyCode; // "USD"
  QDate rateDate;       // Дата встановлення курсу
  int nominal = 1;      // Номінал (1 або 100)
  std::int64_t rateMicros = 0; // Курс у мікрогривнях за nominal одиниць валюти
};

// Допоміжний клас для точних валютних перерахунків без втрати копійок
class ExchangeRate
{
 public:
  static constexpr std::int64_t kMicrosPerUah = 1'000'000;
  static constexpr std::int64_t kMicrosPerCent = 10'000;

  constexpr ExchangeRate() = default;

  constexpr ExchangeRate(std::int64_t rateMicros, int nominal = 1)
      : rateMicros_(rateMicros), nominal_(nominal > 0 ? nominal : 1)
  {
  }

  static ExchangeRate fromMicros(std::int64_t micros, int nominal = 1)
  {
    return ExchangeRate(micros, nominal);
  }

  // 41.5234 грн → 41 грн + 5234 сотих
  static ExchangeRate fromUah(std::int64_t uah, std::int64_t fraction4Digits, int nominal = 1)
  {
    return ExchangeRate(uah * kMicrosPerUah + fraction4Digits * 100, nominal);
  }

  bool isValid() const { return rateMicros_ > 0 && nominal_ > 0; }
  std::int64_t rateMicros() const { return rateMicros_; }
  int nominal() const { return nominal_; }

#if defined(__GNUC__) || defined(__clang__)
  __extension__ typedef __int128 int128_t;
#else
  // Fallback type if __int128 is unsupported on non-GNU compilers.
  // For now, this struct is a stub since this project targets GCC/Clang.
  struct int128_t
  {
    int64_t v; /* ... implement proper 128-bit math here if MSVC is needed ... */
  };
#endif

  // Перерахунок суми в іноземній валюті в гривні за курсом:
  // uahCents = (foreignCents * rateMicros + halfScale) / (nominal * kMicrosPerUah)
  Result<Money> toUah(Money foreignAmount) const
  {
    if (!isValid()) {
      return Result<Money>::fail(
          Error::invalidArgument(QStringLiteral("некоректний валютний курс")));
    }
    const int128_t fc = foreignAmount.cents();
    const int128_t rm = rateMicros_;
    const int128_t denom = static_cast<int128_t>(nominal_) * kMicrosPerUah;
    const int128_t half = denom / 2;

    const int128_t prod = fc * rm;
    const int128_t res = (prod >= 0) ? (prod + half) / denom : (prod - half) / denom;

    if (res > INT64_MAX || res < INT64_MIN) {
      return Result<Money>::fail(
          Error::invalidArgument(QStringLiteral("переповнення при перерахунку у гривню")));
    }
    return Result<Money>::ok(Money::fromCents(static_cast<std::int64_t>(res)));
  }

  // Перерахунок суми в гривнях в іноземну валюту за курсом:
  // foreignCents = (uahCents * nominal * kMicrosPerUah + halfRate) / rateMicros
  Result<Money> toForeign(Money uahAmount) const
  {
    if (!isValid()) {
      return Result<Money>::fail(
          Error::invalidArgument(QStringLiteral("некоректний валютний курс")));
    }
    const int128_t uc = uahAmount.cents();
    const int128_t num = uc * static_cast<int128_t>(nominal_) * kMicrosPerUah;
    const int128_t half = static_cast<int128_t>(rateMicros_) / 2;

    const int128_t res = (num >= 0) ? (num + half) / rateMicros_ : (num - half) / rateMicros_;

    if (res > INT64_MAX || res < INT64_MIN) {
      return Result<Money>::fail(
          Error::invalidArgument(QStringLiteral("переповнення при перерахунку у валюту")));
    }
    return Result<Money>::ok(Money::fromCents(static_cast<std::int64_t>(res)));
  }

  // Форматоване представлення курсу, наприклад "41.5234 грн / 1 USD"
  QString formatRate(const QString & currencyCode = QStringLiteral("USD")) const
  {
    if (!isValid()) {
      return QStringLiteral("—");
    }
    const std::int64_t intPart = rateMicros_ / kMicrosPerUah;
    const std::int64_t fracPart = (rateMicros_ % kMicrosPerUah) / 100; // 4 знаки
    const QString fracStr = QStringLiteral("%1").arg(fracPart, 4, 10, QLatin1Char('0'));
    if (nominal_ == 1) {
      return QStringLiteral("%1.%2 грн / 1 %3")
          .arg(QString::number(intPart), fracStr, currencyCode);
    }
    return QStringLiteral("%1.%2 грн / %3 %4")
        .arg(QString::number(intPart), fracStr, QString::number(nominal_), currencyCode);
  }

 private:
  std::int64_t rateMicros_ = 0;
  int nominal_ = 1;
};

} // namespace fin
