#pragma once

// Друковані форми документів.

#include "fin/core/Result.h"
#include "fin/domain/Document.h"
#include "fin/domain/Employee.h"

#include <QSqlDatabase>
#include <QString>

namespace fin {

struct CashBook;
struct CashJournal;
struct TrialBalance;
struct AccountCard;
struct StockReportData;
struct IncomeStatement;
struct BalanceSheet;
struct IncomeBook;
struct ReconciliationAct;
struct VatStatement;
struct CounterpartyTurnover;

// Екранування спеціальних символів HTML для безпечної підстановки в шаблон
QString htmlEscape(const QString & text);

class PrintForms
{
 public:
  explicit PrintForms(QSqlDatabase db);

  // Первинні документи
  Result<QString> cashReceiptHtml(const Document & document) const; // ПКО (КО-1)
  Result<QString> cashVoucherHtml(const Document & document) const; // ВКО (КО-2)
  Result<QString> cashOrderHtml(const Document & document) const;
  Result<QString> deliveryNoteHtml(const Document & document) const; // Видаткова накладна
  Result<QString> purchaseNoteHtml(const Document & document) const; // Прибуткова накладна
  Result<QString> bankPaymentHtml(const Document & document) const; // Платіжна інструкція
  Result<QString> expenseReportHtml(const Document & document) const; // Авансовий звіт (№ 841)
  Result<QString> documentHtml(const Document & document) const; // Універсальний друк документа

  // Касові та облікові регістри
  Result<QString> cashBookHtml(const CashBook & book, const QString & title = QString()) const;
  Result<QString> cashBookCsv(const CashBook & book) const;
  Result<QString> cashJournalHtml(const CashJournal & journal) const;
  Result<QString> cashJournalCsv(const CashJournal & journal) const;

  // Бухгалтерська звітність
  Result<QString> trialBalanceHtml(const TrialBalance & report) const; // ОСВ
  Result<QString> trialBalanceCsv(const TrialBalance & report) const;
  Result<QString> accountCardHtml(const AccountCard & card) const; // Картка рахунку
  Result<QString> accountCardCsv(const AccountCard & card) const;
  Result<QString> stockHtml(const StockReportData & report) const; // Складські залишки
  Result<QString> stockCsv(const StockReportData & report) const;
  Result<QString>
  incomeStatementHtml(const IncomeStatement & report) const; // Звіт про фінрезультати
  Result<QString> balanceSheetHtml(const BalanceSheet & report) const; // Баланс
  Result<QString> incomeBookHtml(const IncomeBook & book) const; // Книга доходів ФОП
  Result<QString> reconciliationHtml(const ReconciliationAct & act) const; // Акт звірки
  Result<QString> vatStatementHtml(const VatStatement & report) const;     // Довідка ПДВ
  Result<QString> counterpartyTurnoverHtml(const CounterpartyTurnover & report) const;

  // Кадрові та зарплатні форми
  Result<QString> timesheetHtml(const Timesheet & ts) const; // Табель (П-5)
  Result<QString> payslipHtml(const PayrollLine & line, const Employee & emp,
                              QDate month) const; // Розрахунковий листок
  Result<QString> payrollSummaryHtml(const std::vector<PayrollLine> & lines,
                                     QDate month) const; // Розрахункова відомість (П-6)
  Result<QString> vacationOrderHtml(const VacationCalc & calc,
                                    const Employee & emp) const; // Наказ на відпустку (П-3)
  Result<QString> sickLeaveCalcHtml(const SickLeaveCalc & calc,
                                    const Employee & emp) const; // Розрахунок лікарняних
  Result<QString> hrOrderHtml(const HrOrder & order,
                              const Employee & emp) const; // Кадровий наказ (П-1, П-5, П-8)

 private:
  Result<QString> cashDocumentHtml(const Document & document, bool incoming) const;
  Result<QString> goodsNoteHtml(const Document & document, bool purchase) const;
  Result<QString> goodsActHtml(const Document & document) const;
  Result<QString> faDocumentHtml(const Document & document) const;
  Result<QString> iaDocumentHtml(const Document & document) const;
  Result<QString> msDocumentHtml(const Document & document) const;
  Result<QString> namedGoodsHtml(const Document & document, const QString & title,
                                 const QString & supplier, const QString & buyer,
                                 const QString & footnote) const;
  Result<QString> supplierServiceActHtml(const Document & document) const;

  QSqlDatabase db_;
};

} // namespace fin
