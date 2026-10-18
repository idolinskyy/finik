#include "fin/db/EmployeeRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"

#include <QDate>
#include <QLatin1String>
#include <QRegularExpression>
#include <QVariant>

namespace fin {
namespace {

QString iso(QDate date)
{
  return date.toString(Qt::ISODate);
}

Status validateEmployee(const Employee & employee)
{
  const QString taxId = employee.taxId.trimmed();
  static const QRegularExpression kDigits(QStringLiteral("^\\d{10}$"));
  if (!kDigits.match(taxId).hasMatch()) {
    return Status::fail(Error::validation(QStringLiteral("РНОКПП має містити рівно 10 цифр")));
  }
  if (employee.name.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("ім'я працівника обов'язкове")));
  }
  if (!employee.hiredOn.isValid()) {
    return Status::fail(Error::validation(QStringLiteral("дата прийому обов'язкова")));
  }
  if (!employee.salary.isPositive()) {
    return Status::fail(Error::validation(QStringLiteral("оклад має бути більшим за нуль")));
  }
  return Status::ok();
}

Employee fromQuery(sql::Query & query)
{
  Employee row;
  row.id = EmployeeId{query.value(0).toLongLong()};
  row.taxId = query.value(1).toString();
  row.name = query.value(2).toString();
  row.hiredOn = QDate::fromString(query.value(3).toString(), Qt::ISODate);
  row.salary = Money::fromCents(query.value(4).toLongLong());
  row.disabledSsc = query.value(5).toInt() != 0;
  row.isActive = query.value(6).toInt() != 0;
  row.position = query.value(7).toString();
  row.department = query.value(8).toString();
  const QString firedStr = query.value(9).toString();
  if (!firedStr.isEmpty()) {
    row.firedOn = QDate::fromString(firedStr, Qt::ISODate);
  }
  row.hasPitBenefit = query.value(10).toInt() != 0;
  return row;
}

const QString kSelect =
    QStringLiteral("SELECT id, tax_id, name, hired_on, salary_cents, disabled_ssc, is_active, "
                   "position, department, fired_on, has_pit_benefit "
                   "FROM employees");

} // namespace

EmployeeRepository::EmployeeRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<std::vector<Employee>> EmployeeRepository::listWhere(const QString & where) const
{
  sql::Query query(db_);
  Status status = query.exec(kSelect + where);
  if (!status) {
    return Result<std::vector<Employee>>::fail(status.error());
  }
  std::vector<Employee> rows;
  while (query.next()) {
    rows.push_back(fromQuery(query));
  }
  return Result<std::vector<Employee>>::ok(std::move(rows));
}

Result<Employee> EmployeeRepository::findById(EmployeeId id) const
{
  if (!id) {
    return Result<Employee>::fail(Error::invalidArgument(QStringLiteral("порожній id працівника")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<Employee>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<Employee>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Employee>::fail(Error::notFound(QStringLiteral("працівника не знайдено")));
  }
  return Result<Employee>::ok(fromQuery(query));
}

Result<Employee> EmployeeRepository::findByTaxId(const QString & taxId) const
{
  const QString key = taxId.trimmed();
  if (key.isEmpty()) {
    return Result<Employee>::fail(Error::validation(QStringLiteral("РНОКПП обов'язковий")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE tax_id = :tin"));
  if (!status) {
    return Result<Employee>::fail(status.error());
  }
  query.bind(QStringLiteral(":tin"), key);
  status = query.exec();
  if (!status) {
    return Result<Employee>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Employee>::fail(
        Error::notFound(QStringLiteral("працівника з РНОКПП %1 не знайдено").arg(key)));
  }
  return Result<Employee>::ok(fromQuery(query));
}

Result<std::vector<Employee>> EmployeeRepository::listAll() const
{
  return listWhere(QStringLiteral(" ORDER BY name COLLATE NOCASE"));
}

Result<std::vector<Employee>> EmployeeRepository::listActive() const
{
  return listWhere(QStringLiteral(" WHERE is_active = 1 ORDER BY name COLLATE NOCASE"));
}

Result<EmployeeId> EmployeeRepository::insert(const Employee & employee)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<EmployeeId>::fail(access.error());
  }
  const Status valid = validateEmployee(employee);
  if (!valid) {
    return Result<EmployeeId>::fail(valid.error());
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral(
      "INSERT INTO employees(tax_id, name, hired_on, salary_cents, disabled_ssc, "
      "is_active, position, department, fired_on, has_pit_benefit) "
      "VALUES(:tin, :name, :hired, :sal, :dis, :active, :pos, :dept, :fired, :pit)"));
  if (!status) {
    return Result<EmployeeId>::fail(status.error());
  }
  query.bind(QStringLiteral(":tin"), employee.taxId.trimmed());
  query.bind(QStringLiteral(":name"), employee.name.trimmed());
  query.bind(QStringLiteral(":hired"), iso(employee.hiredOn));
  query.bind(QStringLiteral(":sal"), static_cast<qlonglong>(employee.salary.cents()));
  query.bind(QStringLiteral(":dis"), employee.disabledSsc ? 1 : 0);
  query.bind(QStringLiteral(":active"), employee.isActive ? 1 : 0);
  query.bind(QStringLiteral(":pos"), employee.position.trimmed());
  query.bind(QStringLiteral(":dept"), employee.department.trimmed());
  query.bind(QStringLiteral(":fired"), employee.firedOn.isValid()
                                           ? QVariant(iso(employee.firedOn))
                                           : QVariant(QMetaType(QMetaType::QString)));
  query.bind(QStringLiteral(":pit"), employee.hasPitBenefit ? 1 : 0);
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Result<EmployeeId>::fail(
          Error::conflict(QStringLiteral("РНОКПП уже використовується")));
    }
    return Result<EmployeeId>::fail(status.error());
  }
  return Result<EmployeeId>::ok(EmployeeId{query.lastInsertId()});
}

Status EmployeeRepository::update(const Employee & employee)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!employee.id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id працівника")));
  }
  const Status valid = validateEmployee(employee);
  if (!valid) {
    return valid;
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE employees SET tax_id = :tin, name = :name, hired_on = :hired, "
                     "salary_cents = :sal, disabled_ssc = :dis, is_active = :active, "
                     "position = :pos, department = :dept, fired_on = :fired, "
                     "has_pit_benefit = :pit "
                     "WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":tin"), employee.taxId.trimmed());
  query.bind(QStringLiteral(":name"), employee.name.trimmed());
  query.bind(QStringLiteral(":hired"), iso(employee.hiredOn));
  query.bind(QStringLiteral(":sal"), static_cast<qlonglong>(employee.salary.cents()));
  query.bind(QStringLiteral(":dis"), employee.disabledSsc ? 1 : 0);
  query.bind(QStringLiteral(":active"), employee.isActive ? 1 : 0);
  query.bind(QStringLiteral(":pos"), employee.position.trimmed());
  query.bind(QStringLiteral(":dept"), employee.department.trimmed());
  query.bind(QStringLiteral(":fired"), employee.firedOn.isValid()
                                           ? QVariant(iso(employee.firedOn))
                                           : QVariant(QMetaType(QMetaType::QString)));
  query.bind(QStringLiteral(":pit"), employee.hasPitBenefit ? 1 : 0);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(employee.id.value));
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Status::fail(Error::conflict(QStringLiteral("РНОКПП уже використовується")));
    }
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("працівника не знайдено")));
  }
  return Status::ok();
}

Status EmployeeRepository::remove(EmployeeId id)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  const Result<Employee> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  sql::Query used(db_);
  Status status =
      used.prepare(QStringLiteral("SELECT 1 FROM document_lines WHERE employee_id = :id LIMIT 1"));
  if (!status) {
    return status;
  }
  used.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = used.exec();
  if (!status) {
    return status;
  }
  if (used.next()) {
    return Status::fail(Error::conflict(QStringLiteral("працівник використаний у документах")));
  }
  sql::Query query(db_);
  status = query.prepare(QStringLiteral("DELETE FROM employees WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

} // namespace fin
