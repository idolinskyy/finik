#pragma once

// Табель обліку робочого часу.

#include "fin/core/Result.h"
#include "fin/domain/Employee.h"

#include <utility>
#include <vector>

#include <QSqlDatabase>

namespace fin {

class TimesheetRepository
{
 public:
  explicit TimesheetRepository(QSqlDatabase db);

  // Повертає табель за місяць (усі активні працівники, можуть бути рядки з нулями)
  Result<Timesheet> findOrCreate(int year, int month);

  // Зберігає або оновлює рядок табеля одного працівника
  Status upsertRow(int year, int month, const TimesheetRow & row);

  // Повертає один рядок табеля для конкретного працівника у місяці
  Result<TimesheetRow> findRow(EmployeeId empId, int year, int month) const;

  // Повертає суму нарахувань (Дт 92 Кт 661) за кожен з останніх 12 місяців
  // до calcDate (включно з місяцем, що передує calcDate), для розрахунку
  // середнього заробітку за КМУ № 100.
  Result<std::vector<std::pair<QDate, Money>>> last12MonthsEarnings(EmployeeId empId,
                                                                    QDate calcDate) const;

 private:
  QSqlDatabase db_;
};

} // namespace fin
