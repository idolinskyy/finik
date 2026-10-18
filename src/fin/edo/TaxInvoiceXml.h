#pragma once

// XML податкової накладної (ЄРПН / M.E.Doc).

#include "fin/core/Result.h"
#include "fin/domain/Company.h"
#include "fin/domain/Counterparty.h"
#include "fin/domain/Document.h"
#include "fin/domain/Vat.h"

#include <QDate>
#include <QSqlDatabase>
#include <QString>

namespace fin {

// Спрощена структура, яка описує вміст розпарсованого XML-файлу ПН.
// Використовується для попереднього перегляду та зіставлення з БД.
struct ParsedTaxInvoiceXml
{
  QString number;     // номер ПН за формою ДПС
  QDate date;         // дата складання ПН
  QString sellerTin;  // ІПН/ЄДРПОУ продавця
  QString sellerName; // найменування продавця
  QString buyerTin;   // ІПН/ЄДРПОУ покупця
  QString buyerName;  // найменування покупця
  Money vat = Money::zero();
  int vatRateBp = 0; // ставка ПДВ у базисних пунктах
  Money net = Money::zero();
  QString sourceNumber; // номер первинного документа-підстави
  QString sourceType;   // тип первинного документа
};

// Результат імпорту XML ПН.
enum class EdoImportKind
{
  AttachedOutgoing, // XML прикріплено до існуючої вихідної ПН
  CreatedIncoming   // автоматично створено чернетку вхідної ПН
};

struct EdoImportResult
{
  DocumentId documentId;
  EdoImportKind kind = EdoImportKind::CreatedIncoming;
};

// Обчислює SHA-256 від рядка у кодуванні UTF-8, повертає hex-рядок.
// Зберігається в документі як edoSha256 для перевірки цілісності.
QString sha256HexUtf8(const QString & text);

// Формує XML-вміст Податкової накладної з документа, реквізитів компанії,
// контрагента та плану рахунків (для визначення напряму ПДВ).
Result<QString> buildTaxInvoiceXml(const Document & document, const CompanyProfile & company,
                                   const Counterparty & party,
                                   const std::vector<Account> & accounts);

// Розбирає XML-файл ПН у структуру ParsedTaxInvoiceXml.
// Повертає помилку, якщо формат не відповідає схемі ДПС.
Result<ParsedTaxInvoiceXml> parseTaxInvoiceXml(const QString & xml);

// Зберігає XML-вміст та хеш у полях edoXml і edoSha256 документа.
// imported = true означає, що XML отримано ззовні (від постачальника).
Status setDocumentEdoPayload(QSqlDatabase db, DocumentId id, const QString & xml, bool imported);

// Генерує XML для існуючої ПН і зберігає у файловій системі.
// Повертає шлях до збереженого файлу.
Result<QString> exportTaxInvoiceXml(QSqlDatabase db, DocumentId id);

// Імпортує XML-файл ПН від постачальника або прикріплює до власної ПН.
// Логіка: якщо ІПН продавця = ЄДРПОУ нашого підприємства →
//   AttachedOutgoing (знаходимо існуючу ПН і прикріплюємо XML).
// Інакше → CreatedIncoming (створюємо чернетку вхідної ПН).
Result<EdoImportResult> importTaxInvoiceXml(QSqlDatabase db, const QString & xml);

} // namespace fin
