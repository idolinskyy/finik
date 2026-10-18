#include "fin/db/TimesheetRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"

#include <QDate>
#include <QVariant>

namespace fin {
namespace {

TimesheetRow rowFromQuery(sql::Query & q)
{
  TimesheetRow row;
  row.employeeId = EmployeeId{q.value(0).toLongLong()};
  row.employeeName = q.value(1).toString();
  row.daysWorked = q.value(2).toInt();
  row.hoursWorked = q.value(3).toInt();
  row.vacationDays = q.value(4).toInt();
  row.sickDays = q.value(5).toInt();
  row.absenceDays = q.value(6).toInt();
  row.workingDaysNorm = q.value(7).toInt();
  return row;
}

} // namespace

TimesheetRepository::TimesheetRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<Timesheet> TimesheetRepository::findOrCreate(int year, int month)
{
  if (month < 1 || month > 12 || year < 2000) {
    return Result<Timesheet>::fail(
        Error::validation(QStringLiteral("некоректний рік або місяць табеля")));
  }

  // Вибираємо всіх активних працівників з їхніми рядками табеля (LEFT JOIN)
  sql::Query q(db_);
  Status status = q.prepare(QStringLiteral("SELECT e.id, e.name, "
                                           "  COALESCE(t.days_worked, 0), "
                                           "  COALESCE(t.hours_worked, 0), "
                                           "  COALESCE(t.vacation_days, 0), "
                                           "  COALESCE(t.sick_days, 0), "
                                           "  COALESCE(t.absence_days, 0), "
                                           "  COALESCE(t.working_days_norm, :norm) "
                                           "FROM employees e "
                                           "LEFT JOIN timesheets t ON t.employee_id = e.id "
                                           "  AND t.year = :y AND t.month = :m "
                                           "WHERE e.is_active = 1 "
                                           "ORDER BY e.name COLLATE NOCASE"));
  if (!status) {
    return Result<Timesheet>::fail(status.error());
  }
  q.bind(QStringLiteral(":y"), year);
  q.bind(QStringLiteral(":m"), month);
  q.bind(QStringLiteral(":norm"), workingDaysInMonth(year, month));
  status = q.exec();
  if (!status) {
    return Result<Timesheet>::fail(status.error());
  }

  Timesheet ts;
  ts.year = year;
  ts.month = month;
  while (q.next()) {
    ts.rows.push_back(rowFromQuery(q));
  }
  return Result<Timesheet>::ok(std::move(ts));
}

Status TimesheetRepository::upsertRow(int year, int month, const TimesheetRow & row)
{
  const Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!row.employeeId) {
    return Status::fail(Error::invalidArgument(QStringLiteral("табель: порожній employeeId")));
  }
  if (month < 1 || month > 12 || year < 2000) {
    return Status::fail(Error::validation(QStringLiteral("некоректний рік або місяць табеля")));
  }
  if (row.daysWorked < 0 || row.hoursWorked < 0 || row.vacationDays < 0 || row.sickDays < 0 ||
      row.absenceDays < 0) {
    return Status::fail(Error::validation(QStringLiteral("дні табеля не можуть бути від'ємними")));
  }

  sql::Query q(db_);
  Status status =
      q.prepare(QStringLiteral("INSERT INTO timesheets"
                               "(employee_id, year, month, days_worked, hours_worked, "
                               " vacation_days, sick_days, absence_days, working_days_norm) "
                               "VALUES(:emp, :y, :m, :dw, :hw, :vd, :sd, :ad, :norm) "
                               "ON CONFLICT(employee_id, year, month) DO UPDATE SET "
                               "  days_worked = excluded.days_worked, "
                               "  hours_worked = excluded.hours_worked, "
                               "  vacation_days = excluded.vacation_days, "
                               "  sick_days = excluded.sick_days, "
                               "  absence_days = excluded.absence_days, "
                               "  working_days_norm = excluded.working_days_norm"));
  if (!status) {
    return status;
  }
  q.bind(QStringLiteral(":emp"), static_cast<qlonglong>(row.employeeId.value));
  q.bind(QStringLiteral(":y"), year);
  q.bind(QStringLiteral(":m"), month);
  q.bind(QStringLiteral(":dw"), row.daysWorked);
  q.bind(QStringLiteral(":hw"), row.hoursWorked);
  q.bind(QStringLiteral(":vd"), row.vacationDays);
  q.bind(QStringLiteral(":sd"), row.sickDays);
  q.bind(QStringLiteral(":ad"), row.absenceDays);
  q.bind(QStringLiteral(":norm"),
         row.workingDaysNorm > 0 ? row.workingDaysNorm : workingDaysInMonth(year, month));
  return q.exec();
}

Result<TimesheetRow> TimesheetRepository::findRow(EmployeeId empId, int year, int month) const
{
  if (!empId) {
    return Result<TimesheetRow>::fail(
        Error::invalidArgument(QStringLiteral("порожній employeeId")));
  }
  sql::Query q(db_);
  Status status =
      q.prepare(QStringLiteral("SELECT t.employee_id, e.name, "
                               "  t.days_worked, t.hours_worked, t.vacation_days, "
                               "  t.sick_days, t.absence_days, t.working_days_norm "
                               "FROM timesheets t "
                               "JOIN employees e ON e.id = t.employee_id "
                               "WHERE t.employee_id = :emp AND t.year = :y AND t.month = :m"));
  if (!status) {
    return Result<TimesheetRow>::fail(status.error());
  }
  q.bind(QStringLiteral(":emp"), static_cast<qlonglong>(empId.value));
  q.bind(QStringLiteral(":y"), year);
  q.bind(QStringLiteral(":m"), month);
  status = q.exec();
  if (!status) {
    return Result<TimesheetRow>::fail(status.error());
  }
  if (!q.next()) {
    return Result<TimesheetRow>::fail(Error::notFound(QStringLiteral("рядок табеля не знайдено")));
  }
  return Result<TimesheetRow>::ok(rowFromQuery(q));
}

Result<std::vector<std::pair<QDate, Money>>>
TimesheetRepository::last12MonthsEarnings(EmployeeId empId, QDate calcDate) const
{
  if (!empId) {
    return Result<std::vector<std::pair<QDate, Money>>>::fail(
        Error::invalidArgument(QStringLiteral("порожній employeeId")));
  }

  // Розрахунковий 12-місячний діапазон: від місяця, що передує calcDate, назад на 12.
  // Відповідно до КМУ № 100 п. 2: береться 12 місяців, що передують місяцю нарахування.
  const QDate periodEnd = QDate(calcDate.year(), calcDate.month(), 1).addDays(-1);
  const QDate periodFirst = QDate(periodEnd.year(), periodEnd.month(), 1).addMonths(-11);

  sql::Query q(db_);
  Status status = q.prepare(
      QStringLiteral("SELECT d.date, SUM(dl.amount_cents) "
                     "FROM document_lines dl "
                     "JOIN documents d ON d.id = dl.document_id "
                     "JOIN accounts da ON da.id = dl.debit_account_id "
                     "JOIN accounts ca ON ca.id = dl.credit_account_id "
                     "WHERE dl.employee_id = :eid "
                     "  AND d.posted = 1 "
                     "  AND d.date >= :from AND d.date <= :to "
                     "  AND da.code = '92' AND ca.code = '661' "
                     "  AND d.type IN ('payroll','vacation_payroll','sick_leave_payroll') "
                     "GROUP BY substr(d.date, 1, 7) "
                     "ORDER BY d.date"));
  if (!status) {
    return Result<std::vector<std::pair<QDate, Money>>>::fail(status.error());
  }
  q.bind(QStringLiteral(":eid"), static_cast<qlonglong>(empId.value));
  q.bind(QStringLiteral(":from"), periodFirst.toString(Qt::ISODate));
  q.bind(QStringLiteral(":to"), periodEnd.toString(Qt::ISODate));
  status = q.exec();
  if (!status) {
    return Result<std::vector<std::pair<QDate, Money>>>::fail(status.error());
  }

  std::vector<std::pair<QDate, Money>> result;
  while (q.next()) {
    const QDate date = QDate::fromString(q.value(0).toString(), Qt::ISODate);
    const Money amount = Money::fromCents(q.value(1).toLongLong());
    result.emplace_back(date, amount);
  }
  return Result<std::vector<std::pair<QDate, Money>>>::ok(std::move(result));
}

} // namespace fin
