#include "fin/db/ContractRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"

namespace fin {
namespace {

Status validateContract(const Contract & contract)
{
  if (!contract.counterpartyId) {
    return Status::fail(Error::validation(QStringLiteral("контрагент договору обов'язковий")));
  }
  if (contract.number.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("номер договору обов'язковий")));
  }
  if (!contract.date.isValid()) {
    return Status::fail(Error::validation(QStringLiteral("некоректна дата договору")));
  }
  return Status::ok();
}

Contract fromQuery(sql::Query & query)
{
  Contract row;
  row.id = ContractId{query.value(0).toLongLong()};
  row.counterpartyId = CounterpartyId{query.value(1).toLongLong()};
  row.number = query.value(2).toString();
  row.name = query.value(3).toString();
  row.date = QDate::fromString(query.value(4).toString(), Qt::ISODate);
  row.contractType = contractTypeFromString(query.value(5).toString());
  row.currency = query.value(6).toString();
  row.isActive = query.value(7).toInt() != 0;
  return row;
}

const QString kSelect = QStringLiteral(
    "SELECT id, counterparty_id, number, name, date, contract_type, currency, is_active "
    "FROM contracts");

} // namespace

ContractRepository::ContractRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<Contract> ContractRepository::findById(ContractId id) const
{
  if (!id) {
    return Result<Contract>::fail(Error::invalidArgument(QStringLiteral("порожній id договору")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<Contract>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<Contract>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Contract>::fail(Error::notFound(QStringLiteral("договір не знайдено")));
  }
  return Result<Contract>::ok(fromQuery(query));
}

Result<std::vector<Contract>> ContractRepository::listByCounterparty(CounterpartyId counterpartyId,
                                                                     bool activeOnly) const
{
  if (!counterpartyId) {
    return Result<std::vector<Contract>>::fail(
        Error::invalidArgument(QStringLiteral("порожній id контрагента")));
  }
  sql::Query query(db_);
  QString sql = kSelect + QStringLiteral(" WHERE counterparty_id = :cp_id");
  if (activeOnly) {
    sql += QStringLiteral(" AND is_active = 1");
  }
  sql += QStringLiteral(" ORDER BY date DESC, id DESC");
  Status status = query.prepare(sql);
  if (!status) {
    return Result<std::vector<Contract>>::fail(status.error());
  }
  query.bind(QStringLiteral(":cp_id"), static_cast<qlonglong>(counterpartyId.value));
  status = query.exec();
  if (!status) {
    return Result<std::vector<Contract>>::fail(status.error());
  }
  std::vector<Contract> rows;
  while (query.next()) {
    rows.push_back(fromQuery(query));
  }
  return Result<std::vector<Contract>>::ok(std::move(rows));
}

Result<std::vector<Contract>> ContractRepository::listAll(bool activeOnly) const
{
  sql::Query query(db_);
  QString sql = kSelect;
  if (activeOnly) {
    sql += QStringLiteral(" WHERE is_active = 1");
  }
  sql += QStringLiteral(" ORDER BY counterparty_id ASC, date DESC, id DESC");
  Status status = query.exec(sql);
  if (!status) {
    return Result<std::vector<Contract>>::fail(status.error());
  }
  std::vector<Contract> rows;
  while (query.next()) {
    rows.push_back(fromQuery(query));
  }
  return Result<std::vector<Contract>>::ok(std::move(rows));
}

Result<ContractId> ContractRepository::insert(const Contract & contract)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<ContractId>::fail(access.error());
  }
  const Status valid = validateContract(contract);
  if (!valid) {
    return Result<ContractId>::fail(valid.error());
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("INSERT INTO contracts(counterparty_id, number, name, date, contract_type, "
                     "currency, is_active) "
                     "VALUES(:cp_id, :number, :name, :date, :type, :currency, :active)"));
  if (!status) {
    return Result<ContractId>::fail(status.error());
  }
  query.bind(QStringLiteral(":cp_id"), static_cast<qlonglong>(contract.counterpartyId.value));
  query.bind(QStringLiteral(":number"), contract.number.trimmed());
  query.bind(QStringLiteral(":name"),
             contract.name.trimmed().isEmpty()
                 ? QStringLiteral("Договір №%1 від %2")
                       .arg(contract.number.trimmed(), contract.date.toString(Qt::ISODate))
                 : contract.name.trimmed());
  query.bind(QStringLiteral(":date"), contract.date.toString(Qt::ISODate));
  query.bind(QStringLiteral(":type"), contractTypeToString(contract.contractType));
  query.bind(QStringLiteral(":currency"), contract.currency.isEmpty()
                                              ? QStringLiteral("UAH")
                                              : contract.currency.trimmed().toUpper());
  query.bind(QStringLiteral(":active"), contract.isActive ? 1 : 0);
  status = query.exec();
  if (!status) {
    return Result<ContractId>::fail(status.error());
  }
  return Result<ContractId>::ok(ContractId{query.lastInsertId()});
}

Status ContractRepository::update(const Contract & contract)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!contract.id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id договору")));
  }
  const Status valid = validateContract(contract);
  if (!valid) {
    return valid;
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE contracts SET counterparty_id = :cp_id, number = :number, "
                     "name = :name, date = :date, contract_type = :type, "
                     "currency = :currency, is_active = :active WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":cp_id"), static_cast<qlonglong>(contract.counterpartyId.value));
  query.bind(QStringLiteral(":number"), contract.number.trimmed());
  query.bind(QStringLiteral(":name"),
             contract.name.trimmed().isEmpty()
                 ? QStringLiteral("Договір №%1 від %2")
                       .arg(contract.number.trimmed(), contract.date.toString(Qt::ISODate))
                 : contract.name.trimmed());
  query.bind(QStringLiteral(":date"), contract.date.toString(Qt::ISODate));
  query.bind(QStringLiteral(":type"), contractTypeToString(contract.contractType));
  query.bind(QStringLiteral(":currency"), contract.currency.isEmpty()
                                              ? QStringLiteral("UAH")
                                              : contract.currency.trimmed().toUpper());
  query.bind(QStringLiteral(":active"), contract.isActive ? 1 : 0);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(contract.id.value));
  status = query.exec();
  if (!status) {
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("договір не знайдено")));
  }
  return Status::ok();
}

Status ContractRepository::remove(ContractId id)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id договору")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral("DELETE FROM contracts WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    const QString text = status.message();
    if (text.contains(QLatin1String("FOREIGN KEY"), Qt::CaseInsensitive)) {
      return Status::fail(Error::conflict(
          QStringLiteral("договір не можна видалити: є пов'язані документи або проводки")));
    }
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("договір не знайдено")));
  }
  return Status::ok();
}

} // namespace fin
