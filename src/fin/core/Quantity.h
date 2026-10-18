#pragma once

// Кількість у міліодиницях (1 од. = 1000). Без float; переповнення → Result.

#include "fin/core/Money.h"
#include "fin/core/Result.h"

#include <cstdint>

#include <QString>
#include <QStringView>

namespace fin {

class Quantity
{
 public:
  static constexpr int kScale = 1000;

  static constexpr Quantity zero() noexcept { return Quantity{0}; }

  // Фабричний метод для створення з сирих міліодиниць (для репозиторіїв та тестів).
  static constexpr Quantity fromMilli(std::int64_t milli) noexcept { return Quantity{milli}; }

  // Парсинг рядка кількості користувача з підтримкою коми/крапки (до 3 знаків).
  static Result<Quantity> fromDisplayString(QStringView text);

  constexpr std::int64_t milli() const noexcept { return milli_; }
  constexpr bool isZero() const noexcept { return milli_ == 0; }
  constexpr bool isPositive() const noexcept { return milli_ > 0; }
  constexpr bool isNegative() const noexcept { return milli_ < 0; }

  Result<Quantity> negated() const;
  Result<Quantity> plus(Quantity other) const;

  // Розрахунок вартості (кількість × ціна) з арифметичним округленням до копійок.
  Result<Money> times(Money unitPrice) const;

  // Форматування для користувацького інтерфейсу (без зайвих нулів у хвості).
  QString toDisplay() const;

  friend constexpr bool operator==(Quantity a, Quantity b) noexcept { return a.milli_ == b.milli_; }
  friend constexpr bool operator!=(Quantity a, Quantity b) noexcept { return !(a == b); }

 private:
  explicit constexpr Quantity(std::int64_t milli) noexcept : milli_(milli) {}

  std::int64_t milli_ = 0;
};

} // namespace fin
