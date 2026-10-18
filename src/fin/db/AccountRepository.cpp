#include "fin/db/AccountRepository.h"

#include "fin/core/Sql.h"
#include "fin/core/Transaction.h"
#include "fin/domain/ChartCodes.h"

#include <QHash>
#include <QVariant>

namespace fin {
namespace {

Account accountFromQuery(sql::Query & query)
{
  Account account;
  account.id = AccountId{query.value(0).toLongLong()};
  account.code = query.value(1).toString();
  account.name = query.value(2).toString();
  const std::int64_t parent = query.value(3).toLongLong();
  if (parent > 0) {
    account.parentId = AccountId{parent};
  }
  account.accountClass = query.value(4).toInt();
  const auto side = normalSideFromString(query.value(5).toString());
  account.normalSide = side.value_or(NormalSide::Debit);
  account.hasCounterparty = query.value(6).toInt() != 0;
  account.isFolder = query.value(7).toInt() != 0;
  account.isActive = query.value(8).toInt() != 0;
  return account;
}

const QString kSelect =
    QStringLiteral("SELECT id, code, name, parent_id, account_class, normal_side, "
                   "has_counterparty, is_folder, is_active FROM accounts");

struct SeedAccount
{
  const char * code;
  const char * name;
  const char * parentCode;
  int accountClass;
  NormalSide side;
  bool hasCounterparty;
  bool folder;
};

const SeedAccount kChart[] = {
    {chart::kAuxiliary, "Допоміжний", nullptr, 0, NormalSide::Debit, false, false},
    {"10", "Основні засоби", nullptr, 1, NormalSide::Debit, false, true},
    {chart::kMachinery, "Машини та обладнання", "10", 1, NormalSide::Debit, false, false},
    {chart::kVehicles, "Транспортні засоби", "10", 1, NormalSide::Debit, false, false},
    {chart::kOtherFixedAssets, "Інші основні засоби", "10", 1, NormalSide::Debit, false, false},
    {"13", "Знос необоротних активів", nullptr, 1, NormalSide::Credit, false, true},
    {chart::kFaDepreciation, "Знос основних засобів", "13", 1, NormalSide::Credit, false, false},
    {chart::kIaAmortization, "Накопичена амортизація нематеріальних активів", "13", 1,
     NormalSide::Credit, false, false},
    {"15", "Капітальні інвестиції", nullptr, 1, NormalSide::Debit, false, true},
    {chart::kCapexFa, "Придбання (виготовлення) основних засобів", "15", 1, NormalSide::Debit,
     false, false},
    {chart::kCapexIa, "Придбання (створення) нематеріальних активів", "15", 1, NormalSide::Debit,
     false, false},
    {"12", "Нематеріальні активи", nullptr, 1, NormalSide::Debit, false, true},
    {chart::kSoftware, "Авторське право та суміжні права", "12", 1, NormalSide::Debit, false,
     false},
    {chart::kOtherIntangibles, "Інші нематеріальні активи", "12", 1, NormalSide::Debit, false,
     false},
    {"30", "Каса", nullptr, 3, NormalSide::Debit, false, true},
    {chart::kCash, "Каса в національній валюті", "30", 3, NormalSide::Debit, false, false},
    {chart::kForeignCash, "Готівка в іноземній валюті", "30", 3, NormalSide::Debit, false, false},
    {"31", "Рахунки в банках", nullptr, 3, NormalSide::Debit, false, true},
    {chart::kBank, "Поточні рахунки в національній валюті", "31", 3, NormalSide::Debit, false,
     false},
    {chart::kForeignBank, "Поточні рахунки в іноземній валюті", "31", 3, NormalSide::Debit, false,
     false},
    {chart::kForeignTransit, "Грошові кошти в дорозі в іноземній валюті", nullptr, 3,
     NormalSide::Debit, false, false},
    {"36", "Розрахунки з покупцями та замовниками", nullptr, 3, NormalSide::Debit, true, true},
    {chart::kCustomers, "Розрахунки з вітчизняними покупцями", "36", 3, NormalSide::Debit, true,
     false},
    {chart::kForeignCustomers, "Розрахунки з іноземними покупцями", "36", 3, NormalSide::Debit,
     true, false},
    {"37", "Розрахунки з різними дебіторами", nullptr, 3, NormalSide::Debit, true, true},
    {chart::kAdvancesIssued, "Розрахунки за виданими авансами", "37", 3, NormalSide::Debit, true,
     false},
    {chart::kForeignAdvancesIssued, "Аванси видані в іноземній валюті", "37", 3, NormalSide::Debit,
     true, false},
    {"63", "Розрахунки з постачальниками та підрядниками", nullptr, 4, NormalSide::Credit, true,
     true},
    {chart::kSuppliers, "Розрахунки з вітчизняними постачальниками", "63", 4, NormalSide::Credit,
     true, false},
    {chart::kForeignSuppliers, "Розрахунки з іноземними постачальниками", "63", 4,
     NormalSide::Credit, true, false},
    {"68", "Розрахунки за іншими операціями", nullptr, 6, NormalSide::Credit, true, true},
    {chart::kAdvancesReceived, "Розрахунки за одержаними авансами", "68", 6, NormalSide::Credit,
     true, false},
    {chart::kForeignAdvancesReceived, "Аванси одержані в іноземній валюті", "68", 6,
     NormalSide::Credit, true, false},
    {"64", "Розрахунки за податками й платежами", nullptr, 6, NormalSide::Credit, false, true},
    {chart::kVatBudget, "Розрахунки за податком на додану вартість", "64", 6, NormalSide::Credit,
     false, false},
    {chart::kVatLiability, "Податкові зобов'язання", "64", 6, NormalSide::Credit, false, false},
    {chart::kVatCredit, "Податковий кредит", "64", 6, NormalSide::Debit, false, false},
    {chart::kPitPayable, "Розрахунки за податком на доходи фізичних осіб", "64", 6,
     NormalSide::Credit, false, false},
    {chart::kMilitaryLevy, "Розрахунки за військовим збором", "64", 6, NormalSide::Credit, false,
     false},
    {"65", "Розрахунки за страхуванням", nullptr, 6, NormalSide::Credit, false, true},
    {chart::kSscPayable, "Розрахунки за єдиним соціальним внеском", "65", 6, NormalSide::Credit,
     false, false},
    {"66", "Розрахунки за виплатами працівникам", nullptr, 6, NormalSide::Credit, false, true},
    {chart::kSalaryPayable, "Розрахунки за заробітною платою", "66", 6, NormalSide::Credit, false,
     false},
    {"28", "Товари", nullptr, 2, NormalSide::Debit, false, true},
    {chart::kGoods, "Товари на складі", "28", 2, NormalSide::Debit, false, false},
    {chart::kLowValue, "Малоцінні та швидкозношувані предмети", nullptr, 2, NormalSide::Debit,
     false, false},
    {"70", "Доходи від реалізації", nullptr, 7, NormalSide::Credit, false, true},
    {chart::kRevenue, "Дохід від реалізації товарів", "70", 7, NormalSide::Credit, false, false},
    {chart::kServiceRevenue, "Дохід від реалізації послуг", "70", 7, NormalSide::Credit, false,
     false},
    {chart::kExchangeGain, "Дохід від операційної курсової різниці", nullptr, 7, NormalSide::Credit,
     false, false},
    {"90", "Собівартість реалізації", nullptr, 9, NormalSide::Debit, false, true},
    {chart::kCogs, "Собівартість реалізованих товарів", "90", 9, NormalSide::Debit, false, false},
    {chart::kAdminExpense, "Адміністративні витрати", nullptr, 9, NormalSide::Debit, false, false},
    {chart::kExchangeLoss, "Втрати від операційної курсової різниці", nullptr, 9, NormalSide::Debit,
     false, false},
    {chart::kOtherIncome, "Інший операційний дохід", nullptr, 7, NormalSide::Credit, false, false},
    {chart::kFinancialResult, "Фінансові результати", nullptr, 7, NormalSide::Credit, false, false},
    {chart::kCapital, "Зареєстрований (пайовий) капітал", nullptr, 4, NormalSide::Credit, false,
     false},
    {chart::kRetainedEarnings, "Нерозподілені прибутки (непокриті збитки)", nullptr, 4,
     NormalSide::Credit, false, false},
};

} // namespace

AccountRepository::AccountRepository(QSqlDatabase db) : db_(std::move(db)) {}

Status AccountRepository::seedChartIfEmpty()
{
  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }

  QHash<QString, AccountId> byCode;
  {
    sql::Query existing(db_);
    Status status = existing.exec(QStringLiteral("SELECT id, code FROM accounts"));
    if (!status) {
      return status;
    }
    while (existing.next()) {
      byCode.insert(existing.value(1).toString(), AccountId{existing.value(0).toLongLong()});
    }
  }

  sql::Query insert(db_);
  Status status = insert.prepare(
      QStringLiteral("INSERT INTO accounts(code, name, parent_id, account_class, "
                     "normal_side, has_counterparty, is_folder, is_active) "
                     "VALUES(:code, :name, :parent, :class, :side, :cp, :folder, 1)"));
  if (!status) {
    return status;
  }

  for (const SeedAccount & row : kChart) {
    const QString code = QString::fromLatin1(row.code);
    if (byCode.contains(code)) {
      continue;
    }
    QVariant parent;
    if (row.parentCode != nullptr) {
      const auto it = byCode.constFind(QString::fromLatin1(row.parentCode));
      if (it == byCode.cend()) {
        return Status::fail(
            Error::database(QStringLiteral("seed: невідомий батьківський рахунок %1")
                                .arg(QLatin1String(row.parentCode))));
      }
      parent = QVariant::fromValue(static_cast<qlonglong>(it->value));
    }
    insert.bind(QStringLiteral(":code"), code);
    insert.bind(QStringLiteral(":name"), QString::fromUtf8(row.name));
    insert.bind(QStringLiteral(":parent"), parent);
    insert.bind(QStringLiteral(":class"), row.accountClass);
    insert.bind(QStringLiteral(":side"), normalSideToString(row.side));
    insert.bind(QStringLiteral(":cp"), row.hasCounterparty ? 1 : 0);
    insert.bind(QStringLiteral(":folder"), row.folder ? 1 : 0);
    status = insert.exec();
    if (!status) {
      return status;
    }
    byCode.insert(code, AccountId{insert.lastInsertId()});
  }

  const QStringList subcontoSeeds = {
      QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, "
                     "subconto_kind_id, is_required) "
                     "SELECT id, 1, 'counterparty', 1 FROM accounts WHERE code IN ('361', '362', "
                     "'631', '632', '371', '3712', '681', '6812')"),
      QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, "
                     "subconto_kind_id, is_required) "
                     "SELECT id, 2, 'contract', 0 FROM accounts WHERE code IN ('361', '362', "
                     "'631', '632', '371', '3712', '681', '6812')"),
      QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, "
                     "subconto_kind_id, is_required) "
                     "SELECT id, 1, 'nomenclature', 1 FROM accounts WHERE code IN ('281')"),
      QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, "
                     "subconto_kind_id, is_required) "
                     "SELECT id, 1, 'fixed_asset', 1 FROM accounts WHERE code IN ('104', '105', "
                     "'109', '131', '152')"),
      QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, "
                     "subconto_kind_id, is_required) "
                     "SELECT id, 1, 'intangible_asset', 1 FROM accounts WHERE code IN ('125', "
                     "'127', '133', '154')"),
      QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, "
                     "subconto_kind_id, is_required) "
                     "SELECT id, 1, 'low_value', 1 FROM accounts WHERE code IN ('22')"),
      QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, "
                     "subconto_kind_id, is_required) "
                     "SELECT id, 1, 'employee', 1 FROM accounts WHERE code IN ('661')")};
  for (const QString & stmt : subcontoSeeds) {
    sql::Query subQuery(db_);
    status = subQuery.exec(stmt);
    if (!status) {
      return status;
    }
  }

  return txn.commit();
}

Result<Account> AccountRepository::findById(AccountId id) const
{
  if (!id) {
    return Result<Account>::fail(Error::invalidArgument(QStringLiteral("порожній id рахунку")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<Account>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<Account>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Account>::fail(Error::notFound(QStringLiteral("рахунок не знайдено")));
  }
  return Result<Account>::ok(accountFromQuery(query));
}

Result<Account> AccountRepository::findByCode(const QString & code) const
{
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE code = :code"));
  if (!status) {
    return Result<Account>::fail(status.error());
  }
  query.bind(QStringLiteral(":code"), code);
  status = query.exec();
  if (!status) {
    return Result<Account>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Account>::fail(
        Error::notFound(QStringLiteral("рахунок %1 не знайдено").arg(code)));
  }
  return Result<Account>::ok(accountFromQuery(query));
}

Result<std::vector<Account>> AccountRepository::listAll() const
{
  sql::Query query(db_);
  Status status = query.exec(kSelect + QStringLiteral(" ORDER BY account_class, code"));
  if (!status) {
    return Result<std::vector<Account>>::fail(status.error());
  }
  std::vector<Account> accounts;
  while (query.next()) {
    accounts.push_back(accountFromQuery(query));
  }
  return Result<std::vector<Account>>::ok(std::move(accounts));
}

Result<std::vector<Account>> AccountRepository::listPostable() const
{
  sql::Query query(db_);
  Status status = query.exec(kSelect + QStringLiteral(" WHERE is_folder = 0 AND is_active = 1 "
                                                      "ORDER BY account_class, code"));
  if (!status) {
    return Result<std::vector<Account>>::fail(status.error());
  }
  std::vector<Account> accounts;
  while (query.next()) {
    accounts.push_back(accountFromQuery(query));
  }
  return Result<std::vector<Account>>::ok(std::move(accounts));
}

} // namespace fin
