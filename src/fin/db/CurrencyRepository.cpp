#include "fin/db/CurrencyRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"

namespace fin {
namespace {

Status validateCurrency(const Currency & currency)
{
  if (currency.code.trimmed().isEmpty() || currency.code.trimmed().length() != 3) {
    return Status::fail(
        Error::validation(QStringLiteral("код валюти повинен містити 3 символи (ISO 4217)")));
  }
  if (currency.nameUa.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("назва валюти обов'язкова")));
  }
  return Status::ok();
}

Currency currencyFromQuery(sql::Query & query)
{
  Currency row;
  row.code = query.value(0).toString().toUpper();
  row.numericCode = query.value(1).toInt();
  row.nameUa = query.value(2).toString();
  row.symbol = query.value(3).toString();
  row.isActive = query.value(4).toInt() != 0;
  return row;
}

CurrencyRate rateFromQuery(sql::Query & query)
{
  CurrencyRate row;
  row.currencyCode = query.value(0).toString().toUpper();
  row.rateDate = QDate::fromString(query.value(1).toString(), Qt::ISODate);
  row.nominal = query.value(2).toInt();
  row.rateMicros = query.value(3).toLongLong();
  return row;
}

const QString kSelectCurrency =
    QStringLiteral("SELECT code, numeric_code, name_ua, symbol, is_active FROM currencies");

const QString kSelectRate =
    QStringLiteral("SELECT currency_code, rate_date, nominal, rate_micros FROM currency_rates");

} // namespace

CurrencyRepository::CurrencyRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<std::vector<Currency>> CurrencyRepository::listAll(bool activeOnly) const
{
  sql::Query query(db_);
  QString sql = kSelectCurrency;
  if (activeOnly) {
    sql += QStringLiteral(" WHERE is_active = 1");
  }
  sql += QStringLiteral(" ORDER BY code ASC");
  Status status = query.exec(sql);
  if (!status) {
    return Result<std::vector<Currency>>::fail(status.error());
  }
  std::vector<Currency> rows;
  while (query.next()) {
    rows.push_back(currencyFromQuery(query));
  }
  return Result<std::vector<Currency>>::ok(std::move(rows));
}

Result<Currency> CurrencyRepository::findByCode(const QString & code) const
{
  const QString trimmedCode = code.trimmed().toUpper();
  if (trimmedCode.isEmpty()) {
    return Result<Currency>::fail(Error::invalidArgument(QStringLiteral("порожній код валюти")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelectCurrency + QStringLiteral(" WHERE code = :code"));
  if (!status) {
    return Result<Currency>::fail(status.error());
  }
  query.bind(QStringLiteral(":code"), trimmedCode);
  status = query.exec();
  if (!status) {
    return Result<Currency>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Currency>::fail(
        Error::notFound(QStringLiteral("валюту %1 не знайдено").arg(trimmedCode)));
  }
  return Result<Currency>::ok(currencyFromQuery(query));
}

Status CurrencyRepository::insert(const Currency & currency)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  const Status valid = validateCurrency(currency);
  if (!valid) {
    return valid;
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("INSERT INTO currencies(code, numeric_code, name_ua, symbol, is_active) "
                     "VALUES(:code, :num, :name, :sym, :active)"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":code"), currency.code.trimmed().toUpper());
  query.bind(QStringLiteral(":num"), currency.numericCode);
  query.bind(QStringLiteral(":name"), currency.nameUa.trimmed());
  query.bind(QStringLiteral(":sym"), currency.symbol.trimmed());
  query.bind(QStringLiteral(":active"), currency.isActive ? 1 : 0);
  return query.exec();
}

Status CurrencyRepository::update(const Currency & currency)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  const Status valid = validateCurrency(currency);
  if (!valid) {
    return valid;
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("UPDATE currencies SET numeric_code = :num, name_ua = :name, "
                                   "symbol = :sym, is_active = :active WHERE code = :code"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":num"), currency.numericCode);
  query.bind(QStringLiteral(":name"), currency.nameUa.trimmed());
  query.bind(QStringLiteral(":sym"), currency.symbol.trimmed());
  query.bind(QStringLiteral(":active"), currency.isActive ? 1 : 0);
  query.bind(QStringLiteral(":code"), currency.code.trimmed().toUpper());
  status = query.exec();
  if (!status) {
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("валюту не знайдено")));
  }
  return Status::ok();
}

Status CurrencyRepository::remove(const QString & code)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  const QString upperCode = code.trimmed().toUpper();
  if (upperCode == QStringLiteral("UAH")) {
    return Status::fail(
        Error::conflict(QStringLiteral("неможливо видалити національну валюту (UAH)")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral("DELETE FROM currencies WHERE code = :code"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":code"), upperCode);
  status = query.exec();
  if (!status) {
    const QString text = status.message();
    if (text.contains(QLatin1String("FOREIGN KEY"), Qt::CaseInsensitive)) {
      return Status::fail(Error::conflict(
          QStringLiteral("валюту не можна видалити: є пов'язані документи, проводки або курси")));
    }
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("валюту не знайдено")));
  }
  return Status::ok();
}

Result<CurrencyRate> CurrencyRepository::getRate(const QString & currencyCode, QDate date) const
{
  const QString code = currencyCode.trimmed().toUpper();
  if (code.isEmpty() || code == QStringLiteral("UAH")) {
    CurrencyRate uah;
    uah.currencyCode = QStringLiteral("UAH");
    uah.rateDate = date.isValid() ? date : QDate::currentDate();
    uah.nominal = 1;
    uah.rateMicros = ExchangeRate::kMicrosPerUah;
    return Result<CurrencyRate>::ok(uah);
  }

  const QString dateStr =
      date.isValid() ? date.toString(Qt::ISODate) : QDate::currentDate().toString(Qt::ISODate);

  sql::Query query(db_);
  Status status = query.prepare(kSelectRate +
                                QStringLiteral(" WHERE currency_code = :code AND rate_date <= :dt "
                                               " ORDER BY rate_date DESC LIMIT 1"));
  if (!status) {
    return Result<CurrencyRate>::fail(status.error());
  }
  query.bind(QStringLiteral(":code"), code);
  query.bind(QStringLiteral(":dt"), dateStr);
  status = query.exec();
  if (!status) {
    return Result<CurrencyRate>::fail(status.error());
  }
  if (query.next()) {
    return Result<CurrencyRate>::ok(rateFromQuery(query));
  }

  // Якщо попередній курс не знайдено — помилка (без підстановки майбутнього курсу).
  return Result<CurrencyRate>::fail(Error::notFound(
      QStringLiteral("курс для валюти %1 на дату %2 не знайдено").arg(code, dateStr)));
}

bool CurrencyRepository::hasRatesOnDate(QDate date) const
{
  if (!date.isValid()) {
    return false;
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT 1 FROM currency_rates WHERE rate_date = :dt LIMIT 1"));
  if (!status) {
    return false;
  }
  query.bind(QStringLiteral(":dt"), date.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return false;
  }
  return query.next();
}

bool CurrencyRepository::hasRateOnDate(const QString & currencyCode, QDate date) const
{
  if (!date.isValid()) {
    return false;
  }
  const QString code = currencyCode.trimmed().toUpper();
  if (code.isEmpty() || code == QStringLiteral("UAH")) {
    return true;
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral(
      "SELECT 1 FROM currency_rates WHERE currency_code = :code AND rate_date = :dt LIMIT 1"));
  if (!status) {
    return false;
  }
  query.bind(QStringLiteral(":code"), code);
  query.bind(QStringLiteral(":dt"), date.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return false;
  }
  return query.next();
}

Result<std::vector<CurrencyRate>> CurrencyRepository::listRates(const QString & currencyCode,
                                                                QDate from, QDate to) const
{
  const QString code = currencyCode.trimmed().toUpper();
  sql::Query query(db_);
  QString sql = kSelectRate + QStringLiteral(" WHERE currency_code = :code");
  if (from.isValid()) {
    sql += QStringLiteral(" AND rate_date >= :from");
  }
  if (to.isValid()) {
    sql += QStringLiteral(" AND rate_date <= :to");
  }
  sql += QStringLiteral(" ORDER BY rate_date DESC");

  Status status = query.prepare(sql);
  if (!status) {
    return Result<std::vector<CurrencyRate>>::fail(status.error());
  }
  query.bind(QStringLiteral(":code"), code);
  if (from.isValid()) {
    query.bind(QStringLiteral(":from"), from.toString(Qt::ISODate));
  }
  if (to.isValid()) {
    query.bind(QStringLiteral(":to"), to.toString(Qt::ISODate));
  }
  status = query.exec();
  if (!status) {
    return Result<std::vector<CurrencyRate>>::fail(status.error());
  }
  std::vector<CurrencyRate> rows;
  while (query.next()) {
    rows.push_back(rateFromQuery(query));
  }
  return Result<std::vector<CurrencyRate>>::ok(std::move(rows));
}

Status CurrencyRepository::setRate(const CurrencyRate & rate)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  const QString code = rate.currencyCode.trimmed().toUpper();
  if (code.isEmpty()) {
    return Status::fail(Error::invalidArgument(QStringLiteral("не вказано код валюти")));
  }
  if (!rate.rateDate.isValid()) {
    return Status::fail(Error::invalidArgument(QStringLiteral("некоректна дата курсу")));
  }
  if (rate.nominal <= 0) {
    return Status::fail(
        Error::invalidArgument(QStringLiteral("номінал курсу повинен бути більшим за 0")));
  }
  if (rate.rateMicros <= 0) {
    return Status::fail(
        Error::invalidArgument(QStringLiteral("значення курсу повинно бути додатнім")));
  }

  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("INSERT INTO currency_rates(currency_code, rate_date, nominal, rate_micros) "
                     "VALUES(:code, :dt, :nom, :rate) "
                     "ON CONFLICT(currency_code, rate_date) DO UPDATE SET "
                     "nominal = excluded.nominal, rate_micros = excluded.rate_micros"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":code"), code);
  query.bind(QStringLiteral(":dt"), rate.rateDate.toString(Qt::ISODate));
  query.bind(QStringLiteral(":nom"), rate.nominal);
  query.bind(QStringLiteral(":rate"), static_cast<qlonglong>(rate.rateMicros));
  return query.exec();
}

Status CurrencyRepository::removeRate(const QString & currencyCode, QDate date)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("DELETE FROM currency_rates WHERE currency_code = :code AND rate_date = :dt"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":code"), currencyCode.trimmed().toUpper());
  query.bind(QStringLiteral(":dt"), date.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("курс не знайдено")));
  }
  return Status::ok();
}

} // namespace fin
