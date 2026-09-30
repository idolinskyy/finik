#pragma once

// accounting_period: closed_through, закриття/відкриття місяців.

#include "fin/core/Result.h"

#include <QDate>
#include <QSqlDatabase>

namespace fin {

class PeriodRepository
{
 public:
  explicit PeriodRepository(QSqlDatabase db);

  Result<QDate> closedThrough() const;
  // Запис дозволено лише якщо date > closedThrough
  Status ensureDateWritable(QDate date) const;
  // Послідовність місяців + відсутність чернеток
  Status checkCloseThroughMonth(int year, int month) const;
  Status closeThroughMonth(int year, int month);
  Status reopenLastMonth();

 private:
  Result<QDate> lastDayOfMonth(int year, int month) const;

  QSqlDatabase db_;
};

} // namespace fin
