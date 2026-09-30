#include "fin/edo/PayrollTaxDigest.h"

#include "fin/db/AccountRepository.h"
#include "fin/db/DocumentRepository.h"
#include "fin/db/EmployeeRepository.h"
#include "fin/domain/Document.h"
#include "fin/domain/Employee.h"

#include <QHash>

namespace fin {
namespace {

void addMoney(Money & dst, Money src)
{
  dst = Money::fromCents(dst.cents() + src.cents());
}

Result<ConsolidatedTaxReport> buildPayrollTaxReport(QSqlDatabase db, QDate from, QDate to, int year,
                                                    int quarter, int month)
{
  ConsolidatedTaxReport report;
  report.year = year;
  report.quarter = quarter;
  report.month = month;
  report.from = from;
  report.to = to;

  AccountRepository accountsRepo(db);
  Result<std::vector<Account>> accounts = accountsRepo.listPostable();
  if (!accounts) {
    return Result<ConsolidatedTaxReport>::fail(accounts.error());
  }

  DocumentRepository docs(db);
  const DocumentType types[] = {DocumentType::Payroll, DocumentType::VacationPayroll,
                                DocumentType::SickLeavePayroll};

  QHash<qlonglong, EmployeeTaxRow> byEmp;
  QList<qlonglong> order;

  for (DocumentType type : types) {
    Result<std::vector<DocumentHeader>> list =
        docs.list(report.from, report.to, type, CounterpartyId{});
    if (!list) {
      return Result<ConsolidatedTaxReport>::fail(list.error());
    }
    for (const DocumentHeader & header : list.value()) {
      if (!header.posted) {
        continue;
      }
      Result<Document> full = docs.findById(header.id);
      if (!full) {
        return Result<ConsolidatedTaxReport>::fail(full.error());
      }
      const std::vector<PayrollLine> lines =
          extractPayrollLines(full.value().lines, accounts.value());
      Result<bool> paid = hasPostedPayrollPayment(db, header.id);
      if (!paid) {
        return Result<ConsolidatedTaxReport>::fail(paid.error());
      }
      const bool isPaid = paid.value();
      for (const PayrollLine & line : lines) {
        if (!line.employeeId) {
          continue;
        }
        const qlonglong key = line.employeeId.value;
        if (!byEmp.contains(key)) {
          EmployeeTaxRow row;
          row.employeeId = line.employeeId;
          byEmp.insert(key, row);
          order.push_back(key);
        }
        EmployeeTaxRow & row = byEmp[key];
        addMoney(row.gross, line.gross);
        addMoney(row.pit, line.pit);
        addMoney(row.military, line.military);
        addMoney(row.ssc, line.ssc);
        if (isPaid) {
          addMoney(row.paidGross, line.gross);
          addMoney(row.paidPit, line.pit);
          addMoney(row.paidMilitary, line.military);
        }
      }
    }
  }

  EmployeeRepository employees(db);
  for (qlonglong key : order) {
    EmployeeTaxRow row = byEmp.value(key);
    Result<Employee> emp = employees.findById(EmployeeId{key});
    if (emp) {
      row.taxId = emp.value().taxId;
      row.name = emp.value().name;
      row.hiredOn = emp.value().hiredOn;
      row.firedOn = emp.value().firedOn;
      row.hasPitBenefit = emp.value().hasPitBenefit;
      row.disabledSsc = emp.value().disabledSsc;
    }
    if (row.name.isEmpty()) {
      row.name = QStringLiteral("Працівник %1").arg(key);
    }
    addMoney(report.totalGross, row.gross);
    addMoney(report.totalPit, row.pit);
    addMoney(report.totalMilitary, row.military);
    addMoney(report.totalSsc, row.ssc);
    addMoney(report.totalPaidGross, row.paidGross);
    addMoney(report.totalPaidPit, row.paidPit);
    addMoney(report.totalPaidMilitary, row.paidMilitary);
    report.employees.push_back(row);
  }
  report.employeeCount = static_cast<int>(report.employees.size());
  return Result<ConsolidatedTaxReport>::ok(std::move(report));
}

} // namespace

Result<ConsolidatedTaxReport> buildConsolidatedTaxReport(QSqlDatabase db, int year, int quarter)
{
  if (year < 2000 || year > 2100 || quarter < 1 || quarter > 4) {
    return Result<ConsolidatedTaxReport>::fail(
        Error::invalidArgument(QStringLiteral("некоректний рік/квартал")));
  }
  const QDate from(year, quarter * 3 - 2, 1);
  const QDate to(year, quarter * 3, QDate(year, quarter * 3, 1).daysInMonth());
  return buildPayrollTaxReport(db, from, to, year, quarter, 0);
}

Result<ConsolidatedTaxReport> buildMonthlyPayrollTaxReport(QSqlDatabase db, int year, int month)
{
  if (year < 2000 || year > 2100 || month < 1 || month > 12) {
    return Result<ConsolidatedTaxReport>::fail(
        Error::invalidArgument(QStringLiteral("некоректний рік/місяць")));
  }
  const QDate from(year, month, 1);
  const QDate to(year, month, from.daysInMonth());
  const int quarter = (month - 1) / 3 + 1;
  return buildPayrollTaxReport(db, from, to, year, quarter, month);
}

} // namespace fin
