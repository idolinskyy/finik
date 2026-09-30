#include "fin/db/CounterpartyRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"
#include "fin/domain/BankRequisites.h"

namespace fin {
namespace {

Status validateCounterparty(const Counterparty & counterparty)
{
  if (counterparty.name.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("назва контрагента обов'язкова")));
  }
  const QString edrpou = counterparty.edrpou.trimmed();
  if (!edrpou.isEmpty()) {
    if (edrpou.size() < 8 || edrpou.size() > 10) {
      return Status::fail(Error::validation(QStringLiteral("ЄДРПОУ/ІПН має містити 8–10 цифр")));
    }
    for (const QChar c : edrpou) {
      if (!c.isDigit()) {
        return Status::fail(Error::validation(QStringLiteral("ЄДРПОУ/ІПН має містити лише цифри")));
      }
    }
  }
  const Status bank = validateIban(counterparty.iban);
  if (!bank) {
    return bank;
  }
  return Status::ok();
}

Counterparty fromQuery(sql::Query & query)
{
  Counterparty row;
  row.id = CounterpartyId{query.value(0).toLongLong()};
  row.name = query.value(1).toString();
  row.edrpou = query.value(2).toString();
  row.iban = query.value(3).toString();
  row.bankName = query.value(4).toString();
  row.isCustomer = query.value(5).toInt() != 0;
  row.isSupplier = query.value(6).toInt() != 0;
  row.notes = query.value(7).toString();
  return row;
}

const QString kSelect =
    QStringLiteral("SELECT id, name, edrpou, iban, bank_name, is_customer, is_supplier, notes "
                   "FROM counterparties");

} // namespace

CounterpartyRepository::CounterpartyRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<Counterparty> CounterpartyRepository::findById(CounterpartyId id) const
{
  if (!id) {
    return Result<Counterparty>::fail(
        Error::invalidArgument(QStringLiteral("порожній id контрагента")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<Counterparty>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<Counterparty>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Counterparty>::fail(Error::notFound(QStringLiteral("контрагента не знайдено")));
  }
  return Result<Counterparty>::ok(fromQuery(query));
}

Result<Counterparty> CounterpartyRepository::findByEdrpou(const QString & edrpou) const
{
  const QString code = edrpou.trimmed();
  if (code.isEmpty()) {
    return Result<Counterparty>::fail(Error::invalidArgument(QStringLiteral("порожній ЄДРПОУ")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE edrpou = :code"));
  if (!status) {
    return Result<Counterparty>::fail(status.error());
  }
  query.bind(QStringLiteral(":code"), code);
  status = query.exec();
  if (!status) {
    return Result<Counterparty>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Counterparty>::fail(
        Error::notFound(QStringLiteral("контрагента з ЄДРПОУ %1 не знайдено").arg(code)));
  }
  return Result<Counterparty>::ok(fromQuery(query));
}

Result<std::vector<Counterparty>> CounterpartyRepository::listAll() const
{
  sql::Query query(db_);
  Status status = query.exec(kSelect + QStringLiteral(" ORDER BY name COLLATE NOCASE"));
  if (!status) {
    return Result<std::vector<Counterparty>>::fail(status.error());
  }
  std::vector<Counterparty> rows;
  while (query.next()) {
    rows.push_back(fromQuery(query));
  }
  return Result<std::vector<Counterparty>>::ok(std::move(rows));
}

Result<CounterpartyId> CounterpartyRepository::insert(const Counterparty & counterparty)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<CounterpartyId>::fail(access.error());
  }
  const Status valid = validateCounterparty(counterparty);
  if (!valid) {
    return Result<CounterpartyId>::fail(valid.error());
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("INSERT INTO counterparties(name, edrpou, iban, bank_name, "
                                   "is_customer, is_supplier, notes) "
                                   "VALUES(:name, :edrpou, :iban, :bank, :cust, :sup, :notes)"));
  if (!status) {
    return Result<CounterpartyId>::fail(status.error());
  }
  query.bind(QStringLiteral(":name"), counterparty.name.trimmed());
  query.bind(QStringLiteral(":edrpou"), counterparty.edrpou.trimmed());
  query.bind(QStringLiteral(":iban"), normalizeIban(counterparty.iban));
  query.bind(QStringLiteral(":bank"), counterparty.bankName.trimmed());
  query.bind(QStringLiteral(":cust"), counterparty.isCustomer ? 1 : 0);
  query.bind(QStringLiteral(":sup"), counterparty.isSupplier ? 1 : 0);
  query.bind(QStringLiteral(":notes"), counterparty.notes);
  status = query.exec();
  if (!status) {
    return Result<CounterpartyId>::fail(status.error());
  }
  return Result<CounterpartyId>::ok(CounterpartyId{query.lastInsertId()});
}

Status CounterpartyRepository::update(const Counterparty & counterparty)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!counterparty.id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id контрагента")));
  }
  const Status valid = validateCounterparty(counterparty);
  if (!valid) {
    return valid;
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("UPDATE counterparties SET name = :name, edrpou = :edrpou, "
                                   "iban = :iban, bank_name = :bank, is_customer = :cust, "
                                   "is_supplier = :sup, notes = :notes WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":name"), counterparty.name.trimmed());
  query.bind(QStringLiteral(":edrpou"), counterparty.edrpou.trimmed());
  query.bind(QStringLiteral(":iban"), normalizeIban(counterparty.iban));
  query.bind(QStringLiteral(":bank"), counterparty.bankName.trimmed());
  query.bind(QStringLiteral(":cust"), counterparty.isCustomer ? 1 : 0);
  query.bind(QStringLiteral(":sup"), counterparty.isSupplier ? 1 : 0);
  query.bind(QStringLiteral(":notes"), counterparty.notes);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(counterparty.id.value));
  status = query.exec();
  if (!status) {
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("контрагента не знайдено")));
  }
  return Status::ok();
}

Status CounterpartyRepository::remove(CounterpartyId id)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id контрагента")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral("DELETE FROM counterparties WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    const QString text = status.message();
    if (text.contains(QLatin1String("FOREIGN KEY"), Qt::CaseInsensitive)) {
      return Status::fail(
          Error::conflict(QStringLiteral("контрагента не можна видалити: є проведені документи")));
    }
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("контрагента не знайдено")));
  }
  return Status::ok();
}

} // namespace fin
