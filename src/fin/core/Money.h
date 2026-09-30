#pragma once

// Сума в копійках (int64). Без float/double — щоб не псувати залишки в книзі.
// Арифметика повертає Result<Money> при переповненні.

#include "fin/core/Result.h"

#include <cstdint>

#include <QString>
#include <QStringView>

namespace fin {

class Money
{
 public:
  static constexpr Money zero() noexcept { return Money{0}; }

  // Єдиний «сирий» конструктор з копійок — для використання у репозиторіях
  // при читанні значень з БД та в юніт-тестах.
  static constexpr Money fromCents(std::int64_t cents) noexcept { return Money{cents}; }

  // Розбирає рядок у форматі «1 234,56» або «1234.56».
  // Повертає помилку при наявності більше двох знаків після коми або
  // при перевищенні діапазону int64.
  static Result<Money> fromDisplayString(QStringView text);

  constexpr std::int64_t cents() const noexcept { return cents_; }
  constexpr bool isZero() const noexcept { return cents_ == 0; }
  constexpr bool isNegative() const noexcept { return cents_ < 0; }
  constexpr bool isPositive() const noexcept { return cents_ > 0; }

  Result<Money> negated() const;
  Result<Money> plus(Money other) const;
  // minus реалізовано через plus(negated(other)) — щоб не дублювати
  // перевірку переповнення.
  Result<Money> minus(Money other) const;

  // Форматує суму з роздільником тисяч (нерозривний пробіл) та двома
  // знаками після коми: «1 234.56» або «-1 234.56».
  QString toDisplay() const;

  friend constexpr bool operator==(Money a, Money b) noexcept { return a.cents_ == b.cents_; }
  friend constexpr bool operator!=(Money a, Money b) noexcept { return !(a == b); }
  friend constexpr bool operator<(Money a, Money b) noexcept { return a.cents_ < b.cents_; }

 private:
  explicit constexpr Money(std::int64_t cents) noexcept : cents_(cents) {}

  std::int64_t cents_ = 0;
};

} // namespace fin
