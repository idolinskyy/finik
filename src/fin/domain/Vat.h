#pragma once

// ПДВ: ставки, перша подія, формування податкової накладної.

#include "fin/core/Money.h"
#include "fin/core/Result.h"
#include "fin/domain/Account.h"
#include "fin/domain/Company.h"
#include "fin/domain/Document.h"
#include "fin/domain/DocumentBlueprint.h"

#include <optional>
#include <vector>

#include <QDate>
#include <QSqlDatabase>

namespace fin {

// Стандартна ставка ПДВ 20% у базисних пунктах (1 bp = 0.01%).
inline constexpr int kDefaultVatRateBp = 2000;

// Напрям ПДВ: Output — вихідна ПН (продаж), Input — вхідна ПН (купівля).
enum class VatDirection
{
  Output,
  Input
};

// Чи передбачає тип документа комерційний ПДВ (тобто чи виникає
// податкове зобов'язання або кредит при проведенні)?
bool usesCommercialVat(DocumentType type);

// Чи може цей тип документа бути підставою для Податкової накладної
// (у меню «На підставі → Податкова накладна»)?
bool usesTaxInvoiceForm(DocumentType type);

// Чи є документ «першою подією» для вихідного ПДВ-зобов'язання?
// true для Sale, ServiceSale, CashIn (на суму з ПДВ).
bool isVatAdvanceSource(DocumentType type);

// Чи є документ «першою подією» для ПДВ-кредиту?
// true для Purchase, ServicePurchase, CashOut (передоплата постачальнику).
bool isVatReturnSource(DocumentType type);

// Визначає напрям ПДВ для документа: Output або Input.
// Повертає std::nullopt для документів без ПДВ.
std::optional<VatDirection> vatDirectionFromSource(DocumentType type);

// Чи є рядок автоматично згенерованим рядком реєстрації ПН
// (643→641 або 641→644)? Такі рядки видаляються перед повторним
// проведенням і не відображаються у формі документа.
bool isVatRegistrationLine(const std::vector<Account> & accounts, const DocumentLine & line);

bool isVatObligationLine(const std::vector<Account> & accounts, const DocumentLine & line);

// Видаляє з document.lines рядки реєстрації ПДВ (643/644↔641).
// Викликається перед кожним post() та registerTaxInvoice() для «чистого» старту.
void stripVatRegistrationLines(const std::vector<Account> & accounts, Document & document);

// Перевіряє чи є проведена ПН «на підставі» документа sourceId.
// excludeId дозволяє виключити саму ПН з перевірки (при re-register).
Result<bool> hasPostedTaxInvoiceBasedOn(QSqlDatabase db, DocumentId sourceId, DocumentId excludeId);

// Арифметика ПДВ

// Обчислює суму ПДВ від суми без ПДВ: net * rateBp / 10000.
// Наприклад: vatOnNet(100_00, 2000) → 20_00 (копійок).
Result<Money> vatOnNet(Money net, int rateBp);

// Виділяє ПДВ із суми з ПДВ: gross * rateBp / (10000 + rateBp).
Result<Money> vatIncludedInGross(Money gross, int rateBp);

// Обчислює суму без ПДВ зі суми з ПДВ: gross - vatIncludedInGross(gross).
Result<Money> netFromGross(Money gross, int rateBp);

// Обчислює суму з ПДВ зі суми без ПДВ: net + vatOnNet(net).
Result<Money> grossFromNet(Money net, int rateBp);

// Проставляє суму ПДВ та ставку у поля рядка line відповідно до net.
Status stampVatOnNet(DocumentLine & line, int rateBp);

// Проставляє суми ПДВ у рядках накладної постачальника (вхідна ПН).
Status stampSupplierVat(std::vector<DocumentLine> & lines, const std::vector<Account> & accounts,
                        int rateBp);

// Сума ПДВ по всіх товарних рядках (для заголовку ПН).
Money goodsVatTotal(const std::vector<GoodsLine> & rows);

// Загальна сума з ПДВ по товарних рядках.
Money goodsGrossTotal(const std::vector<GoodsLine> & rows);

// Формування Податкової накладної

// Складає чернетку ПН на основі первинного документа-підстави (source).
// Копіює контрагента, рядки, суми ПДВ, встановлює зв'язок basedOnId.
// excludeId — вже існуюча ПН по цьому документу, яку не враховувати
// при визначенні залишку до виставлення.
Result<Document> composeTaxInvoice(QSqlDatabase db, const std::vector<Account> & accounts,
                                   const Document & source, const CompanyProfile & company,
                                   QDate date, DocumentId excludeId = {});

// Формує рядки Головної книги для реєстрації ПН в ЄРПН:
//   Вихідна ПН: Дт 643 Кт 641 (списання проміжного ПДВ-зобов'язання)
//   Вхідна ПН: Дт 641 Кт 644 (підтвердження ПДВ-кредиту)
// Рядки мають postingDate = vatRegistrationDate (може ≠ даті ПН).
Result<std::vector<DocumentLine>> vatRegistrationLines(const std::vector<Account> & accounts,
                                                       const Document & document);

} // namespace fin
