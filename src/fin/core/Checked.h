#pragma once

// Арифметика int64 з перевіркою переповнення (Money, Quantity, ledger).

#include "fin/core/Money.h"
#include "fin/core/Result.h"

#include <limits>

namespace fin {

// Перевіряє чи спричинить додавання a + b переповнення int64.
// Використовує умову, що не вимагає ширших типів (128-bit).
inline bool addOverflows(std::int64_t a, std::int64_t b) noexcept
{
  if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) {
    return true;
  }
  if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b) {
    return true;
  }
  return false;
}

// Додає delta до accumulator з перевіркою переповнення.
// what — опис операції для повідомлення про помилку (наприклад, назва рядка ОСВ).
inline Status addTo(std::int64_t & accumulator, std::int64_t delta, const QString & what)
{
  if (addOverflows(accumulator, delta)) {
    return Status::fail(
        Error::overflow(QStringLiteral("переповнення під час розрахунку: %1").arg(what)));
  }
  accumulator += delta;
  return Status::ok();
}

// Обчислює (amount * numerator / denominator) з арифметичним округленням
// «half-up» і захистом від переповнення на кожному кроці.
// Застосовується при розрахунку середньозваженої собівартості та пропорційному
// розподілі сум між рядками.
// Усі аргументи мають бути невід'ємними, denominator — строго додатнім.
inline Result<std::int64_t> mulDivRound(std::int64_t amount, std::int64_t numerator,
                                        std::int64_t denominator)
{
  if (amount < 0 || numerator < 0 || denominator <= 0) {
    return Result<std::int64_t>::fail(
        Error::invalidArgument(QStringLiteral("некоректні аргументи пропорції")));
  }
  if (amount == 0 || numerator == 0) {
    return Result<std::int64_t>::ok(0);
  }
  if (numerator > 0 && amount > std::numeric_limits<std::int64_t>::max() / numerator) {
    return Result<std::int64_t>::fail(Error::overflow(QStringLiteral("переповнення пропорції")));
  }
  const std::int64_t product = amount * numerator;
  const std::int64_t half = denominator / 2;
  if (addOverflows(product, half)) {
    return Result<std::int64_t>::fail(Error::overflow(QStringLiteral("переповнення пропорції")));
  }
  return Result<std::int64_t>::ok((product + half) / denominator);
}

// Розкладає знакове сальдо net (у копійках) на беззнакові компоненти Дт / Кт.
// Якщо net > 0 — дебетове сальдо (актив, витрати);
// якщо net < 0 — кредитове сальдо (зобов'язання, дохід);
// якщо net = 0 — обидва дорівнюють нулю.
// Використовується в ОСВ та картці рахунку для відображення в окремих колонках.
inline Status splitSigned(std::int64_t net, Money & debit, Money & credit)
{
  if (net > 0) {
    debit = Money::fromCents(net);
    credit = Money::zero();
    return Status::ok();
  }
  if (net < 0) {
    if (net == std::numeric_limits<std::int64_t>::min()) {
      return Status::fail(Error::overflow(QStringLiteral("переповнення сальдо")));
    }
    debit = Money::zero();
    credit = Money::fromCents(-net);
    return Status::ok();
  }
  debit = Money::zero();
  credit = Money::zero();
  return Status::ok();
}

} // namespace fin
