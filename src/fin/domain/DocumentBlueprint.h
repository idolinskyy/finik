#pragma once

// Шаблони проводок за типом документа та введення «На підставі».

#include "fin/domain/Account.h"
#include "fin/domain/Document.h"
#include "fin/core/Result.h"

#include <vector>

#include <QSqlDatabase>

namespace fin {

// Структурований рядок товарних документів (купівля, продаж, накладна)
struct GoodsLine
{
  NomenclatureId nomenclatureId;
  Quantity quantity = Quantity::zero();
  Money unitPrice = Money::zero();
  Money amount = Money::zero();
  int vatRateBp = 0;
  std::int64_t vatCents = 0;
  QString description;
};

// Рядок касового документа
struct CashLine
{
  AccountId correspondenceId;
  Money amount = Money::zero();
  int vatRateBp = 0;
  std::int64_t vatCents = 0;
  QString description;
};

// Рядок банківської виписки
struct BankLine
{
  AccountId correspondenceId;
  Money amount = Money::zero();
  QString description;
};

// Рядок введення початкових грошових залишків
struct OpeningMoneyLine
{
  AccountId accountId;
  Money debit = Money::zero();
  Money credit = Money::zero();
};

// Залишок товару на складі для інвентаризації
struct StockBalance
{
  NomenclatureId nomenclatureId;
  Quantity quantity = Quantity::zero();
  Money amount = Money::zero();
};

// Рядок авансового звіту підзвітної особи
struct ExpenseLine
{
  AccountId expenseAccountId; // Рахунок витрат/ТМЦ (92, 91, 231, 93, 201, 281, 22, 631 тощо)
  NomenclatureId nomenclatureId; // Номенклатура (якщо ТМЦ/матеріали)
  Quantity quantity = Quantity::zero();
  Money unitPrice = Money::zero();
  Money amount = Money::zero(); // Сума витрат без ПДВ (або загальна)
  int vatRateBp = 0;            // Ставка ПДВ (2000 = 20%)
  std::int64_t vatCents = 0;    // Сума ПДВ
  QString documentRef; // Документ-підтвердження (номер чека, квитанція)
  QString description; // Призначення витрат
};

AccountId accountIdByCode(const std::vector<Account> & accounts, const char * code);
bool accountHasCode(const std::vector<Account> & accounts, AccountId id, const char * code);

// Визначення відповідної форми UI для типу документа
bool usesGoodsForm(DocumentType type);
bool usesCashForm(DocumentType type);
bool usesBankForm(DocumentType type);
bool usesOpeningForm(DocumentType type);
bool usesServicePurchaseForm(DocumentType type);
bool isServicePurchaseDebitCode(const QString & code);
bool usesExpenseReportForm(DocumentType type);

// Який редактор відкриває DocumentDialog::openDocument (без HR-винятку для Manual).
enum class DocumentEditorKind
{
  Goods,
  Cash,
  Bank,
  ServicePurchase,
  Opening,
  FaCommission,
  FaDepreciation,
  FaWriteOff,
  IaCommission,
  IaDepreciation,
  IaWriteOff,
  Ms,
  TaxInvoice,
  VacationPayroll,
  SickLeavePayroll,
  Payroll,
  ExpenseReport,
  Generic
};

DocumentEditorKind documentEditorKind(DocumentType type);
QString documentEditorKindLabel(DocumentEditorKind kind);

// Прапорці доступності типових друкованих форм
bool canPrintCashReceipt(DocumentType type);
bool canPrintCashVoucher(DocumentType type);
bool canPrintCashOrder(DocumentType type);
bool canPrintDeliveryNote(DocumentType type);
bool canPrintPurchaseNote(DocumentType type);
bool canPrintBankPayment(DocumentType type);
bool canPrintServiceAct(DocumentType type);
bool canPrintSupplierServiceAct(DocumentType type);
bool canPrintCustomerInvoice(DocumentType type);
bool canPrintExpenseReport(DocumentType type);
bool canPrintDocument(DocumentType type);
bool requiresLedgerPosting(DocumentType type);

// SQL-фрагмент: типи без проведення в ГЛ (рахунки, замовлення).
inline QString sqlNonLedgerDocumentTypes()
{
  return QStringLiteral("'customer_invoice','customer_order','supplier_order'");
}

// Перелік допустимих цільових документів для створення «На підставі»
std::vector<DocumentType> basedOnTargets(DocumentType source);

// Створює новий документ-чернетку на підставі первинного документа
Result<Document> buildBasedOn(const Document & source, DocumentType target,
                              const std::vector<Account> & accounts, QSqlDatabase db = {});

// Проводки за замовчуванням при створенні нового документа
std::vector<DocumentLine> defaultLinesForType(DocumentType type,
                                              const std::vector<Account> & accounts);
void accountLocksForType(DocumentType type, bool & lockDebit, bool & lockCredit);

// Генерація проводок за типами операцій
Result<std::vector<DocumentLine>> composePurchase(const std::vector<Account> & accounts,
                                                  const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeSale(const std::vector<Account> & accounts,
                                              const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeCashIn(const std::vector<Account> & accounts,
                                                const std::vector<CashLine> & rows);
Result<std::vector<DocumentLine>> composeCashOut(const std::vector<Account> & accounts,
                                                 const std::vector<CashLine> & rows);
Result<std::vector<DocumentLine>> composeBank(const std::vector<Account> & accounts, bool incoming,
                                              const std::vector<BankLine> & rows);
Result<std::vector<DocumentLine>> composeOpening(const std::vector<Account> & accounts,
                                                 const std::vector<OpeningMoneyLine> & money,
                                                 const std::vector<GoodsLine> & goods);
Result<std::vector<DocumentLine>> composeSaleReturn(const std::vector<Account> & accounts,
                                                    const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composePurchaseReturn(const std::vector<Account> & accounts,
                                                        const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeWriteOff(const std::vector<Account> & accounts,
                                                  const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeStocktake(const std::vector<Account> & accounts,
                                                   const std::vector<GoodsLine> & counted,
                                                   const std::vector<StockBalance> & onHand);
Result<std::vector<DocumentLine>> composeServiceSale(const std::vector<Account> & accounts,
                                                     const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeServicePurchase(const std::vector<Account> & accounts,
                                                         const std::vector<CashLine> & rows);
Result<std::vector<DocumentLine>> composeCustomerInvoice(const std::vector<Account> & accounts,
                                                         const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeStockTransfer(const std::vector<Account> & accounts,
                                                       const std::vector<GoodsLine> & rows);
// Рядок 1 — комплект, рядки 2+ — компоненти (Дт 281 Кт 281).
Result<std::vector<DocumentLine>> composeKitAssembly(const std::vector<Account> & accounts,
                                                     const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeKitDisassembly(const std::vector<Account> & accounts,
                                                        const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeCustomerOrder(const std::vector<Account> & accounts,
                                                       const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeSupplierOrder(const std::vector<Account> & accounts,
                                                       const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeMaterialIssue(const std::vector<Account> & accounts,
                                                       const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeProductionReport(const std::vector<Account> & accounts,
                                                          const std::vector<GoodsLine> & rows);
Result<std::vector<DocumentLine>> composeExpenseReport(const std::vector<Account> & accounts,
                                                       const std::vector<ExpenseLine> & rows,
                                                       AccountId creditAccountId = AccountId{0});

// Витяг спеціалізованих рядків з універсальних проводок для UI
std::vector<GoodsLine> extractGoodsLines(DocumentType type, const std::vector<DocumentLine> & lines,
                                         const std::vector<Account> & accounts);
std::vector<GoodsLine> extractStocktakeCounted(const std::vector<DocumentLine> & lines,
                                               const std::vector<Account> & accounts,
                                               const std::vector<StockBalance> & onHand);
std::vector<CashLine> extractCashLines(DocumentType type, const std::vector<DocumentLine> & lines,
                                       const std::vector<Account> & accounts);
std::vector<CashLine> extractServicePurchaseLines(const std::vector<DocumentLine> & lines,
                                                  const std::vector<Account> & accounts);
std::vector<ExpenseLine> extractExpenseLines(const std::vector<DocumentLine> & lines,
                                             const std::vector<Account> & accounts);
std::vector<BankLine> extractBankLines(const std::vector<DocumentLine> & lines,
                                       const std::vector<Account> & accounts);
std::vector<OpeningMoneyLine> extractOpeningMoneyLines(const std::vector<DocumentLine> & lines,
                                                       const std::vector<Account> & accounts);
bool bankDocumentIncoming(const std::vector<DocumentLine> & lines,
                          const std::vector<Account> & accounts);
Money saleRevenueTotal(const std::vector<DocumentLine> & lines,
                       const std::vector<Account> & accounts);
Money goodsLineTotal(const std::vector<GoodsLine> & rows);
Money expenseLineTotal(const std::vector<ExpenseLine> & rows);

} // namespace fin
