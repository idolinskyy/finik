#pragma once

// Первинний документ і рядки табличної частини.

#include "fin/core/Ids.h"
#include "fin/core/Money.h"
#include "fin/core/Quantity.h"

#include <optional>
#include <vector>

#include <QDate>
#include <QString>

namespace fin {

// Тип документа визначає:
//   1. Набір допустимих кореспонденцій рахунків (DocumentValidator)
//   2. Чи потрапляє документ у Головну книгу (requiresLedgerPosting)
//   3. Яку групу меню відображати в UI
enum class DocumentType
{
  OpeningBalance, // введення початкових залишків (рахунок 00)
  Manual,         // довільний бухгалтерський запис
  CashIn,         // прибутковий касовий ордер КО-1 (Дт 301)
  CashOut,        // видатковий касовий ордер КО-2 (Кт 301)
  Purchase,       // надходження товарів (Дт 281 Кт 631)
  Sale,           // реалізація товарів (Дт 361 Кт 703 + COGS)
  BankPayment,    // банківський платіж за випискою (Дт/Кт 311)
  SaleReturn,     // повернення від покупця (сторно реалізації)
  PurchaseReturn, // повернення постачальнику
  WriteOff,     // списання товарів (інвентаризаційна нестача)
  Stocktake,    // результат інвентаризації
  PeriodResult, // закриття місяця: 701/703→79→44
  ServiceSale,  // реалізація послуг (Дт 361 Кт 703)
  ServicePurchase, // надходження послуг/робіт (Дт 92/152/154 Кт 631)
  CustomerInvoice, // рахунок на оплату покупцю — НЕ проводиться
  FaCommission,    // введення ОЗ в експлуатацію (Дт 104 Кт 152)
  FaDepreciation,  // нарахування амортизації ОЗ (Дт 92 Кт 131)
  FaWriteOff,      // списання ОЗ (Дт 131 Кт 104)
  MsReceipt,       // надходження МШП на склад (рахунок 22)
  MsIssue,         // передача МШП в експлуатацію (Дт 92 Кт 22)
  IaCommission, // введення НМА в господарський оборот (Дт 125 Кт 154)
  IaDepreciation, // нарахування амортизації НМА (Дт 92 Кт 133)
  IaWriteOff,     // списання НМА (Дт 133 Кт 125)
  TaxInvoice,     // податкова накладна ПДВ (643/641/644)
  Payroll,        // нарахування зарплати (Дт 92 Кт 661/651/642/645)
  StockTransfer, // переміщення товарів між складами (Дт 281/склад2 Кт 281/склад1)
  KitAssembly,   // комплектація номенклатури (компоненти → комплект на 281)
  KitDisassembly, // розукомплектація номенклатури (комплект → компоненти на 281)
  CustomerOrder, // замовлення покупця (резервування, без проводок ГК)
  SupplierOrder, // замовлення постачальнику (без проводок ГК)
  MaterialIssue, // вимога-накладна на списання матеріалів у виробництво (Дт 231 Кт 201/202/203/281/22)
  ProductionReport, // звіт виробництва за зміну / випуск готової продукції (Дт 26 Кт 231)
  ExpenseReport, // авансовий звіт підзвітної особи (Дт 92/91/201/281/22/644 Кт 3721)
  VacationPayroll, // нарахування відпускних (Дт 92 Кт 661 + ЄСВ/ПДФО/ВЗ; КМУ № 100)
  SickLeavePayroll // нарахування лікарняних — частини роботодавця (перші 5 днів, Дт 92 Кт 661)
};

std::optional<DocumentType> documentTypeFromString(const QString & text);
QString documentTypeToString(DocumentType type);
QString documentTypeLabelUk(DocumentType type);
// Префікс номера документа: «КО-1», «РН», «ПН» тощо.
// Автоматично підставляється при виборі типу у формі.
QString documentTypePrefix(DocumentType type);

// Усі зареєстровані типи (для вичерпних тестів маршрутизації UI тощо).
std::vector<DocumentType> allDocumentTypes();

// Один рядок табличної частини документа.
// Прямо відповідає рядку в БД-таблиці document_lines.
// Поля-посилання (nomenclatureId, fixedAssetId тощо) заповнюються
// залежно від типу документа — для більшості рядків більшість з них нульові.
struct DocumentLine
{
  int lineNo = 0;
  AccountId debitAccountId;
  AccountId creditAccountId;
  Money amount = Money::zero();
  NomenclatureId nomenclatureId; // товар/послуга (для Sale, Purchase, WriteOff)
  FixedAssetId fixedAssetId;     // картка ОЗ (для FaCommission/Depreciation/WriteOff)
  LowValueId lowValueId;         // картка МШП (для MsReceipt/MsIssue)
  IntangibleId intangibleId;     // картка НМА (для IaCommission/Depreciation/WriteOff)
  EmployeeId employeeId;         // працівник (для Payroll)
  WarehouseId warehouseId; // склад рядка (якщо відрізняється від складу документа)
  CounterpartyId counterpartyId; // контрагент рядка (переоцінка 362/632; ЗС 361/631; інакше — з документа)
  ContractId contractId; // договір рядка (ЗС 361/631; інакше — з шапки документа)
  Quantity quantity = Quantity::zero();
  Money unitPrice = Money::zero();
  int vatRateBp = 0;         // ставка ПДВ у базисних пунктах (2000 = 20%)
  std::int64_t vatCents = 0; // сума ПДВ у копійках (обчислена, зберігається для звітності)
  // postingDate: для ПН-рядків реєстрації — дата реєстрації в ЄРПН,
  // що може відрізнятися від дати документа; для решти полів — невалідна.
  QDate postingDate;
  QString description;
};

// Заголовок документа.
// Поля EDO зберігають посилання на XML-файл та його SHA-256 хеш
// для подальшої перевірки цілісності при імпорті/експорті ПН.
struct Document
{
  DocumentId id;
  DocumentType type = DocumentType::Manual;
  QString number;
  QDate date;
  CounterpartyId counterpartyId;
  ContractId contractId; // договір контрагента (субконто 2)
  WarehouseId warehouseId; // основний склад документа / склад відправника
  WarehouseId targetWarehouseId; // цільовий склад (для StockTransfer)
  BomId bomId; // специфікація виробу (для ProductionReport / MaterialIssue)
  EmployeeId employeeId; // підзвітна особа / працівник (для ExpenseReport / Payroll)
  QString orderStatus; // статус замовлення: "draft", "confirmed", "completed", "cancelled"
  DocumentId basedOnId; // батьківський документ («На підставі»)
  QString comment;
  QString currencyCode; // код валюти ("USD", "EUR", "PLN"; порожній = "UAH")
  std::int64_t currencyRateMicros = 0; // курс НБУ в мікрогривнях за nominal одиниць
  int currencyNominal = 1;   // номінал курсу (1 або 100)
  QString receivedBy;        // отримавач (для КО-2)
  QString identityDoc;       // документ, що посвідчує особу (для КО-2)
  QDate vatRegistrationDate; // дата реєстрації ПН в ЄРПН (порожня = ще не зареєстрована)
  std::int64_t vatOffsetCents = 0; // сума заліку авансу 681→361 або 371→631
  QString edoXml;                  // шлях до збереженого XML-файлу ПН
  QString edoSha256; // SHA-256 хеш XML для перевірки цілісності
  bool edoImported = false; // true = ПН отримана від постачальника, імпортована
  // Checkbox ПРРО
  QString fiscalReceiptId; // UUID чека в Checkbox
  QString fiscalCode;      // фіскальний номер чека
  QString fiscalStatus;    // статус (CREATED / DONE / …)
  bool posted = false;
  std::vector<DocumentLine> lines;

  // Сума заголовку — підсумок за всіма рядками.
  // Для Sale містить суму з ПДВ, для Payroll — суму нарахованої зарплати.
  Money total() const;
};

// Спрощена проекція документа для відображення в Журналі документів.
// Не містить рядків — завантажується швидко без JOIN до document_lines.
struct DocumentHeader
{
  DocumentId id;
  DocumentType type = DocumentType::Manual;
  QString number;
  QDate date;
  CounterpartyId counterpartyId;
  QString counterpartyName; // денормалізована назва для відображення
  QString currencyCode;     // валюта документа
  bool posted = false;
  Money total = Money::zero();
  QString comment;
};

} // namespace fin
