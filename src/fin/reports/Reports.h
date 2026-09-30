#pragma once

// Звіти: ОСВ, картка рахунку, каса, баланс, ПДВ тощо.

#include "fin/core/Ids.h"
#include "fin/core/Money.h"
#include "fin/core/Quantity.h"
#include "fin/core/Result.h"
#include "fin/domain/Company.h"
#include "fin/domain/Counterparty.h"
#include "fin/domain/Subconto.h"

#include <vector>

#include <QDate>
#include <QSqlDatabase>
#include <QString>

namespace fin {

// Оборотно-сальдова відомість (ОСВ)
// Один рядок ОСВ відповідає одному субрахунку Плану рахунків.
// Початкове сальдо + обороти за період = кінцеве сальдо.
// Перевірка: totals.openingDebit == totals.openingCredit і аналогічно для closing.
struct TrialBalanceRow
{
  AccountId accountId;
  QString code;
  QString name;
  Money openingDebit = Money::zero();   // сальдо Дт на початок
  Money openingCredit = Money::zero();  // сальдо Кт на початок
  Money turnoverDebit = Money::zero();  // оборот Дт за період
  Money turnoverCredit = Money::zero(); // оборот Кт за період
  Money closingDebit = Money::zero();   // сальдо Дт на кінець
  Money closingCredit = Money::zero();  // сальдо Кт на кінець
  // Паралельний валютний залишок (якщо на рахунку рівно одна інвалюта)
  QString currencyCode;
  Money openingForeignDebit = Money::zero();
  Money openingForeignCredit = Money::zero();
  Money closingForeignDebit = Money::zero();
  Money closingForeignCredit = Money::zero();
};

struct TrialBalance
{
  QDate from;
  QDate to;
  std::vector<TrialBalanceRow> rows;
  TrialBalanceRow totals; // підсумок по всіх рахунках
};

class TrialBalanceReport
{
 public:
  explicit TrialBalanceReport(QSqlDatabase db);
  Result<TrialBalance> build(QDate from, QDate to) const;

 private:
  QSqlDatabase db_;
};

// Картка рахунку
// Хронологічний журнал проводок для одного субрахунку.
// Рядок картки відповідає одному рядку ledger_entries (Дт або Кт).
// balanceDebit / balanceCredit — наростаючий залишок після кожної операції.
struct AccountCardRow
{
  QDate date;
  DocumentId documentId;
  QString documentNumber;
  QString documentTypeLabel;
  QString corrAccountCode; // Кореспондуючий рахунок
  QString description;
  Money debit = Money::zero();
  Money credit = Money::zero();
  Money balanceDebit = Money::zero();
  Money balanceCredit = Money::zero();
  QString currencyCode; // Код іноземної валюти (якщо операція валютна)
  Money currencyDebit = Money::zero();  // Сума у валюті (дебет)
  Money currencyCredit = Money::zero(); // Сума у валюті (кредит)
  std::int64_t currencyRateMicros = 0;  // Курс перерахунку
  SubcontoSet debitSubconto;            // Значення субконто дебету
  SubcontoSet creditSubconto;           // Значення субконто кредиту
};

struct AccountCard
{
  AccountId accountId;
  QString code;
  QString name;
  QDate from;
  QDate to;
  Money openingDebit = Money::zero();
  Money openingCredit = Money::zero();
  std::vector<AccountCardRow> rows;
  Money turnoverDebit = Money::zero();
  Money turnoverCredit = Money::zero();
  Money closingDebit = Money::zero();
  Money closingCredit = Money::zero();
};

class AccountCardReport
{
 public:
  explicit AccountCardReport(QSqlDatabase db);
  // counterparty — необов'язковий фільтр: якщо переданий, показує лише
  // проводки по одному контрагенту (для Акту звірки).
  Result<AccountCard> build(AccountId accountId, QDate from, QDate to,
                            CounterpartyId counterparty = {}) const;

  // Розширений пошук за довільним субконто
  Result<AccountCard> buildWithSubconto(AccountId accountId, QDate from, QDate to,
                                        const QString & subcontoKindId,
                                        std::int64_t entityId) const;

 private:
  QSqlDatabase db_;
};

// ОСВ по рахунку в розрізі субконто (SubcontoTrialBalance)
struct SubcontoTrialBalanceRow
{
  SubcontoValue subconto;
  Money openingDebit = Money::zero();
  Money openingCredit = Money::zero();
  Money turnoverDebit = Money::zero();
  Money turnoverCredit = Money::zero();
  Money closingDebit = Money::zero();
  Money closingCredit = Money::zero();
};

struct SubcontoTrialBalance
{
  AccountId accountId;
  QString accountCode;
  QString accountName;
  QString subcontoKindId;
  QString subcontoKindName;
  QDate from;
  QDate to;
  std::vector<SubcontoTrialBalanceRow> rows;
  SubcontoTrialBalanceRow totals;
};

class SubcontoTrialBalanceReport
{
 public:
  explicit SubcontoTrialBalanceReport(QSqlDatabase db);
  Result<SubcontoTrialBalance> build(AccountId accountId, int lineNo, QDate from, QDate to) const;

 private:
  QSqlDatabase db_;
};

// Аналіз рахунку (AccountAnalysisReport)
struct AccountAnalysisCorrespondence
{
  AccountId corrAccountId;
  QString corrAccountCode;
  QString corrAccountName;
  Money debitTurnover = Money::zero(); // Дт обраного рахунку - Кт кореспондуючого
  Money creditTurnover = Money::zero(); // Дт кореспондуючого - Кт обраного рахунку
};

struct AccountAnalysis
{
  AccountId accountId;
  QString accountCode;
  QString accountName;
  QDate from;
  QDate to;
  Money openingDebit = Money::zero();
  Money openingCredit = Money::zero();
  std::vector<AccountAnalysisCorrespondence> correspondences;
  Money turnoverDebit = Money::zero();
  Money turnoverCredit = Money::zero();
  Money closingDebit = Money::zero();
  Money closingCredit = Money::zero();
};

class AccountAnalysisReport
{
 public:
  explicit AccountAnalysisReport(QSqlDatabase db);
  Result<AccountAnalysis> build(AccountId accountId, QDate from, QDate to) const;

 private:
  QSqlDatabase db_;
};

// Обороти по контрагентах
// Зведена таблиця дебіторської та кредиторської заборгованості:
// один рядок = один контрагент в межах вказаного субрахунку (361, 631 тощо).
struct CounterpartyTurnoverRow
{
  CounterpartyId counterpartyId;
  QString name;
  Money openingDebit = Money::zero();
  Money openingCredit = Money::zero();
  Money turnoverDebit = Money::zero();
  Money turnoverCredit = Money::zero();
  Money closingDebit = Money::zero();
  Money closingCredit = Money::zero();
};

struct CounterpartyTurnover
{
  AccountId accountId;
  QString accountCode;
  QString accountName;
  QDate from;
  QDate to;
  std::vector<CounterpartyTurnoverRow> rows;
  CounterpartyTurnoverRow totals;
};

class CounterpartyTurnoverReport
{
 public:
  explicit CounterpartyTurnoverReport(QSqlDatabase db);
  Result<CounterpartyTurnover> build(AccountId accountId, QDate from, QDate to) const;

 private:
  QSqlDatabase db_;
};

struct StockRow
{
  NomenclatureId nomenclatureId;
  QString name;
  QString sku;
  QString unit;
  Quantity openingQty = Quantity::zero();
  Money openingAmount = Money::zero();
  Quantity inQty = Quantity::zero();
  Money inAmount = Money::zero();
  Quantity outQty = Quantity::zero();
  Money outAmount = Money::zero();
  Quantity closingQty = Quantity::zero();
  Money closingAmount = Money::zero();
  Quantity reservedQty = Quantity::zero(); // зарезервовано в замовленнях покупців
  Quantity orderedQty = Quantity::zero(); // замовлено у постачальників
};

struct StockReportData
{
  QDate from;
  QDate to;
  WarehouseId warehouseId;
  QString warehouseName;
  std::vector<StockRow> rows;
  StockRow totals;
};

class StockReport
{
 public:
  explicit StockReport(QSqlDatabase db);
  Result<StockReportData> build(QDate from, QDate to,
                                std::optional<WarehouseId> warehouseId = std::nullopt) const;

 private:
  QSqlDatabase db_;
};

// Касова книга (форма КО-4)
// Стандартна форма касової книги: рядки з касовими ордерами та
// підсумкові рядки по кожному дню роботи каси (dayTotal = true).
// Закривальний залишок closing повинен збігатися з кінцевим сальдо 301 в ОСВ.
struct CashBookRow
{
  QDate date;
  DocumentId documentId;
  QString number;
  QString typeLabel;
  QString description;
  Money incoming = Money::zero();
  Money outgoing = Money::zero();
  Money balance = Money::zero(); // наростаючий залишок після рядка
  bool dayTotal = false; // рядок є підсумком дня (не касовим ордером)
};

struct CashBook
{
  QDate from;
  QDate to;
  Money opening = Money::zero();
  std::vector<CashBookRow> rows;
  Money incoming = Money::zero();
  Money outgoing = Money::zero();
  Money closing = Money::zero();
};

class CashBookReport
{
 public:
  explicit CashBookReport(QSqlDatabase db);
  // build() — за рахунком 301 (готівка в касі).
  Result<CashBook> build(QDate from, QDate to) const;
  // buildFor() — за довільним кодом рахунку (наприклад, для розрахункового субрахунку).
  Result<CashBook> buildFor(const char * accountCode, QDate from, QDate to) const;

 private:
  QSqlDatabase db_;
};

// Журнал касових ордерів — спрощена вибірка для швидкого перегляду,
// без наростаючого залишку та підсумків по днях.
struct CashJournalRow
{
  QDate date;
  DocumentId documentId;
  QString number;
  QString typeLabel;
  QString party;
  QString purpose;
  Money incoming = Money::zero();
  Money outgoing = Money::zero();
};

struct CashJournal
{
  QDate from;
  QDate to;
  std::vector<CashJournalRow> rows;
  Money incoming = Money::zero();
  Money outgoing = Money::zero();
};

class CashJournalReport
{
 public:
  explicit CashJournalReport(QSqlDatabase db);
  Result<CashJournal> build(QDate from, QDate to) const;

 private:
  QSqlDatabase db_;
};

// Фінансова звітність (НП(С)БО 1)
// StatementLine — один рядок Балансу або Звіту про фінансові результати
// з кодом (за формою НП(С)БО) та підсумковою сумою.
struct StatementLine
{
  QString code;
  QString name;
  Money amount = Money::zero();
};

// Звіт про фінансові результати (P&L):
// profit = goodsRevenue + serviceRevenue + otherIncome - cogs - admin
// Для незакритого місяця profit розраховується безпосередньо з оборотів;
// для закритого — включає сальдо документів PeriodResult.
struct IncomeStatement
{
  QDate from;
  QDate to;
  Money goodsRevenue = Money::zero();   // оборот Кт 701
  Money serviceRevenue = Money::zero(); // оборот Кт 703
  Money otherIncome = Money::zero();    // оборот Кт 71 + 714
  Money cogs = Money::zero();           // оборот Дт 902
  Money admin = Money::zero();          // оборот Дт 92 + 944
  Money profit = Money::zero();         // чистий результат
};

// Баланс (Звіт про фінансовий стан) на дату asOf.
// assets, equity, liabilities — списки рядків стандартної форми;
// assetsTotal == equityTotal + liabilitiesTotal (рівновага балансу).
struct BalanceSheet
{
  QDate asOf;
  std::vector<StatementLine> assets;
  Money assetsTotal = Money::zero();
  std::vector<StatementLine> equity;
  Money equityTotal = Money::zero();
  std::vector<StatementLine> liabilities;
  Money liabilitiesTotal = Money::zero();
};

class FinancialStatementReport
{
 public:
  explicit FinancialStatementReport(QSqlDatabase db);
  Result<IncomeStatement> income(QDate from, QDate to) const;
  Result<BalanceSheet> balance(QDate asOf) const;

 private:
  QSqlDatabase db_;
};

// Книга доходів (єдиний податок)
// Книга обліку доходів для платників єдиного податку (ФОП або юрособа
// 3-ї групи). Джерело: обороти рахунків 701 та 703 за датою.
// tax = income * rateBp / 10000 (5% або 3% для ПДВ-платника).
struct IncomeBookRow
{
  QDate date;
  QString number;
  QString typeLabel;
  QString party;
  QString account; // код субрахунку (701 або 703)
  Money amount = Money::zero();
};

struct IncomeBook
{
  QDate from;
  QDate to;
  TaxSystem taxSystem = TaxSystem::Simplified;
  int rateBp = 500; // ставка єдиного податку в базисних пунктах
  std::vector<IncomeBookRow> rows;
  Money income = Money::zero();
  Money tax = Money::zero();
};

class IncomeBookReport
{
 public:
  explicit IncomeBookReport(QSqlDatabase db);
  Result<IncomeBook> build(QDate from, QDate to) const;

 private:
  QSqlDatabase db_;
};

// Акт звірки розрахунків
// Документ для узгодження взаємозаборгованостей з конкретним контрагентом
// по обраному субрахунку (361, 631, 681, 371).
// Структура даних повністю збігається з AccountCard, але доповнена
// повними реквізитами контрагента для друкованої форми.
struct ReconciliationAct
{
  Counterparty party;
  AccountId accountId;
  QString accountCode;
  QString accountName;
  QDate from;
  QDate to;
  Money openingDebit = Money::zero();
  Money openingCredit = Money::zero();
  std::vector<AccountCardRow> rows;
  Money turnoverDebit = Money::zero();
  Money turnoverCredit = Money::zero();
  Money closingDebit = Money::zero();
  Money closingCredit = Money::zero();
};
class ReconciliationReport
{
 public:
  explicit ReconciliationReport(QSqlDatabase db);
  Result<ReconciliationAct> build(AccountId accountId, CounterpartyId party, QDate from,
                                  QDate to) const;

 private:
  QSqlDatabase db_;
};

// Відомість ПДВ
// Звіт для перевірки правильності нарахування ПДВ за місяць:
//   liability (643) — зобов'язання до реєстрації
//   credit    (644) — кредит до підтвердження
//   budget    (641) — чисте ПДВ до сплати в бюджет
// Звіт придатний для ручної звірки з Декларацією ПДВ (форма 0110).
struct VatStatement
{
  QDate from;
  QDate to;
  TrialBalanceRow liability; // 643 — зобов'язання
  TrialBalanceRow credit;    // 644 — кредит
  TrialBalanceRow budget;    // 641 — чисте ПДВ
};

class VatStatementReport
{
 public:
  explicit VatStatementReport(QSqlDatabase db);
  Result<VatStatement> build(QDate from, QDate to) const;

 private:
  QSqlDatabase db_;
};

// Управлінський звіт: рух коштів (каса 301 + банк 311) та рентабельність.
struct CashFlowCorrRow
{
  QString accountCode;
  QString accountName;
  Money incoming = Money::zero(); // надходження на 301/311
  Money outgoing = Money::zero(); // списання з 301/311
};

struct CashFlowStatement
{
  QDate from;
  QDate to;
  Money openingCash = Money::zero();
  Money openingBank = Money::zero();
  Money openingTotal = Money::zero();
  Money incoming = Money::zero();
  Money outgoing = Money::zero();
  Money closingCash = Money::zero();
  Money closingBank = Money::zero();
  Money closingTotal = Money::zero();
  std::vector<CashFlowCorrRow> byCorrespondence;
};

struct Profitability
{
  QDate from;
  QDate to;
  Money revenue = Money::zero(); // 701+703+71+714
  Money costs = Money::zero();   // 902+92+944
  Money profit = Money::zero();
  int marginBp = 0; // рентабельність у базисних пунктах (1500 = 15,00%)
};

class CashFlowReport
{
 public:
  explicit CashFlowReport(QSqlDatabase db);
  Result<CashFlowStatement> build(QDate from, QDate to) const;
  Result<Profitability> profitability(QDate from, QDate to) const;

 private:
  QSqlDatabase db_;
};

} // namespace fin
