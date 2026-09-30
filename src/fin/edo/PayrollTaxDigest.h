#pragma once

// Зведення зарплатних нарахувань і виплат за період для об'єднаної звітності (1-ДФ / 4ДФ / Д1).

#include "fin/core/Ids.h"
#include "fin/core/Money.h"
#include "fin/core/Result.h"

#include <vector>

#include <QDate>
#include <QSqlDatabase>
#include <QString>

namespace fin {

struct EmployeeTaxRow
{
  EmployeeId employeeId;
  QString taxId; // РНОКПП
  QString name;
  Money gross = Money::zero();
  Money pit = Money::zero();
  Money military = Money::zero();
  Money ssc = Money::zero();
  // Виплачено: заповнюється з BankPayment/CashOut «на підставі» payroll у періоді.
  Money paidGross = Money::zero();
  Money paidPit = Money::zero();
  Money paidMilitary = Money::zero();
  QDate hiredOn;
  QDate firedOn;
  bool hasPitBenefit = false;
  bool disabledSsc = false;
  // Ознака доходу 4ДФ: 101 — заробітна плата за трудовим договором
  int incomeSign = 101;
};

struct ConsolidatedTaxReport
{
  int year = 0;
  int quarter = 0;
  int month = 0; // 0 = увесь квартал; 1..12 для місячного Д1
  QDate from;
  QDate to;
  int employeeCount = 0;
  Money totalGross = Money::zero();
  Money totalPit = Money::zero();
  Money totalMilitary = Money::zero();
  Money totalSsc = Money::zero();
  Money totalPaidGross = Money::zero();
  Money totalPaidPit = Money::zero();
  Money totalPaidMilitary = Money::zero();
  std::vector<EmployeeTaxRow> employees;
};

Result<ConsolidatedTaxReport> buildConsolidatedTaxReport(QSqlDatabase db, int year, int quarter);

// Місячний зріз для додатка Д1 (ЄСВ).
Result<ConsolidatedTaxReport> buildMonthlyPayrollTaxReport(QSqlDatabase db, int year, int month);

} // namespace fin
