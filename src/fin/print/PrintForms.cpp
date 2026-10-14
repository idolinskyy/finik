#include "fin/print/PrintForms.h"

#include "fin/db/AccountRepository.h"
#include "fin/db/CompanyRepository.h"
#include "fin/db/ContractRepository.h"
#include "fin/db/CounterpartyRepository.h"
#include "fin/db/EmployeeRepository.h"
#include "fin/db/FixedAssetRepository.h"
#include "fin/db/IntangibleAssetRepository.h"
#include "fin/db/LowValueRepository.h"
#include "fin/db/NomenclatureRepository.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"
#include "fin/domain/FixedAsset.h"
#include "fin/domain/IntangibleAsset.h"
#include "fin/domain/LowValueItem.h"
#include "fin/domain/Vat.h"
#include "fin/print/AmountInWords.h"
#include "fin/reports/Reports.h"

namespace fin {
namespace {

enum class Paper
{
  Portrait,
  Landscape
};

QString printCss(bool landscape)
{
  const QString size = landscape ? QStringLiteral("9.5pt") : QStringLiteral("11pt");
  return QStringLiteral("body{font-family:'Times New Roman',Times,serif;font-size:%1;"
                        "color:#000;margin:0;padding:0;}"
                        "h1{font-size:14pt;text-align:center;margin:0 0 4px 0;}"
                        "h2{font-size:11pt;text-align:center;margin:0 0 8px 0;font-weight:normal;}"
                        ".formcode{text-align:right;font-size:9pt;margin:0 0 6px 0;}"
                        ".meta{margin:2px 0;}"
                        "table{width:100%;border-collapse:separate;border-spacing:0;"
                        "margin:6px 0;}"
                        "th,td{border:1px solid #000;padding:3px 5px;vertical-align:middle;}"
                        "th{text-align:center;font-weight:bold;}"
                        "td.num,th.num{text-align:right;white-space:nowrap;}"
                        "td.pair{width:50%;vertical-align:top;}"
                        "table.sheet{border:none;}"
                        "table.sheet>tbody>tr>td{border:none;vertical-align:top;width:50%;"
                        "padding:0 10px;}"
                        ".tear{border-left:1px dashed #000;padding-left:14px;}"
                        ".words{margin:8px 0;}"
                        ".sign{margin-top:16px;}"
                        ".muted{color:#333;font-size:9pt;margin-top:8px;}")
      .arg(size);
}

QString wrapHtml(const QString & title, const QString & body, Paper paper)
{
  const QString meta = paper == Paper::Landscape
                           ? QStringLiteral("<meta name=\"fin-page\" content=\"landscape\">")
                           : QStringLiteral("<meta name=\"fin-page\" content=\"portrait\">");
  return QStringLiteral("<!DOCTYPE html><html lang=\"uk\"><head><meta charset=\"utf-8\">"
                        "%1<title>%2</title><style>%3</style></head><body>%4</body></html>")
      .arg(meta, htmlEscape(title), printCss(paper == Paper::Landscape), body);
}

QString tableOpen()
{
  return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"3\">");
}

QString contractPurpose(QSqlDatabase db, ContractId id)
{
  if (!id) {
    return {};
  }
  const Result<Contract> row = ContractRepository(db).findById(id);
  if (!row) {
    return {};
  }
  QString text = QStringLiteral("договір № %1").arg(row.value().number.trimmed());
  if (row.value().date.isValid()) {
    text += QStringLiteral(" від %1").arg(row.value().date.toString(QStringLiteral("dd.MM.yyyy")));
  }
  return text;
}

QString withContractPurpose(const QString & purpose, const QString & contractText)
{
  if (contractText.isEmpty()) {
    return purpose;
  }
  if (purpose.contains(contractText, Qt::CaseInsensitive)) {
    return purpose;
  }
  if (purpose.trimmed().isEmpty()) {
    return contractText;
  }
  return purpose.trimmed() + QStringLiteral("; ") + contractText;
}

QString counterpartyName(QSqlDatabase db, CounterpartyId id)
{
  if (!id) {
    return {};
  }
  const Result<Counterparty> row = CounterpartyRepository(db).findById(id);
  if (!row) {
    return {};
  }
  QString name = row.value().name;
  if (!row.value().edrpou.trimmed().isEmpty()) {
    name += QStringLiteral(" (ЄДРПОУ %1)").arg(row.value().edrpou.trimmed());
  }
  return name;
}

QString counterpartyBank(QSqlDatabase db, CounterpartyId id)
{
  if (!id) {
    return {};
  }
  const Result<Counterparty> row = CounterpartyRepository(db).findById(id);
  if (!row) {
    return {};
  }
  QString line;
  if (!row.value().iban.trimmed().isEmpty()) {
    line += QStringLiteral("IBAN %1").arg(row.value().iban.trimmed());
  }
  if (!row.value().bankName.trimmed().isEmpty()) {
    if (!line.isEmpty()) {
      line += QStringLiteral(", ");
    }
    line += row.value().bankName.trimmed();
  }
  return line;
}

QString accountLabel(QSqlDatabase db, AccountId id)
{
  if (!id) {
    return {};
  }
  const Result<Account> account = AccountRepository(db).findById(id);
  if (!account) {
    return {};
  }
  return account.value().code + QStringLiteral("  ") + account.value().name;
}

QString csvCell(const QString & text)
{
  if (!text.contains(QLatin1Char(';')) && !text.contains(QLatin1Char('"')) &&
      !text.contains(QLatin1Char('\n'))) {
    return text;
  }
  QString escaped = text;
  escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
  return QStringLiteral("\"%1\"").arg(escaped);
}

QString csvMoney(Money value)
{
  return csvCell(value.isZero() ? QString() : value.toDisplay());
}

QString dashIfEmpty(const QString & text)
{
  const QString trimmed = text.trimmed();
  return trimmed.isEmpty() ? QStringLiteral("_________________") : trimmed;
}

QString companyBlock(const CompanyProfile & company)
{
  QString block = QStringLiteral("<div class=\"meta\">Підприємство: %1</div>")
                      .arg(htmlEscape(company.heading()));
  if (!company.address.trimmed().isEmpty()) {
    block += QStringLiteral("<div class=\"meta\">Адреса: %1</div>")
                 .arg(htmlEscape(company.address.trimmed()));
  }
  return block;
}

QString bankLine(const CompanyProfile & company)
{
  QString line;
  if (!company.iban.trimmed().isEmpty()) {
    line += QStringLiteral("IBAN %1").arg(company.iban.trimmed());
  }
  if (!company.mfo.trimmed().isEmpty()) {
    if (!line.isEmpty()) {
      line += QStringLiteral(", ");
    }
    line += QStringLiteral("МФО %1").arg(company.mfo.trimmed());
  }
  if (!company.bankName.trimmed().isEmpty()) {
    if (!line.isEmpty()) {
      line += QStringLiteral(", ");
    }
    line += company.bankName.trimmed();
  }
  return line;
}

QString signatures(const CompanyProfile & company)
{
  return QStringLiteral("<div class=\"sign\">Головний бухгалтер %1 &nbsp;&nbsp; "
                        "Касир %2</div>")
      .arg(htmlEscape(dashIfEmpty(company.accountant)), htmlEscape(dashIfEmpty(company.cashier)));
}

Result<Money> sumCashLines(const std::vector<CashLine> & rows)
{
  Money total = Money::zero();
  for (const CashLine & row : rows) {
    const Result<Money> next = total.plus(row.amount);
    if (!next) {
      return next;
    }
    total = next.value();
  }
  return Result<Money>::ok(total);
}

} // namespace

QString htmlEscape(const QString & text)
{
  QString out;
  out.reserve(text.size());
  for (QChar c : text) {
    if (c == QLatin1Char('&')) {
      out += QStringLiteral("&amp;");
    } else if (c == QLatin1Char('<')) {
      out += QStringLiteral("&lt;");
    } else if (c == QLatin1Char('>')) {
      out += QStringLiteral("&gt;");
    } else if (c == QLatin1Char('"')) {
      out += QStringLiteral("&quot;");
    } else {
      out += c;
    }
  }
  return out;
}

PrintForms::PrintForms(QSqlDatabase db) : db_(std::move(db)) {}

Result<QString> PrintForms::cashReceiptHtml(const Document & document) const
{
  return cashDocumentHtml(document, true);
}

Result<QString> PrintForms::cashVoucherHtml(const Document & document) const
{
  return cashDocumentHtml(document, false);
}

Result<QString> PrintForms::cashOrderHtml(const Document & document) const
{
  if (canPrintCashReceipt(document.type)) {
    return cashReceiptHtml(document);
  }
  if (canPrintCashVoucher(document.type)) {
    return cashVoucherHtml(document);
  }
  return Result<QString>::fail(
      Error::validation(QStringLiteral("касовий ордер друкується лише для ПКО або ВКО")));
}

Result<QString> PrintForms::cashDocumentHtml(const Document & document, bool incoming) const
{
  if (incoming && !canPrintCashReceipt(document.type)) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("ПКО друкується лише для прибуткового касового ордера")));
  }
  if (!incoming && !canPrintCashVoucher(document.type)) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("ВКО друкується лише для видаткового касового ордера")));
  }
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return Result<QString>::fail(accounts.error());
  }
  const std::vector<CashLine> rows =
      extractCashLines(document.type, document.lines, accounts.value());
  const Result<Money> total = sumCashLines(rows);
  if (!total) {
    return Result<QString>::fail(total.error());
  }
  const Result<QString> words = moneyToUkrainianWords(total.value());
  if (!words) {
    return Result<QString>::fail(words.error());
  }

  const QString title = incoming ? QStringLiteral("Прибутковий касовий ордер")
                                 : QStringLiteral("Видатковий касовий ордер");
  const QString partyLabel = incoming ? QStringLiteral("Прийнято від") : QStringLiteral("Видано");
  const QString party = counterpartyName(db_, document.counterpartyId);
  QString linesHtml = tableOpen();
  linesHtml +=
      QStringLiteral("<tr><th width=\"40%\">Кореспонденція</th><th width=\"20%\">Сума, грн</th>"
                     "<th width=\"40%\">Зміст</th></tr>");
  for (const CashLine & row : rows) {
    linesHtml += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td><td>%3</td></tr>")
                     .arg(htmlEscape(accountLabel(db_, row.correspondenceId)),
                          htmlEscape(row.amount.toDisplay()), htmlEscape(row.description));
  }
  linesHtml +=
      QStringLiteral("<tr><td><b>Разом</b></td><td class=\"num\"><b>%1</b></td><td></td></tr>")
          .arg(htmlEscape(total.value().toDisplay()));
  linesHtml += QStringLiteral("</table>");

  auto orderBody = [&](bool stub) {
    QString part;
    part += QStringLiteral("<div class=\"formcode\">%1</div>")
                .arg(htmlEscape(incoming ? QStringLiteral("Типова форма № КО-1")
                                         : QStringLiteral("Типова форма № КО-2")));
    part += QStringLiteral("<h1>%1</h1>")
                .arg(htmlEscape(stub && incoming ? QStringLiteral("Квитанція до ПКО") : title));
    part += QStringLiteral("<h2>№ %1 від %2</h2>")
                .arg(htmlEscape(document.number),
                     htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
    part += companyBlock(company.value());
    part += QStringLiteral("<div class=\"meta\">%1: %2</div>")
                .arg(htmlEscape(partyLabel), htmlEscape(dashIfEmpty(party)));
    part += linesHtml;
    part += QStringLiteral("<div class=\"words\">Сума прописом: %1</div>")
                .arg(htmlEscape(words.value()));
    if (!document.comment.trimmed().isEmpty()) {
      part += QStringLiteral("<div class=\"meta\">Підстава: %1</div>")
                  .arg(htmlEscape(document.comment));
    }
    if (!incoming) {
      part += QStringLiteral("<div class=\"meta\">Одержав: %1</div>")
                  .arg(htmlEscape(dashIfEmpty(document.receivedBy)));
      part += QStringLiteral("<div class=\"meta\">Документ, що посвідчує особу: %1</div>")
                  .arg(htmlEscape(dashIfEmpty(document.identityDoc)));
    }
    part += signatures(company.value());
    return part;
  };

  QString body;
  Paper paper = Paper::Portrait;
  if (incoming) {
    paper = Paper::Landscape;
    body += QStringLiteral("<table class=\"sheet\" width=\"100%\"><tr>");
    body += QStringLiteral("<td>%1</td>").arg(orderBody(false));
    body += QStringLiteral("<td class=\"tear\">%1</td>").arg(orderBody(true));
    body += QStringLiteral("</tr></table>");
    body += QStringLiteral("<p class=\"muted\">Внутрішній бланк за Положенням НБУ № 148: ордер і "
                           "відривна квитанція. Проведення — Дт 301 і вказана кореспонденція.</p>");
  } else {
    body = orderBody(false);
    body += QStringLiteral("<p class=\"muted\">Внутрішній бланк за Положенням НБУ № 148. "
                           "Проведення — Кт 301 і вказана кореспонденція.</p>");
  }
  const QString fileTitle =
      (incoming ? QStringLiteral("ПКО %1") : QStringLiteral("ВКО %1")).arg(document.number);
  return Result<QString>::ok(wrapHtml(fileTitle, body, paper));
}

Result<QString> PrintForms::deliveryNoteHtml(const Document & document) const
{
  return goodsNoteHtml(document, false);
}

Result<QString> PrintForms::purchaseNoteHtml(const Document & document) const
{
  return goodsNoteHtml(document, true);
}

Result<QString> PrintForms::goodsNoteHtml(const Document & document, bool purchase) const
{
  if (purchase && !canPrintPurchaseNote(document.type)) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("прибуткова накладна друкується лише для надходження")));
  }
  if (!purchase && !canPrintDeliveryNote(document.type)) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("накладна друкується лише для реалізації")));
  }
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return Result<QString>::fail(accounts.error());
  }
  const std::vector<GoodsLine> rows =
      extractGoodsLines(document.type, document.lines, accounts.value());
  const Money net = document.type == DocumentType::Sale
                        ? saleRevenueTotal(document.lines, accounts.value())
                        : goodsLineTotal(rows);
  const Money vat = goodsVatTotal(rows);
  const Result<Money> gross = net.plus(vat);
  if (!gross) {
    return Result<QString>::fail(gross.error());
  }
  const Result<QString> words = moneyToUkrainianWords(gross.value());
  if (!words) {
    return Result<QString>::fail(words.error());
  }

  QString title = QStringLiteral("Накладна");
  switch (document.type) {
  case DocumentType::Purchase:
    title = QStringLiteral("Прибуткова накладна");
    break;
  case DocumentType::PurchaseReturn:
    title = QStringLiteral("Накладна на повернення постачальнику");
    break;
  case DocumentType::Sale:
    title = QStringLiteral("Видаткова накладна");
    break;
  case DocumentType::SaleReturn:
    title = QStringLiteral("Накладна на повернення від покупця");
    break;
  case DocumentType::StockTransfer:
    title = QStringLiteral("Накладна на внутрішнє переміщення");
    break;
  case DocumentType::KitAssembly:
    title = QStringLiteral("Акт комплектації номенклатури");
    break;
  case DocumentType::KitDisassembly:
    title = QStringLiteral("Акт розукомплектації номенклатури");
    break;
  case DocumentType::MaterialIssue:
    title = QStringLiteral("Вимога-накладна");
    break;
  case DocumentType::ProductionReport:
    title = QStringLiteral("Акт випуску продукції");
    break;
  default:
    break;
  }
  const QString party = counterpartyName(db_, document.counterpartyId);
  QString body;
  body += QStringLiteral("<h1>%1</h1>").arg(htmlEscape(title));
  body += QStringLiteral("<h2>№ %1 від %2</h2>")
              .arg(htmlEscape(document.number),
                   htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
  if (purchase) {
    body += QStringLiteral("<div class=\"meta\">Постачальник: %1</div>")
                .arg(htmlEscape(dashIfEmpty(party)));
    body += QStringLiteral("<div class=\"meta\">Покупець: %1</div>")
                .arg(htmlEscape(company.value().heading()));
  } else {
    body += QStringLiteral("<div class=\"meta\">Постачальник: %1</div>")
                .arg(htmlEscape(company.value().heading()));
    body += QStringLiteral("<div class=\"meta\">Покупець: %1</div>")
                .arg(htmlEscape(dashIfEmpty(party)));
  }
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"6%\">№</th><th width=\"40%\">Номенклатура</th>"
                         "<th width=\"8%\">Од.</th><th width=\"14%\">К-сть</th>"
                         "<th width=\"16%\">Ціна</th><th width=\"16%\">Сума</th></tr>");
  NomenclatureRepository items(db_);
  int n = 0;
  for (const GoodsLine & row : rows) {
    ++n;
    QString name = QStringLiteral("—");
    QString unit = QStringLiteral("шт");
    if (row.nomenclatureId) {
      const Result<Nomenclature> item = items.findById(row.nomenclatureId);
      if (item) {
        name = item.value().name;
        if (!item.value().sku.isEmpty()) {
          name = item.value().sku + QStringLiteral("  ") + name;
        }
        unit = item.value().unit;
      }
    }
    body += QStringLiteral("<tr><td class=\"num\">%1</td><td>%2</td><td>%3</td>"
                           "<td class=\"num\">%4</td><td class=\"num\">%5</td>"
                           "<td class=\"num\">%6</td></tr>")
                .arg(QString::number(n), htmlEscape(name), htmlEscape(unit),
                     htmlEscape(row.quantity.toDisplay()), htmlEscape(row.unitPrice.toDisplay()),
                     htmlEscape(row.amount.toDisplay()));
  }
  body += QStringLiteral("<tr><td colspan=\"5\"><b>Разом без ПДВ</b></td>"
                         "<td class=\"num\"><b>%1</b></td></tr>")
              .arg(htmlEscape(net.toDisplay()));
  if (!vat.isZero()) {
    body += QStringLiteral("<tr><td colspan=\"5\">ПДВ</td>"
                           "<td class=\"num\">%1</td></tr>")
                .arg(htmlEscape(vat.toDisplay()));
    body += QStringLiteral("<tr><td colspan=\"5\"><b>Разом з ПДВ</b></td>"
                           "<td class=\"num\"><b>%1</b></td></tr>")
                .arg(htmlEscape(gross.value().toDisplay()));
  }
  body += QStringLiteral("</table>");
  body +=
      QStringLiteral("<div class=\"words\">Сума прописом: %1</div>").arg(htmlEscape(words.value()));
  if (!document.comment.trimmed().isEmpty()) {
    body +=
        QStringLiteral("<div class=\"meta\">Примітка: %1</div>").arg(htmlEscape(document.comment));
  }
  body += QStringLiteral("<div class=\"sign\">Відвантажив %1 &nbsp;&nbsp; "
                         "Отримав _________________</div>")
              .arg(htmlEscape(dashIfEmpty(company.value().cashier)));
  QString footnote;
  switch (document.type) {
  case DocumentType::Purchase:
    footnote = QStringLiteral("Сума — надходження товару (Дт 281 Кт 631).");
    break;
  case DocumentType::PurchaseReturn:
    footnote = QStringLiteral("Повернення постачальнику: Дт 631 Кт 281 за середньою.");
    break;
  case DocumentType::SaleReturn:
    footnote = QStringLiteral("Сума накладної — сторно доходу (Дт 701 Кт 361). "
                              "Повернення на 281 — за середньою при проведенні.");
    break;
  default:
    footnote = QStringLiteral("Сума накладної — продаж (Дт 361 Кт 701). "
                              "Собівартість (Дт 902 Кт 281) рахується при проведенні і в "
                              "накладну не входить.");
    break;
  }
  body += QStringLiteral("<p class=\"muted\">%1</p>").arg(htmlEscape(footnote));
  return Result<QString>::ok(
      wrapHtml(QStringLiteral("Накладна %1").arg(document.number), body, Paper::Portrait));
}

Result<QString> PrintForms::cashBookHtml(const CashBook & book, const QString & title) const
{
  const QString heading =
      title.trimmed().isEmpty() ? QStringLiteral("Касова книга") : title.trimmed();
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  if (heading.contains(QStringLiteral("Касова"))) {
    body += QStringLiteral("<div class=\"formcode\">%1</div>")
                .arg(htmlEscape(QStringLiteral("Типова форма № КО-4")));
  }
  body += QStringLiteral("<h1>%1</h1>").arg(htmlEscape(heading));
  body += QStringLiteral("<h2>%1 — %2</h2>")
              .arg(htmlEscape(book.from.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(book.to.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  body += QStringLiteral("<div class=\"meta\">Залишок на початок: %1</div>")
              .arg(htmlEscape(book.opening.toDisplay()));
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"12%\">Дата</th><th width=\"12%\">Номер</th>"
                         "<th width=\"28%\">Документ</th><th width=\"16%\">Надійшло</th>"
                         "<th width=\"16%\">Вибуло</th><th width=\"16%\">Залишок</th></tr>");
  for (const CashBookRow & row : book.rows) {
    const QString dateText =
        row.dayTotal ? QString() : row.date.toString(QStringLiteral("dd.MM.yyyy"));
    const QString label = row.dayTotal ? QStringLiteral("<b>%1</b>").arg(htmlEscape(row.typeLabel))
                                       : htmlEscape(row.typeLabel);
    body += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td>"
                           "<td class=\"num\">%4</td><td class=\"num\">%5</td>"
                           "<td class=\"num\">%6</td></tr>")
                .arg(htmlEscape(dateText), htmlEscape(row.number), label,
                     htmlEscape(row.incoming.isZero() ? QString() : row.incoming.toDisplay()),
                     htmlEscape(row.outgoing.isZero() ? QString() : row.outgoing.toDisplay()),
                     htmlEscape(row.balance.toDisplay()));
  }
  body += QStringLiteral("<tr><td colspan=\"3\"><b>Разом за період</b></td>"
                         "<td class=\"num\"><b>%1</b></td><td class=\"num\"><b>%2</b></td>"
                         "<td class=\"num\"><b>%3</b></td></tr>")
              .arg(htmlEscape(book.incoming.toDisplay()), htmlEscape(book.outgoing.toDisplay()),
                   htmlEscape(book.closing.toDisplay()));
  body += QStringLiteral("</table>");
  body += signatures(company.value());
  return Result<QString>::ok(wrapHtml(heading, body, Paper::Landscape));
}

Result<QString> PrintForms::documentHtml(const Document & document) const
{
  if (canPrintCashOrder(document.type)) {
    return cashOrderHtml(document);
  }
  if (canPrintDeliveryNote(document.type)) {
    return deliveryNoteHtml(document);
  }
  if (canPrintPurchaseNote(document.type)) {
    return purchaseNoteHtml(document);
  }
  if (canPrintBankPayment(document.type)) {
    return bankPaymentHtml(document);
  }
  if (canPrintExpenseReport(document.type)) {
    return expenseReportHtml(document);
  }
  if (canPrintSupplierServiceAct(document.type)) {
    return supplierServiceActHtml(document);
  }
  if (canPrintServiceAct(document.type) || canPrintCustomerInvoice(document.type) ||
      document.type == DocumentType::CustomerOrder ||
      document.type == DocumentType::SupplierOrder) {
    const Result<CompanyProfile> company = CompanyRepository(db_).load();
    if (!company) {
      return Result<QString>::fail(company.error());
    }
    const QString ours = company.value().heading();
    const QString party = counterpartyName(db_, document.counterpartyId);
    if (canPrintServiceAct(document.type)) {
      return namedGoodsHtml(
          document, QStringLiteral("Акт наданих послуг"), ours, dashIfEmpty(party),
          QStringLiteral("Сума акта — дохід від послуг (Дт 361 Кт 703). Складу немає."));
    }
    if (document.type == DocumentType::CustomerOrder) {
      return namedGoodsHtml(
          document, QStringLiteral("Замовлення покупця"), ours, dashIfEmpty(party),
          QStringLiteral("Замовлення не проводиться в Головну книгу. Далі — рахунок або "
                         "реалізація («На підставі»)."));
    }
    if (document.type == DocumentType::SupplierOrder) {
      return namedGoodsHtml(
          document, QStringLiteral("Замовлення постачальнику"), dashIfEmpty(party), ours,
          QStringLiteral("Замовлення не проводиться в Головну книгу. Далі — надходження або "
                         "оплата («На підставі»)."));
    }
    return namedGoodsHtml(
        document, QStringLiteral("Рахунок на оплату"), ours, dashIfEmpty(party),
        QStringLiteral("Рахунок не проводиться в журнал. Після оплати — банківський "
                       "платіж або реалізація."));
  }
  if (document.type == DocumentType::WriteOff || document.type == DocumentType::Stocktake) {
    return goodsActHtml(document);
  }
  if (usesFaForm(document.type)) {
    return faDocumentHtml(document);
  }
  if (usesIaForm(document.type)) {
    return iaDocumentHtml(document);
  }
  if (usesMsForm(document.type)) {
    return msDocumentHtml(document);
  }
  if (usesTaxInvoiceForm(document.type)) {
    const Result<CompanyProfile> company = CompanyRepository(db_).load();
    if (!company) {
      return Result<QString>::fail(company.error());
    }
    const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
    if (!accounts) {
      return Result<QString>::fail(accounts.error());
    }
    Money vat = Money::zero();
    for (const DocumentLine & line : document.lines) {
      if (!isVatObligationLine(accounts.value(), line)) {
        continue;
      }
      const Result<Money> next = vat.plus(line.amount);
      if (!next) {
        return Result<QString>::fail(next.error());
      }
      vat = next.value();
    }
    const Result<QString> words = moneyToUkrainianWords(vat);
    if (!words) {
      return Result<QString>::fail(words.error());
    }
    QString body;
    body += QStringLiteral("<h1>Податкова накладна</h1>");
    body += QStringLiteral("<h2>№ %1 від %2</h2>")
                .arg(htmlEscape(document.number),
                     htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
    body += QStringLiteral("<div class=\"meta\">Постачальник / продавець: %1</div>")
                .arg(htmlEscape(company.value().heading()));
    body += QStringLiteral("<div class=\"meta\">Покупець: %1</div>")
                .arg(htmlEscape(dashIfEmpty(counterpartyName(db_, document.counterpartyId))));
    if (document.vatRegistrationDate.isValid()) {
      body +=
          QStringLiteral("<div class=\"meta\">Дата реєстрації в ЄРПН: %1</div>")
              .arg(htmlEscape(document.vatRegistrationDate.toString(QStringLiteral("dd.MM.yyyy"))));
    } else {
      body += QStringLiteral("<div class=\"meta\">Не зареєстровано в ЄРПН (643/644).</div>");
    }
    body += tableOpen();
    body += QStringLiteral("<tr><th>Зміст</th><th class=\"num\">ПДВ</th></tr>");
    for (const DocumentLine & line : document.lines) {
      if (!isVatObligationLine(accounts.value(), line)) {
        continue;
      }
      body += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td></tr>")
                  .arg(htmlEscape(line.description), htmlEscape(line.amount.toDisplay()));
    }
    body += QStringLiteral("<tr><td><b>Разом ПДВ</b></td><td class=\"num\"><b>%1</b></td></tr>")
                .arg(htmlEscape(vat.toDisplay()));
    body += QStringLiteral("</table>");
    body += QStringLiteral("<div class=\"words\">Сума прописом: %1</div>")
                .arg(htmlEscape(words.value()));
    body += QStringLiteral("<p class=\"muted\">Податкова накладна проводить 643 або 644. "
                           "Реєстрація переносить суму на 641. XML експортується для Кабінету / "
                           "M.E.doc; Фінік не зберігає ключ КЕП і не підписує файл.</p>");
    return Result<QString>::ok(wrapHtml(
        QStringLiteral("Податкова накладна %1").arg(document.number), body, Paper::Portrait));
  }
  if (isPayrollFamily(document.type)) {
    const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
    if (!accounts) {
      return Result<QString>::fail(accounts.error());
    }
    std::vector<PayrollLine> rows = extractPayrollLines(document.lines, accounts.value());
    EmployeeRepository staff(db_);
    for (PayrollLine & row : rows) {
      if (row.name.isEmpty() && row.employeeId) {
        const Result<Employee> employee = staff.findById(row.employeeId);
        if (employee) {
          row.name = employee.value().name;
        }
      }
    }
    return payrollSummaryHtml(rows, document.date);
  }
  return Result<QString>::fail(
      Error::validation(QStringLiteral("для цього документа немає друкованої форми")));
}

Result<QString> PrintForms::bankPaymentHtml(const Document & document) const
{
  if (!canPrintBankPayment(document.type)) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("платіжка друкується лише для банківського платежу")));
  }
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return Result<QString>::fail(accounts.error());
  }
  const bool incoming = bankDocumentIncoming(document.lines, accounts.value());
  const std::vector<BankLine> rows = extractBankLines(document.lines, accounts.value());
  Money total = Money::zero();
  for (const BankLine & row : rows) {
    const Result<Money> next = total.plus(row.amount);
    if (!next) {
      return Result<QString>::fail(next.error());
    }
    total = next.value();
  }
  const Result<QString> words = moneyToUkrainianWords(total);
  if (!words) {
    return Result<QString>::fail(words.error());
  }
  const QString title =
      incoming ? QStringLiteral("Зарахування на рахунок") : QStringLiteral("Платіжна інструкція");
  const QString ours = company.value().heading();
  const QString party = counterpartyName(db_, document.counterpartyId);
  const QString contractText = contractPurpose(db_, document.contractId);
  const QString payer = incoming ? dashIfEmpty(party) : ours;
  const QString payee = incoming ? ours : dashIfEmpty(party);
  QString body;
  body += QStringLiteral("<div class=\"formcode\">%1</div>")
              .arg(htmlEscape(QStringLiteral("Інструкція НБУ № 163")));
  body += QStringLiteral("<h1>%1</h1>").arg(htmlEscape(title));
  body += QStringLiteral("<h2>№ %1 від %2</h2>")
              .arg(htmlEscape(document.number),
                   htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
  if (!contractText.isEmpty()) {
    body += QStringLiteral("<div class=\"meta\"><b>Договір:</b> %1</div>")
                .arg(htmlEscape(contractText));
  }
  body += tableOpen();
  const QString ourBank = bankLine(company.value());
  const QString partyBank = counterpartyBank(db_, document.counterpartyId);
  const QString payerBank = incoming ? partyBank : ourBank;
  const QString payeeBank = incoming ? ourBank : partyBank;
  QString payerCell = htmlEscape(payer);
  if (!payerBank.isEmpty()) {
    payerCell += QStringLiteral("<br/>") + htmlEscape(payerBank);
  }
  QString payeeCell = htmlEscape(payee);
  if (!payeeBank.isEmpty()) {
    payeeCell += QStringLiteral("<br/>") + htmlEscape(payeeBank);
  }
  body += QStringLiteral("<tr><td class=\"pair\"><b>Платник</b><br/>%1</td>"
                         "<td class=\"pair\"><b>Одержувач</b><br/>%2</td></tr>")
              .arg(payerCell, payeeCell);
  body += QStringLiteral("</table>");
  if (!company.value().address.trimmed().isEmpty() && !incoming) {
    body += QStringLiteral("<div class=\"meta\">Адреса платника: %1</div>")
                .arg(htmlEscape(company.value().address.trimmed()));
  }
  body += tableOpen();
  body += QStringLiteral(
      "<tr><th width=\"75%\">Призначення платежу</th><th width=\"25%\">Сума, грн</th></tr>");
  for (const BankLine & row : rows) {
    body += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td></tr>")
                .arg(htmlEscape(withContractPurpose(row.description, contractText)),
                     htmlEscape(row.amount.toDisplay()));
  }
  body += QStringLiteral("<tr><td><b>Разом</b></td><td class=\"num\"><b>%1</b></td></tr>")
              .arg(htmlEscape(total.toDisplay()));
  body += QStringLiteral("</table>");
  body +=
      QStringLiteral("<div class=\"words\">Сума прописом: %1</div>").arg(htmlEscape(words.value()));
  const QString purposeComment = withContractPurpose(document.comment, contractText);
  if (!purposeComment.trimmed().isEmpty()) {
    body += QStringLiteral("<div class=\"meta\">Призначення: %1</div>")
                .arg(htmlEscape(purposeComment));
  }
  body += signatures(company.value());
  body += incoming ? QStringLiteral("<p class=\"muted\">Внутрішній бланк зарахування. Проведення — "
                                    "Дт 311 Кт 361.</p>")
                   : QStringLiteral("<p class=\"muted\">Внутрішній бланк платіжної інструкції за "
                                    "Інструкцією НБУ № 163 (зі змінами постановою Правління НБУ "
                                    "від 26.02.2026 № 20). Проведення — Дт 631 Кт 311.</p>");
  return Result<QString>::ok(
      wrapHtml(title + QLatin1Char(' ') + document.number, body, Paper::Landscape));
}

Result<QString> PrintForms::expenseReportHtml(const Document & document) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return Result<QString>::fail(accounts.error());
  }
  const auto accountCode = [&](AccountId id) {
    for (const Account & account : accounts.value()) {
      if (account.id == id) {
        return account.code;
      }
    }
    return QString();
  };

  const std::vector<ExpenseLine> rows = extractExpenseLines(document.lines, accounts.value());
  const Money total = expenseLineTotal(rows);
  const Result<QString> words = moneyToUkrainianWords(total);
  if (!words) {
    return Result<QString>::fail(words.error());
  }

  QString empName = QStringLiteral("—");
  QString empTaxId;
  if (document.employeeId) {
    EmployeeRepository employees(db_);
    const Result<Employee> emp = employees.findById(document.employeeId);
    if (emp) {
      empName = emp.value().name;
      empTaxId = emp.value().taxId;
    }
  }

  Money totalNet = Money::zero();
  Money totalVat = Money::zero();
  for (const ExpenseLine & r : rows) {
    if (const auto n = totalNet.plus(r.amount))
      totalNet = n.value();
    if (r.vatCents > 0) {
      if (const auto v = totalVat.plus(Money::fromCents(r.vatCents)))
        totalVat = v.value();
    }
  }

  QString body;
  body += QStringLiteral(
      "<div class=\"formcode\">Типова форма № 841 (Наказ Мінфіну від 28.09.2015 № 841)</div>");
  body +=
      QStringLiteral("<h1>Звіт про використання коштів, виданих на відрядження або під звіт</h1>");
  body += QStringLiteral("<h2>№ %1 від %2</h2>")
              .arg(htmlEscape(document.number),
                   htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  body += QStringLiteral("<div class=\"meta\"><b>Підзвітна особа:</b> %1</div>")
              .arg(htmlEscape(empName));
  if (!empTaxId.isEmpty()) {
    body += QStringLiteral("<div class=\"meta\"><b>РНОКПП (ІПН):</b> %1</div>")
                .arg(htmlEscape(empTaxId));
  }
  if (!document.comment.trimmed().isEmpty()) {
    body += QStringLiteral("<div class=\"meta\"><b>Призначення авансу:</b> %1</div>")
                .arg(htmlEscape(document.comment));
  }

  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"5%\">№</th><th width=\"15%\">Документ</th>"
                         "<th width=\"25%\">Зміст витрат</th><th width=\"10%\">Дебет</th>"
                         "<th width=\"10%\">Кредит</th><th width=\"12%\">Сума без ПДВ</th>"
                         "<th width=\"11%\">ПДВ</th><th width=\"12%\">Всього</th></tr>");

  NomenclatureRepository items(db_);
  int n = 0;
  for (const ExpenseLine & row : rows) {
    ++n;
    QString desc = row.description;
    if (row.nomenclatureId) {
      const Result<Nomenclature> item = items.findById(row.nomenclatureId);
      if (item) {
        if (!desc.isEmpty())
          desc += QStringLiteral(" (") + item.value().name + QStringLiteral(")");
        else
          desc = item.value().name;
      }
    }
    const QString dtCode = accountCode(row.expenseAccountId);
    const QString ctCode = QStringLiteral("3721");
    const Money lineVat = Money::fromCents(row.vatCents);
    Money lineTotal = row.amount;
    if (const auto lt = lineTotal.plus(lineVat)) {
      lineTotal = lt.value();
    }
    body += QStringLiteral(
                "<tr><td class=\"num\">%1</td><td>%2</td><td>%3</td>"
                "<td class=\"num\">%4</td><td class=\"num\">%5</td>"
                "<td class=\"num\">%6</td><td class=\"num\">%7</td><td class=\"num\">%8</td></tr>")
                .arg(QString::number(n), htmlEscape(dashIfEmpty(row.documentRef)),
                     htmlEscape(dashIfEmpty(desc)), htmlEscape(dtCode), htmlEscape(ctCode),
                     htmlEscape(row.amount.toDisplay()),
                     htmlEscape(lineVat.isZero() ? QStringLiteral("—") : lineVat.toDisplay()),
                     htmlEscape(lineTotal.toDisplay()));
  }
  body += QStringLiteral("<tr><td colspan=\"5\"><b>Всього витрат</b></td>"
                         "<td class=\"num\"><b>%1</b></td><td class=\"num\"><b>%2</b></td>"
                         "<td class=\"num\"><b>%3</b></td></tr>")
              .arg(htmlEscape(totalNet.toDisplay()), htmlEscape(totalVat.toDisplay()),
                   htmlEscape(total.toDisplay()));
  body += QStringLiteral("</table>");

  body += QStringLiteral("<div class=\"words\"><b>Затверджена сума витрат прописом:</b> %1</div>")
              .arg(htmlEscape(words.value()));

  body +=
      QStringLiteral("<table width=\"100%\" style=\"margin-top:20px;\"><tr>"
                     "<td width=\"50%\">Звіт склав (підзвітна особа): _______________ / %1 /</td>"
                     "<td width=\"50%\">Перевірив (бухгалтер): _______________</td>"
                     "</tr><tr>"
                     "<td colspan=\"2\" style=\"padding-top:15px;\">Звіт затверджено в сумі %2: "
                     "_______________ / Керівник /</td>"
                     "</tr></table>")
          .arg(htmlEscape(empName), htmlEscape(total.toDisplay()));

  body += QStringLiteral(
      "<p class=\"muted\">Авансовий звіт проводить витрати (Дт 92/91/93/201/281 Кт 3721) "
      "та податковий кредит за чеками (Дт 6442 Кт 3721). Повернення залишку авансу або виплата "
      "перевитрати "
      "оформлюється через касу (ПКО/ВКО) або банк.</p>");

  return Result<QString>::ok(
      wrapHtml(QStringLiteral("Авансовий звіт %1").arg(document.number), body, Paper::Portrait));
}

Result<QString> PrintForms::cashBookCsv(const CashBook & book) const
{
  QString csv = QStringLiteral("Дата;Номер;Документ;Надійшло;Вибуло;Залишок\n");
  csv +=
      QStringLiteral("%1;;Залишок на початок;;;%2\n")
          .arg(csvCell(book.from.toString(QStringLiteral("dd.MM.yyyy"))), csvMoney(book.opening));
  for (const CashBookRow & row : book.rows) {
    csv += QStringLiteral("%1;%2;%3;%4;%5;%6\n")
               .arg(csvCell(row.date.toString(QStringLiteral("dd.MM.yyyy"))), csvCell(row.number),
                    csvCell(row.typeLabel), csvMoney(row.incoming), csvMoney(row.outgoing),
                    csvMoney(row.balance));
  }
  csv += QStringLiteral(";;Разом;%1;%2;%3\n")
             .arg(csvMoney(book.incoming), csvMoney(book.outgoing), csvMoney(book.closing));
  return Result<QString>::ok(csv);
}

Result<QString> PrintForms::goodsActHtml(const Document & document) const
{
  if (document.type != DocumentType::WriteOff && document.type != DocumentType::Stocktake) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("акт друкується для списання або інвентаризації")));
  }
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return Result<QString>::fail(accounts.error());
  }
  const std::vector<GoodsLine> rows =
      extractGoodsLines(document.type, document.lines, accounts.value());
  const Money total = goodsLineTotal(rows);
  const Result<QString> words = moneyToUkrainianWords(total);
  if (!words) {
    return Result<QString>::fail(words.error());
  }
  const QString title = document.type == DocumentType::WriteOff
                            ? QStringLiteral("Акт списання товарів")
                            : QStringLiteral("Акт інвентаризації (розбіжності)");
  QString body;
  body += QStringLiteral("<h1>%1</h1>").arg(htmlEscape(title));
  body += QStringLiteral("<h2>№ %1 від %2</h2>")
              .arg(htmlEscape(document.number),
                   htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"6%\">№</th><th width=\"40%\">Номенклатура</th>"
                         "<th width=\"8%\">Од.</th><th width=\"14%\">К-сть</th>"
                         "<th width=\"16%\">Ціна</th><th width=\"16%\">Сума</th></tr>");
  NomenclatureRepository items(db_);
  int n = 0;
  for (const GoodsLine & row : rows) {
    ++n;
    QString name = QStringLiteral("—");
    QString unit = QStringLiteral("шт");
    if (row.nomenclatureId) {
      const Result<Nomenclature> item = items.findById(row.nomenclatureId);
      if (item) {
        name = item.value().name;
        if (!item.value().sku.isEmpty()) {
          name = item.value().sku + QStringLiteral("  ") + name;
        }
        unit = item.value().unit;
      }
    }
    body += QStringLiteral("<tr><td class=\"num\">%1</td><td>%2</td><td>%3</td>"
                           "<td class=\"num\">%4</td><td class=\"num\">%5</td>"
                           "<td class=\"num\">%6</td></tr>")
                .arg(QString::number(n), htmlEscape(name), htmlEscape(unit),
                     htmlEscape(row.quantity.toDisplay()), htmlEscape(row.unitPrice.toDisplay()),
                     htmlEscape(row.amount.toDisplay()));
  }
  body += QStringLiteral("<tr><td colspan=\"5\"><b>Разом</b></td>"
                         "<td class=\"num\"><b>%1</b></td></tr>")
              .arg(htmlEscape(total.toDisplay()));
  body += QStringLiteral("</table>");
  body +=
      QStringLiteral("<div class=\"words\">Сума прописом: %1</div>").arg(htmlEscape(words.value()));
  if (!document.comment.trimmed().isEmpty()) {
    body +=
        QStringLiteral("<div class=\"meta\">Примітка: %1</div>").arg(htmlEscape(document.comment));
  }
  body += signatures(company.value());
  body += document.type == DocumentType::WriteOff
              ? QStringLiteral(
                    "<p class=\"muted\">Списання з 281 на 92 за середньою при проведенні.</p>")
              : QStringLiteral(
                    "<p class=\"muted\">Надлишок — Дт 281 Кт 71, нестача — Дт 92 Кт 281.</p>");
  return Result<QString>::ok(
      wrapHtml(title + QLatin1Char(' ') + document.number, body, Paper::Portrait));
}

Result<QString> PrintForms::faDocumentHtml(const Document & document) const
{
  if (!usesFaForm(document.type)) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("форма друкується для документів основних засобів")));
  }
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return Result<QString>::fail(accounts.error());
  }
  const auto accountCode = [&](AccountId id) {
    for (const Account & account : accounts.value()) {
      if (account.id == id) {
        return account.code;
      }
    }
    return QStringLiteral("—");
  };
  Money total = Money::zero();
  for (const DocumentLine & line : document.lines) {
    const Result<Money> next = total.plus(line.amount);
    if (!next) {
      return Result<QString>::fail(next.error());
    }
    total = next.value();
  }
  const Result<QString> words = moneyToUkrainianWords(total);
  if (!words) {
    return Result<QString>::fail(words.error());
  }
  const bool commission = usesFaCommissionForm(document.type);
  const bool writeOff = usesFaWriteOffForm(document.type);
  const QString title = commission ? QStringLiteral("Акт введення в експлуатацію")
                                   : (writeOff ? QStringLiteral("Акт списання основних засобів")
                                               : QStringLiteral("Відомість амортизації ОЗ"));
  QString body;
  body += QStringLiteral("<h1>%1</h1>").arg(htmlEscape(title));
  body += QStringLiteral("<h2>№ %1 від %2</h2>")
              .arg(htmlEscape(document.number),
                   htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  if (document.counterpartyId) {
    body += QStringLiteral("<div class=\"meta\">Контрагент: %1</div>")
                .arg(htmlEscape(dashIfEmpty(counterpartyName(db_, document.counterpartyId))));
  }
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"6%\">№</th><th width=\"18%\">Інв. №</th>"
                         "<th width=\"36%\">Назва</th><th width=\"12%\">Рахунок</th>"
                         "<th width=\"28%\">Сума</th></tr>");
  FixedAssetRepository assets(db_);
  int n = 0;
  for (const DocumentLine & line : document.lines) {
    ++n;
    QString inv = QStringLiteral("—");
    QString name = line.description;
    if (line.fixedAssetId) {
      const Result<FixedAsset> asset = assets.findById(line.fixedAssetId);
      if (asset) {
        inv = asset.value().inventoryNo;
        name = asset.value().name;
      }
    }
    const QString code = accountCode(line.debitAccountId);
    body += QStringLiteral("<tr><td class=\"num\">%1</td><td>%2</td><td>%3</td>"
                           "<td class=\"num\">%4</td><td class=\"num\">%5</td></tr>")
                .arg(QString::number(n), htmlEscape(inv), htmlEscape(name), htmlEscape(code),
                     htmlEscape(line.amount.toDisplay()));
  }
  body += QStringLiteral("<tr><td colspan=\"4\"><b>Разом</b></td>"
                         "<td class=\"num\"><b>%1</b></td></tr>")
              .arg(htmlEscape(total.toDisplay()));
  body += QStringLiteral("</table>");
  body +=
      QStringLiteral("<div class=\"words\">Сума прописом: %1</div>").arg(htmlEscape(words.value()));
  if (!document.comment.trimmed().isEmpty()) {
    body +=
        QStringLiteral("<div class=\"meta\">Примітка: %1</div>").arg(htmlEscape(document.comment));
  }
  body += signatures(company.value());
  if (commission) {
    body += QStringLiteral("<p class=\"muted\">Дт 104/105/109 Кт 631, 152 або 00. "
                           "Знос на початок — Дт 00 Кт 131. Амортизація з наступного місяця.</p>");
  } else if (writeOff) {
    body +=
        QStringLiteral("<p class=\"muted\">Дт 131 Кт 10x — знос; Дт витрат Кт 10x — залишкова. "
                       "Місяць вибуття амортизується, якщо відомість за цей місяць ще не проведено "
                       "(НП(С)БО 7).</p>");
  } else {
    body += QStringLiteral("<p class=\"muted\">Прямолінійна амортизація: Дт витрат Кт 131. "
                           "Останній день місяця, один проведений документ на місяць.</p>");
  }
  return Result<QString>::ok(
      wrapHtml(title + QLatin1Char(' ') + document.number, body, Paper::Portrait));
}

Result<QString> PrintForms::iaDocumentHtml(const Document & document) const
{
  if (!usesIaForm(document.type)) {
    return Result<QString>::fail(Error::validation(
        QStringLiteral("форма друкується для документів нематеріальних активів")));
  }
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return Result<QString>::fail(accounts.error());
  }
  const auto accountCode = [&](AccountId id) {
    for (const Account & account : accounts.value()) {
      if (account.id == id) {
        return account.code;
      }
    }
    return QStringLiteral("—");
  };
  Money total = Money::zero();
  for (const DocumentLine & line : document.lines) {
    const Result<Money> next = total.plus(line.amount);
    if (!next) {
      return Result<QString>::fail(next.error());
    }
    total = next.value();
  }
  const Result<QString> words = moneyToUkrainianWords(total);
  if (!words) {
    return Result<QString>::fail(words.error());
  }
  const bool commission = usesIaCommissionForm(document.type);
  const bool writeOff = usesIaWriteOffForm(document.type);
  const QString title = commission
                            ? QStringLiteral("Акт введення НМА в експлуатацію")
                            : (writeOff ? QStringLiteral("Акт списання нематеріальних активів")
                                        : QStringLiteral("Відомість амортизації НМА"));
  QString body;
  body += QStringLiteral("<h1>%1</h1>").arg(htmlEscape(title));
  body += QStringLiteral("<h2>№ %1 від %2</h2>")
              .arg(htmlEscape(document.number),
                   htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  if (document.counterpartyId) {
    body += QStringLiteral("<div class=\"meta\">Контрагент: %1</div>")
                .arg(htmlEscape(dashIfEmpty(counterpartyName(db_, document.counterpartyId))));
  }
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"6%\">№</th><th width=\"18%\">Інв. №</th>"
                         "<th width=\"36%\">Назва</th><th width=\"12%\">Рахунок</th>"
                         "<th width=\"28%\">Сума</th></tr>");
  IntangibleAssetRepository assets(db_);
  int n = 0;
  for (const DocumentLine & line : document.lines) {
    ++n;
    QString inv = QStringLiteral("—");
    QString name = line.description;
    if (line.intangibleId) {
      const Result<IntangibleAsset> asset = assets.findById(line.intangibleId);
      if (asset) {
        inv = asset.value().inventoryNo;
        name = asset.value().name;
      }
    }
    const QString code = accountCode(line.debitAccountId);
    body += QStringLiteral("<tr><td class=\"num\">%1</td><td>%2</td><td>%3</td>"
                           "<td class=\"num\">%4</td><td class=\"num\">%5</td></tr>")
                .arg(QString::number(n), htmlEscape(inv), htmlEscape(name), htmlEscape(code),
                     htmlEscape(line.amount.toDisplay()));
  }
  body += QStringLiteral("<tr><td colspan=\"4\"><b>Разом</b></td>"
                         "<td class=\"num\"><b>%1</b></td></tr>")
              .arg(htmlEscape(total.toDisplay()));
  body += QStringLiteral("</table>");
  body +=
      QStringLiteral("<div class=\"words\">Сума прописом: %1</div>").arg(htmlEscape(words.value()));
  if (!document.comment.trimmed().isEmpty()) {
    body +=
        QStringLiteral("<div class=\"meta\">Примітка: %1</div>").arg(htmlEscape(document.comment));
  }
  body += signatures(company.value());
  if (commission) {
    body += QStringLiteral("<p class=\"muted\">Дт 125/127 Кт 631, 154 або 00. "
                           "Знос на початок — Дт 00 Кт 133. Амортизація з наступного місяця "
                           "(НП(С)БО 8).</p>");
  } else if (writeOff) {
    body +=
        QStringLiteral("<p class=\"muted\">Дт 133 Кт 12x — знос; Дт витрат Кт 12x — залишкова. "
                       "Місяць вибуття амортизується, якщо відомість за цей місяць ще не проведено "
                       "(НП(С)БО 8).</p>");
  } else {
    body += QStringLiteral("<p class=\"muted\">Прямолінійна амортизація: Дт витрат Кт 133. "
                           "Останній день місяця, один проведений документ на місяць.</p>");
  }
  return Result<QString>::ok(
      wrapHtml(title + QLatin1Char(' ') + document.number, body, Paper::Portrait));
}

Result<QString> PrintForms::msDocumentHtml(const Document & document) const
{
  if (!usesMsForm(document.type)) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("форма друкується для надходження або передачі МШП")));
  }
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  Money total = Money::zero();
  for (const DocumentLine & line : document.lines) {
    const Result<Money> next = total.plus(line.amount);
    if (!next) {
      return Result<QString>::fail(next.error());
    }
    total = next.value();
  }
  const Result<QString> words = moneyToUkrainianWords(total);
  if (!words) {
    return Result<QString>::fail(words.error());
  }
  const bool receipt = usesMsReceiptForm(document.type);
  const QString title = receipt ? QStringLiteral("Акт надходження МШП")
                                : QStringLiteral("Акт передачі МШП в експлуатацію");
  QString body;
  body += QStringLiteral("<h1>%1</h1>").arg(htmlEscape(title));
  body += QStringLiteral("<h2>№ %1 від %2</h2>")
              .arg(htmlEscape(document.number),
                   htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  if (document.counterpartyId) {
    body += QStringLiteral("<div class=\"meta\">Постачальник: %1</div>")
                .arg(htmlEscape(dashIfEmpty(counterpartyName(db_, document.counterpartyId))));
  }
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"6%\">№</th><th width=\"16%\">Інв. №</th>"
                         "<th width=\"34%\">Назва</th><th width=\"12%\">К-сть</th>"
                         "<th width=\"32%\">Сума</th></tr>");
  LowValueRepository items(db_);
  int n = 0;
  for (const DocumentLine & line : document.lines) {
    ++n;
    QString inv = QStringLiteral("—");
    QString name = line.description;
    if (line.lowValueId) {
      const Result<LowValueItem> item = items.findById(line.lowValueId);
      if (item) {
        inv = item.value().inventoryNo;
        name = item.value().name;
      }
    }
    body += QStringLiteral("<tr><td class=\"num\">%1</td><td>%2</td><td>%3</td>"
                           "<td class=\"num\">%4</td><td class=\"num\">%5</td></tr>")
                .arg(QString::number(n), htmlEscape(inv), htmlEscape(name),
                     htmlEscape(line.quantity.toDisplay()), htmlEscape(line.amount.toDisplay()));
  }
  body += QStringLiteral("<tr><td colspan=\"4\"><b>Разом</b></td>"
                         "<td class=\"num\"><b>%1</b></td></tr>")
              .arg(htmlEscape(total.toDisplay()));
  body += QStringLiteral("</table>");
  body +=
      QStringLiteral("<div class=\"words\">Сума прописом: %1</div>").arg(htmlEscape(words.value()));
  if (!document.comment.trimmed().isEmpty()) {
    body +=
        QStringLiteral("<div class=\"meta\">Примітка: %1</div>").arg(htmlEscape(document.comment));
  }
  body += signatures(company.value());
  body += receipt ? QStringLiteral("<p class=\"muted\">Надходження: Дт 22 Кт 631. Залишок і сума "
                                   "на картці змінюються лише проведенням.</p>")
                  : QStringLiteral("<p class=\"muted\">Передача в експлуатацію: Дт 92 Кт 22 за "
                                   "середньою. 100% на витрати в місяці передачі.</p>");
  return Result<QString>::ok(
      wrapHtml(title + QLatin1Char(' ') + document.number, body, Paper::Portrait));
}

Result<QString> PrintForms::supplierServiceActHtml(const Document & document) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return Result<QString>::fail(accounts.error());
  }
  const std::vector<CashLine> rows = extractServicePurchaseLines(document.lines, accounts.value());
  Money total = Money::zero();
  for (const CashLine & row : rows) {
    const Result<Money> next = total.plus(row.amount);
    if (!next) {
      return Result<QString>::fail(next.error());
    }
    total = next.value();
  }
  const Result<QString> words = moneyToUkrainianWords(total);
  if (!words) {
    return Result<QString>::fail(words.error());
  }
  const QString title = QStringLiteral("Акт отриманих послуг");
  const QString party = counterpartyName(db_, document.counterpartyId);
  QString body;
  body += QStringLiteral("<h1>%1</h1>").arg(htmlEscape(title));
  body += QStringLiteral("<h2>№ %1 від %2</h2>")
              .arg(htmlEscape(document.number),
                   htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
  body += QStringLiteral("<div class=\"meta\">Постачальник: %1</div>")
              .arg(htmlEscape(dashIfEmpty(party)));
  body += QStringLiteral("<div class=\"meta\">Покупець: %1</div>")
              .arg(htmlEscape(company.value().heading()));
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"6%\">№</th><th width=\"48%\">Найменування</th>"
                         "<th width=\"22%\">Рахунок</th><th width=\"24%\">Сума</th></tr>");
  int n = 0;
  for (const CashLine & row : rows) {
    ++n;
    QString name = row.description.trimmed();
    if (name.isEmpty()) {
      name = QStringLiteral("—");
    }
    body += QStringLiteral("<tr><td class=\"num\">%1</td><td>%2</td><td>%3</td>"
                           "<td class=\"num\">%4</td></tr>")
                .arg(QString::number(n), htmlEscape(name),
                     htmlEscape(accountLabel(db_, row.correspondenceId)),
                     htmlEscape(row.amount.toDisplay()));
  }
  body += QStringLiteral("<tr><td colspan=\"3\"><b>Разом</b></td>"
                         "<td class=\"num\"><b>%1</b></td></tr>")
              .arg(htmlEscape(total.toDisplay()));
  body += QStringLiteral("</table>");
  body +=
      QStringLiteral("<div class=\"words\">Сума прописом: %1</div>").arg(htmlEscape(words.value()));
  if (!document.comment.trimmed().isEmpty()) {
    body +=
        QStringLiteral("<div class=\"meta\">Примітка: %1</div>").arg(htmlEscape(document.comment));
  }
  body += signatures(company.value());
  body += QStringLiteral("<p class=\"muted\">Сума акта — отримані послуги (Дт 92, 152 або 154 "
                         "Кт 631). Залік виданого авансу — Дт 631 Кт 371.</p>");
  return Result<QString>::ok(
      wrapHtml(title + QLatin1Char(' ') + document.number, body, Paper::Portrait));
}

Result<QString> PrintForms::namedGoodsHtml(const Document & document, const QString & title,
                                           const QString & supplier, const QString & buyer,
                                           const QString & footnote) const
{
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return Result<QString>::fail(accounts.error());
  }
  const std::vector<GoodsLine> rows =
      extractGoodsLines(document.type, document.lines, accounts.value());
  const Money net = goodsLineTotal(rows);
  const Money vat = goodsVatTotal(rows);
  const Result<Money> gross = net.plus(vat);
  if (!gross) {
    return Result<QString>::fail(gross.error());
  }
  const Result<QString> words = moneyToUkrainianWords(gross.value());
  if (!words) {
    return Result<QString>::fail(words.error());
  }
  QString body;
  body += QStringLiteral("<h1>%1</h1>").arg(htmlEscape(title));
  body += QStringLiteral("<h2>№ %1 від %2</h2>")
              .arg(htmlEscape(document.number),
                   htmlEscape(document.date.toString(QStringLiteral("dd.MM.yyyy"))));
  body += QStringLiteral("<div class=\"meta\">Виконавець / постачальник: %1</div>")
              .arg(htmlEscape(supplier));
  body +=
      QStringLiteral("<div class=\"meta\">Замовник / покупець: %1</div>").arg(htmlEscape(buyer));
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"6%\">№</th><th width=\"40%\">Найменування</th>"
                         "<th width=\"8%\">Од.</th><th width=\"14%\">К-сть</th>"
                         "<th width=\"16%\">Ціна</th><th width=\"16%\">Сума</th></tr>");
  NomenclatureRepository items(db_);
  int n = 0;
  for (const GoodsLine & row : rows) {
    ++n;
    QString name = row.description.trimmed();
    QString unit = QStringLiteral("посл.");
    if (row.nomenclatureId) {
      const Result<Nomenclature> item = items.findById(row.nomenclatureId);
      if (item) {
        name = item.value().name;
        if (!item.value().sku.isEmpty()) {
          name = item.value().sku + QStringLiteral("  ") + name;
        }
        unit = item.value().unit;
      }
    }
    if (name.isEmpty()) {
      name = QStringLiteral("—");
    }
    body += QStringLiteral("<tr><td class=\"num\">%1</td><td>%2</td><td>%3</td>"
                           "<td class=\"num\">%4</td><td class=\"num\">%5</td>"
                           "<td class=\"num\">%6</td></tr>")
                .arg(QString::number(n), htmlEscape(name), htmlEscape(unit),
                     htmlEscape(row.quantity.isZero() ? QString() : row.quantity.toDisplay()),
                     htmlEscape(row.unitPrice.isZero() ? QString() : row.unitPrice.toDisplay()),
                     htmlEscape(row.amount.toDisplay()));
  }
  body += QStringLiteral("<tr><td colspan=\"5\"><b>Разом без ПДВ</b></td>"
                         "<td class=\"num\"><b>%1</b></td></tr>")
              .arg(htmlEscape(net.toDisplay()));
  if (!vat.isZero()) {
    body += QStringLiteral("<tr><td colspan=\"5\">ПДВ</td>"
                           "<td class=\"num\">%1</td></tr>")
                .arg(htmlEscape(vat.toDisplay()));
    body += QStringLiteral("<tr><td colspan=\"5\"><b>Разом з ПДВ</b></td>"
                           "<td class=\"num\"><b>%1</b></td></tr>")
                .arg(htmlEscape(gross.value().toDisplay()));
  }
  body += QStringLiteral("</table>");
  body +=
      QStringLiteral("<div class=\"words\">Сума прописом: %1</div>").arg(htmlEscape(words.value()));
  if (!document.comment.trimmed().isEmpty()) {
    body +=
        QStringLiteral("<div class=\"meta\">Примітка: %1</div>").arg(htmlEscape(document.comment));
  }
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (company) {
    body += signatures(company.value());
  }
  body += QStringLiteral("<p class=\"muted\">%1</p>").arg(htmlEscape(footnote));
  return Result<QString>::ok(
      wrapHtml(title + QLatin1Char(' ') + document.number, body, Paper::Portrait));
}

Result<QString> PrintForms::cashJournalHtml(const CashJournal & journal) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<div class=\"formcode\">%1</div>")
              .arg(htmlEscape(QStringLiteral("Типова форма № КО-3")));
  body += QStringLiteral("<h1>Журнал реєстрації прибуткових і видаткових "
                         "касових документів</h1>");
  body += QStringLiteral("<h2>%1 — %2</h2>")
              .arg(htmlEscape(journal.from.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(journal.to.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"12%\">Дата</th><th width=\"12%\">Номер</th>"
                         "<th width=\"18%\">Документ</th><th width=\"20%\">Контрагент</th>"
                         "<th width=\"14%\">Надійшло</th><th width=\"14%\">Вибуло</th>"
                         "<th width=\"10%\">Підстава</th></tr>");
  for (const CashJournalRow & row : journal.rows) {
    body += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td>"
                           "<td class=\"num\">%5</td><td class=\"num\">%6</td><td>%7</td></tr>")
                .arg(htmlEscape(row.date.toString(QStringLiteral("dd.MM.yyyy"))),
                     htmlEscape(row.number), htmlEscape(row.typeLabel), htmlEscape(row.party),
                     htmlEscape(row.incoming.isZero() ? QString() : row.incoming.toDisplay()),
                     htmlEscape(row.outgoing.isZero() ? QString() : row.outgoing.toDisplay()),
                     htmlEscape(row.purpose));
  }
  body +=
      QStringLiteral("<tr><td colspan=\"4\"><b>Разом</b></td>"
                     "<td class=\"num\"><b>%1</b></td><td class=\"num\"><b>%2</b></td>"
                     "<td></td></tr>")
          .arg(htmlEscape(journal.incoming.toDisplay()), htmlEscape(journal.outgoing.toDisplay()));
  body += QStringLiteral("</table>");
  body += signatures(company.value());
  return Result<QString>::ok(
      wrapHtml(QStringLiteral("Журнал касових документів"), body, Paper::Landscape));
}

Result<QString> PrintForms::cashJournalCsv(const CashJournal & journal) const
{
  QString csv = QStringLiteral("Дата;Номер;Документ;Контрагент;Надійшло;Вибуло;Підстава\n");
  for (const CashJournalRow & row : journal.rows) {
    csv += QStringLiteral("%1;%2;%3;%4;%5;%6;%7\n")
               .arg(csvCell(row.date.toString(QStringLiteral("dd.MM.yyyy"))), csvCell(row.number),
                    csvCell(row.typeLabel), csvCell(row.party), csvMoney(row.incoming),
                    csvMoney(row.outgoing), csvCell(row.purpose));
  }
  csv += QStringLiteral(";;;Разом;%1;%2;\n")
             .arg(csvMoney(journal.incoming), csvMoney(journal.outgoing));
  return Result<QString>::ok(csv);
}

Result<QString> PrintForms::trialBalanceHtml(const TrialBalance & report) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<h1>Оборотно-сальдова відомість</h1>");
  body += QStringLiteral("<h2>%1 — %2</h2>")
              .arg(htmlEscape(report.from.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(report.to.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  body += tableOpen();
  body += QStringLiteral(
      "<tr><th width=\"8%\">Рахунок</th><th width=\"22%\">Назва</th>"
      "<th width=\"11.6%\">Сальдо поч. Дт</th><th width=\"11.6%\">Сальдо поч. Кт</th>"
      "<th width=\"11.6%\">Оборот Дт</th><th width=\"11.6%\">Оборот Кт</th>"
      "<th width=\"11.6%\">Сальдо кін. Дт</th><th width=\"11.6%\">Сальдо кін. Кт</th></tr>");
  auto addRow = [&](const TrialBalanceRow & row, bool bold) {
    const QString open = bold ? QStringLiteral("<b>%1</b>") : QStringLiteral("%1");
    body += QStringLiteral("<tr><td>%1</td><td>%2</td>")
                .arg(htmlEscape(row.code), htmlEscape(row.name));
    const Money cols[] = {row.openingDebit,   row.openingCredit, row.turnoverDebit,
                          row.turnoverCredit, row.closingDebit,  row.closingCredit};
    for (Money value : cols) {
      body += QStringLiteral("<td class=\"num\">");
      body += open.arg(htmlEscape(value.isZero() ? QString() : value.toDisplay()));
      body += QStringLiteral("</td>");
    }
    body += QStringLiteral("</tr>");
  };
  for (const TrialBalanceRow & row : report.rows) {
    addRow(row, false);
  }
  addRow(report.totals, true);
  body += QStringLiteral("</table>");
  return Result<QString>::ok(wrapHtml(QStringLiteral("ОСВ"), body, Paper::Landscape));
}

Result<QString> PrintForms::trialBalanceCsv(const TrialBalance & report) const
{
  QString csv = QStringLiteral("Рахунок;Назва;Сальдо поч. Дт;Сальдо поч. Кт;Оборот Дт;Оборот Кт;"
                               "Сальдо кін. Дт;Сальдо кін. Кт\n");
  auto addRow = [&](const TrialBalanceRow & row) {
    csv += QStringLiteral("%1;%2;%3;%4;%5;%6;%7;%8\n")
               .arg(csvCell(row.code), csvCell(row.name), csvMoney(row.openingDebit),
                    csvMoney(row.openingCredit), csvMoney(row.turnoverDebit),
                    csvMoney(row.turnoverCredit), csvMoney(row.closingDebit),
                    csvMoney(row.closingCredit));
  };
  for (const TrialBalanceRow & row : report.rows) {
    addRow(row);
  }
  addRow(report.totals);
  return Result<QString>::ok(csv);
}

Result<QString> PrintForms::accountCardHtml(const AccountCard & card) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<h1>Картка рахунку %1</h1>").arg(htmlEscape(card.code));
  body += QStringLiteral("<h2>%1 — %2</h2>")
              .arg(htmlEscape(card.from.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(card.to.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  body += QStringLiteral("<div class=\"meta\">%1</div>").arg(htmlEscape(card.name));
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"9%\">Дата</th><th width=\"12%\">Документ</th>"
                         "<th width=\"22%\">Зміст</th><th width=\"9%\">Дебет</th>"
                         "<th width=\"9%\">Кредит</th><th width=\"9%\">Сальдо Дт</th>"
                         "<th width=\"9%\">Сальдо Кт</th><th width=\"7%\">Валюта</th>"
                         "<th width=\"7%\">Вал. Дт</th><th width=\"7%\">Вал. Кт</th></tr>");
  body +=
      QStringLiteral("<tr><td colspan=\"3\"><b>Сальдо на початок</b></td>"
                     "<td></td><td></td><td class=\"num\">%1</td><td class=\"num\">%2</td>"
                     "<td></td><td></td><td></td></tr>")
          .arg(htmlEscape(card.openingDebit.toDisplay()),
               htmlEscape(card.openingCredit.toDisplay()));
  for (const AccountCardRow & row : card.rows) {
    body += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td>"
                           "<td class=\"num\">%4</td><td class=\"num\">%5</td>"
                           "<td class=\"num\">%6</td><td class=\"num\">%7</td>"
                           "<td>%8</td><td class=\"num\">%9</td><td class=\"num\">")
                .arg(htmlEscape(row.date.toString(QStringLiteral("dd.MM.yyyy"))),
                     htmlEscape(row.documentNumber), htmlEscape(row.description),
                     htmlEscape(row.debit.isZero() ? QString() : row.debit.toDisplay()),
                     htmlEscape(row.credit.isZero() ? QString() : row.credit.toDisplay()),
                     htmlEscape(row.balanceDebit.toDisplay()),
                     htmlEscape(row.balanceCredit.toDisplay()), htmlEscape(row.currencyCode),
                     htmlEscape(row.currencyDebit.isZero() ? QString()
                                                          : row.currencyDebit.toDisplay()));
    body += htmlEscape(row.currencyCredit.isZero() ? QString() : row.currencyCredit.toDisplay());
    body += QStringLiteral("</td></tr>");
  }
  body += QStringLiteral("<tr><td colspan=\"3\"><b>Разом / сальдо</b></td>"
                         "<td class=\"num\"><b>%1</b></td><td class=\"num\"><b>%2</b></td>"
                         "<td class=\"num\"><b>%3</b></td><td class=\"num\"><b>%4</b></td>"
                         "<td></td><td></td><td></td></tr>")
              .arg(htmlEscape(card.turnoverDebit.toDisplay()),
                   htmlEscape(card.turnoverCredit.toDisplay()),
                   htmlEscape(card.closingDebit.toDisplay()),
                   htmlEscape(card.closingCredit.toDisplay()));
  body += QStringLiteral("</table>");
  return Result<QString>::ok(
      wrapHtml(QStringLiteral("Картка %1").arg(card.code), body, Paper::Landscape));
}

Result<QString> PrintForms::accountCardCsv(const AccountCard & card) const
{
  QString csv = QStringLiteral("Дата;Документ;Зміст;Дебет;Кредит;Сальдо Дт;Сальдо Кт\n");
  csv += QStringLiteral("%1;;Сальдо на початок;;;%2;%3\n")
             .arg(csvCell(card.from.toString(QStringLiteral("dd.MM.yyyy"))),
                  csvMoney(card.openingDebit), csvMoney(card.openingCredit));
  for (const AccountCardRow & row : card.rows) {
    csv += QStringLiteral("%1;%2;%3;%4;%5;%6;%7\n")
               .arg(csvCell(row.date.toString(QStringLiteral("dd.MM.yyyy"))),
                    csvCell(row.documentNumber), csvCell(row.description), csvMoney(row.debit),
                    csvMoney(row.credit), csvMoney(row.balanceDebit), csvMoney(row.balanceCredit));
  }
  csv += QStringLiteral(";;Разом;%1;%2;%3;%4\n")
             .arg(csvMoney(card.turnoverDebit), csvMoney(card.turnoverCredit),
                  csvMoney(card.closingDebit), csvMoney(card.closingCredit));
  return Result<QString>::ok(csv);
}

Result<QString> PrintForms::stockHtml(const StockReportData & report) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<h1>Залишки товарів</h1>");
  body += QStringLiteral("<h2>%1 — %2</h2>")
              .arg(htmlEscape(report.from.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(report.to.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  body += tableOpen();
  body +=
      QStringLiteral("<tr><th width=\"18%\">Номенклатура</th><th width=\"6%\">Од.</th>"
                     "<th width=\"9.5%\">Поч. к-сть</th><th width=\"9.5%\">Поч. сума</th>"
                     "<th width=\"9.5%\">Надх. к-сть</th><th width=\"9.5%\">Надх. сума</th>"
                     "<th width=\"9.5%\">Виб. к-сть</th><th width=\"9.5%\">Виб. сума</th>"
                     "<th width=\"9.5%\">Кін. к-сть</th><th width=\"9.5%\">Кін. сума</th></tr>");
  auto addRow = [&](const StockRow & row, bool bold) {
    const QString wrap = bold ? QStringLiteral("<b>%1</b>") : QStringLiteral("%1");
    body += QStringLiteral("<tr><td>%1</td><td>%2</td>")
                .arg(htmlEscape(row.name), htmlEscape(row.unit));
    const QString cells[] = {row.openingQty.toDisplay(), row.openingAmount.toDisplay(),
                             row.inQty.toDisplay(),      row.inAmount.toDisplay(),
                             row.outQty.toDisplay(),     row.outAmount.toDisplay(),
                             row.closingQty.toDisplay(), row.closingAmount.toDisplay()};
    for (const QString & cell : cells) {
      body += QStringLiteral("<td class=\"num\">%1</td>").arg(wrap.arg(htmlEscape(cell)));
    }
    body += QStringLiteral("</tr>");
  };
  for (const StockRow & row : report.rows) {
    addRow(row, false);
  }
  addRow(report.totals, true);
  body += QStringLiteral("</table>");
  return Result<QString>::ok(wrapHtml(QStringLiteral("Залишки товарів"), body, Paper::Landscape));
}

Result<QString> PrintForms::stockCsv(const StockReportData & report) const
{
  QString csv = QStringLiteral("Номенклатура;Од.;Поч. к-сть;Поч. сума;Надх. к-сть;Надх. сума;"
                               "Виб. к-сть;Виб. сума;Кін. к-сть;Кін. сума\n");
  auto addRow = [&](const StockRow & row) {
    csv += csvCell(row.name) + QLatin1Char(';') + csvCell(row.unit) + QLatin1Char(';') +
           csvCell(row.openingQty.toDisplay()) + QLatin1Char(';') + csvMoney(row.openingAmount) +
           QLatin1Char(';') + csvCell(row.inQty.toDisplay()) + QLatin1Char(';') +
           csvMoney(row.inAmount) + QLatin1Char(';') + csvCell(row.outQty.toDisplay()) +
           QLatin1Char(';') + csvMoney(row.outAmount) + QLatin1Char(';') +
           csvCell(row.closingQty.toDisplay()) + QLatin1Char(';') + csvMoney(row.closingAmount) +
           QLatin1Char('\n');
  };
  for (const StockRow & row : report.rows) {
    addRow(row);
  }
  addRow(report.totals);
  return Result<QString>::ok(csv);
}

Result<QString> PrintForms::incomeStatementHtml(const IncomeStatement & report) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<h1>Звіт про фінансові результати</h1>");
  body += QStringLiteral("<h2>%1 — %2</h2>")
              .arg(htmlEscape(report.from.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(report.to.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"70%\">Стаття</th><th width=\"30%\">Сума</th></tr>");
  auto add = [&](const QString & label, Money amount) {
    body += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td></tr>")
                .arg(htmlEscape(label), htmlEscape(amount.toDisplay()));
  };
  add(QStringLiteral("Дохід від реалізації товарів (701)"), report.goodsRevenue);
  add(QStringLiteral("Дохід від реалізації послуг (703)"), report.serviceRevenue);
  add(QStringLiteral("Інший операційний дохід (71)"), report.otherIncome);
  add(QStringLiteral("Собівартість реалізації (902)"), report.cogs);
  add(QStringLiteral("Адміністративні витрати (92)"), report.admin);
  body += QStringLiteral("<tr><td><b>Фінансовий результат</b></td>"
                         "<td class=\"num\"><b>%1</b></td></tr>")
              .arg(htmlEscape(report.profit.toDisplay()));
  body += QStringLiteral("</table>");
  body +=
      QStringLiteral("<p class=\"muted\">Обороти за період без документа «Фінансовий результат». "
                     "Після закриття місяця сальдо 701/703/71/902/92 у ОСВ нульове.</p>");
  body += signatures(company.value());
  return Result<QString>::ok(wrapHtml(QStringLiteral("Фінрезультати"), body, Paper::Portrait));
}

Result<QString> PrintForms::balanceSheetHtml(const BalanceSheet & report) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<h1>Баланс</h1>");
  body += QStringLiteral("<h2>на %1</h2>")
              .arg(htmlEscape(report.asOf.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  auto section = [&](const QString & title, const std::vector<StatementLine> & rows, Money total) {
    body += QStringLiteral("<h2>%1</h2>").arg(htmlEscape(title));
    body += tableOpen();
    body += QStringLiteral("<tr><th width=\"16%\">Рахунок</th><th width=\"54%\">Назва</th>"
                           "<th width=\"30%\">Сума</th></tr>");
    for (const StatementLine & row : rows) {
      body +=
          QStringLiteral("<tr><td>%1</td><td>%2</td><td class=\"num\">%3</td></tr>")
              .arg(htmlEscape(row.code), htmlEscape(row.name), htmlEscape(row.amount.toDisplay()));
    }
    body += QStringLiteral("<tr><td colspan=\"2\"><b>Разом</b></td>"
                           "<td class=\"num\"><b>%1</b></td></tr>")
                .arg(htmlEscape(total.toDisplay()));
    body += QStringLiteral("</table>");
  };
  section(QStringLiteral("Актив"), report.assets, report.assetsTotal);
  section(QStringLiteral("Власний капітал"), report.equity, report.equityTotal);
  section(QStringLiteral("Зобов'язання"), report.liabilities, report.liabilitiesTotal);
  body += QStringLiteral("<div class=\"meta\">Пасив разом: %1</div>")
              .arg(htmlEscape(
                  Money::fromCents(report.equityTotal.cents() + report.liabilitiesTotal.cents())
                      .toDisplay()));
  body +=
      QStringLiteral("<p class=\"muted\">Сальдо рахунків на дату. Незакриті 701/703/71/902/92/79 "
                     "згорнуто в рядок фінрезультату.</p>");
  body += signatures(company.value());
  return Result<QString>::ok(wrapHtml(QStringLiteral("Баланс"), body, Paper::Portrait));
}

Result<QString> PrintForms::incomeBookHtml(const IncomeBook & book) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<h1>Книга обліку доходів</h1>");
  body += QStringLiteral("<h2>%1 — %2</h2>")
              .arg(htmlEscape(book.from.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(book.to.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  const QString system = book.taxSystem == TaxSystem::Simplified
                             ? QStringLiteral("спрощена система")
                             : QStringLiteral("загальна система");
  body += QStringLiteral("<div class=\"meta\">Система: %1</div>").arg(htmlEscape(system));
  if (book.taxSystem == TaxSystem::Simplified) {
    body += QStringLiteral("<div class=\"meta\">Ставка ЄП: %1%</div>")
                .arg(QString::number(book.rateBp / 100.0, 'f', 2));
  }
  body += tableOpen();
  body += QStringLiteral("<tr><th>Дата</th><th>Номер</th><th>Документ</th><th>Контрагент</th>"
                         "<th>Рахунок</th><th>Сума</th></tr>");
  for (const IncomeBookRow & row : book.rows) {
    body += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td>"
                           "<td class=\"num\">%6</td></tr>")
                .arg(htmlEscape(row.date.toString(QStringLiteral("dd.MM.yyyy"))),
                     htmlEscape(row.number), htmlEscape(row.typeLabel), htmlEscape(row.party),
                     htmlEscape(row.account), htmlEscape(row.amount.toDisplay()));
  }
  body += QStringLiteral("<tr><td colspan=\"5\"><b>Дохід</b></td>"
                         "<td class=\"num\"><b>%1</b></td></tr>")
              .arg(htmlEscape(book.income.toDisplay()));
  if (book.taxSystem == TaxSystem::Simplified) {
    body += QStringLiteral("<tr><td colspan=\"5\"><b>Єдиний податок</b></td>"
                           "<td class=\"num\"><b>%1</b></td></tr>")
                .arg(htmlEscape(book.tax.toDisplay()));
  }
  body += QStringLiteral("</table>");
  body +=
      QStringLiteral("<p class=\"muted\">Дохід = кредит 701 і 703 мінус повернення, без закриття "
                     "фінрезультату. ПДВ і зарплата в цій версії не ведуться.</p>");
  body += signatures(company.value());
  return Result<QString>::ok(wrapHtml(QStringLiteral("Книга доходів"), body, Paper::Landscape));
}

Result<QString> PrintForms::reconciliationHtml(const ReconciliationAct & act) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString party = act.party.name;
  if (!act.party.edrpou.trimmed().isEmpty()) {
    party += QStringLiteral(" (ЄДРПОУ %1)").arg(act.party.edrpou.trimmed());
  }
  QString body;
  body += QStringLiteral("<div class=\"formcode\">Акт звірки розрахунків</div>");
  body += QStringLiteral("<h1>Акт звірки розрахунків</h1>");
  body += QStringLiteral("<h2>за період %1 — %2</h2>")
              .arg(htmlEscape(act.from.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(act.to.toString(QStringLiteral("dd.MM.yyyy"))));
  body +=
      QStringLiteral("<div class=\"meta\">Ми: %1</div>").arg(htmlEscape(company.value().heading()));
  body += QStringLiteral("<div class=\"meta\">Ви: %1</div>").arg(htmlEscape(party));
  body += QStringLiteral("<div class=\"meta\">Рахунок: %1 %2</div>")
              .arg(htmlEscape(act.accountCode), htmlEscape(act.accountName));
  body += tableOpen();
  body += QStringLiteral("<tr><th>Дата</th><th>Документ</th><th>Зміст</th>"
                         "<th class=\"num\">Дебет</th><th class=\"num\">Кредит</th></tr>");
  body +=
      QStringLiteral("<tr><td></td><td></td><td>Сальдо на початок</td>"
                     "<td class=\"num\">%1</td><td class=\"num\">%2</td></tr>")
          .arg(htmlEscape(act.openingDebit.isZero() ? QString() : act.openingDebit.toDisplay()),
               htmlEscape(act.openingCredit.isZero() ? QString() : act.openingCredit.toDisplay()));
  for (const AccountCardRow & row : act.rows) {
    body += QStringLiteral("<tr><td>%1</td><td>%2 %3</td><td>%4</td>"
                           "<td class=\"num\">%5</td><td class=\"num\">%6</td></tr>")
                .arg(htmlEscape(row.date.toString(QStringLiteral("dd.MM.yyyy"))),
                     htmlEscape(row.documentTypeLabel), htmlEscape(row.documentNumber),
                     htmlEscape(row.description),
                     htmlEscape(row.debit.isZero() ? QString() : row.debit.toDisplay()),
                     htmlEscape(row.credit.isZero() ? QString() : row.credit.toDisplay()));
  }
  body += QStringLiteral("<tr><td></td><td></td><td><b>Обороти</b></td>"
                         "<td class=\"num\"><b>%1</b></td><td class=\"num\"><b>%2</b></td></tr>")
              .arg(htmlEscape(act.turnoverDebit.toDisplay()),
                   htmlEscape(act.turnoverCredit.toDisplay()));
  body +=
      QStringLiteral("<tr><td></td><td></td><td><b>Сальдо на кінець</b></td>"
                     "<td class=\"num\"><b>%1</b></td><td class=\"num\"><b>%2</b></td></tr>")
          .arg(htmlEscape(act.closingDebit.isZero() ? QString() : act.closingDebit.toDisplay()),
               htmlEscape(act.closingCredit.isZero() ? QString() : act.closingCredit.toDisplay()));
  body += QStringLiteral("</table>");
  QString closingText;
  if (!act.closingDebit.isZero()) {
    closingText = QStringLiteral("на нашу користь %1 грн").arg(act.closingDebit.toDisplay());
  } else if (!act.closingCredit.isZero()) {
    closingText = QStringLiteral("на вашу користь %1 грн").arg(act.closingCredit.toDisplay());
  } else {
    closingText = QStringLiteral("заборгованість відсутня");
  }
  body +=
      QStringLiteral("<p class=\"words\">За даними обліку %1.</p>").arg(htmlEscape(closingText));
  body += QStringLiteral(
      "<table class=\"sheet\"><tr>"
      "<td class=\"pair\"><b>Від нас</b><div class=\"sign\">Підпис ____________</div></td>"
      "<td class=\"pair\"><b>Від вас</b><div class=\"sign\">Підпис ____________</div></td>"
      "</tr></table>");
  body +=
      QStringLiteral("<p class=\"muted\">Акт формується з журналу проводок по вибраному рахунку "
                     "і контрагенту. Має сходитися з карткою рахунку.</p>");
  return Result<QString>::ok(wrapHtml(QStringLiteral("Акт звірки"), body, Paper::Landscape));
}

Result<QString> PrintForms::vatStatementHtml(const VatStatement & report) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  auto rowHtml = [](const TrialBalanceRow & row) {
    return QStringLiteral("<tr><td>%1</td><td>%2</td>"
                          "<td class=\"num\">%3</td><td class=\"num\">%4</td>"
                          "<td class=\"num\">%5</td><td class=\"num\">%6</td>"
                          "<td class=\"num\">%7</td><td class=\"num\">%8</td></tr>")
        .arg(htmlEscape(row.code), htmlEscape(row.name), htmlEscape(row.openingDebit.toDisplay()),
             htmlEscape(row.openingCredit.toDisplay()), htmlEscape(row.turnoverDebit.toDisplay()),
             htmlEscape(row.turnoverCredit.toDisplay()), htmlEscape(row.closingDebit.toDisplay()),
             htmlEscape(row.closingCredit.toDisplay()));
  };
  QString body;
  body += QStringLiteral("<h1>Відомість ПДВ</h1>");
  body += QStringLiteral("<h2>%1 — %2</h2>")
              .arg(htmlEscape(report.from.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(report.to.toString(QStringLiteral("dd.MM.yyyy"))));
  body += QStringLiteral("<div class=\"meta\">%1</div>").arg(htmlEscape(company.value().heading()));
  body += tableOpen();
  body += QStringLiteral(
      "<tr><th>Рахунок</th><th>Назва</th>"
      "<th class=\"num\">Сальдо поч. Дт</th><th class=\"num\">Сальдо поч. Кт</th>"
      "<th class=\"num\">Оборот Дт</th><th class=\"num\">Оборот Кт</th>"
      "<th class=\"num\">Сальдо кін. Дт</th><th class=\"num\">Сальдо кін. Кт</th></tr>");
  body += rowHtml(report.liability);
  body += rowHtml(report.credit);
  body += rowHtml(report.budget);
  body += QStringLiteral("</table>");
  body += QStringLiteral("<p class=\"muted\">Період той самий, що в ОСВ: відкриття = рух до дати "
                         "«з». Це не декларація ДПС, а обороти 641/643/644.</p>");
  return Result<QString>::ok(wrapHtml(QStringLiteral("Відомість ПДВ"), body, Paper::Landscape));
}

Result<QString> PrintForms::counterpartyTurnoverHtml(const CounterpartyTurnover & report) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<h1>Обороти з контрагентами</h1>");
  body += QStringLiteral("<h2>Рахунок %1 %2</h2>")
              .arg(htmlEscape(report.accountCode), htmlEscape(report.accountName));
  body += QStringLiteral("<h2>%1 — %2</h2>")
              .arg(htmlEscape(report.from.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(report.to.toString(QStringLiteral("dd.MM.yyyy"))));
  body += companyBlock(company.value());
  body += tableOpen();
  body += QStringLiteral(
      "<tr><th>Контрагент</th>"
      "<th class=\"num\">Сальдо поч. Дт</th><th class=\"num\">Сальдо поч. Кт</th>"
      "<th class=\"num\">Оборот Дт</th><th class=\"num\">Оборот Кт</th>"
      "<th class=\"num\">Сальдо кін. Дт</th><th class=\"num\">Сальдо кін. Кт</th></tr>");
  auto addRow = [&](const CounterpartyTurnoverRow & row, bool bold) {
    const QString open = bold ? QStringLiteral("<b>%1</b>") : QStringLiteral("%1");
    body += QStringLiteral("<tr><td>%1</td>").arg(htmlEscape(row.name));
    const Money cols[] = {row.openingDebit,   row.openingCredit, row.turnoverDebit,
                          row.turnoverCredit, row.closingDebit,  row.closingCredit};
    for (Money value : cols) {
      body += QStringLiteral("<td class=\"num\">");
      body += open.arg(htmlEscape(value.isZero() ? QString() : value.toDisplay()));
      body += QStringLiteral("</td>");
    }
    body += QStringLiteral("</tr>");
  };
  for (const CounterpartyTurnoverRow & row : report.rows) {
    addRow(row, false);
  }
  CounterpartyTurnoverRow totals = report.totals;
  totals.name = QStringLiteral("Разом");
  addRow(totals, true);
  body += QStringLiteral("</table>");
  body += signatures(company.value());
  return Result<QString>::ok(
      wrapHtml(QStringLiteral("Обороти з контрагентами"), body, Paper::Landscape));
}

Result<QString> PrintForms::timesheetHtml(const Timesheet & ts) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<div class=\"formcode\">Типова форма № П-5</div>");
  body += QStringLiteral("<h1>Табель обліку робочого часу</h1>");
  body += QStringLiteral("<h2>За %1.%2</h2>")
              .arg(QString::number(ts.month).rightJustified(2, QLatin1Char('0')),
                   QString::number(ts.year));
  body += companyBlock(company.value());
  body += tableOpen();
  body +=
      QStringLiteral("<tr><th width=\"30%\">ПІБ</th><th width=\"10%\">Норма (дн)</th>"
                     "<th width=\"10%\">Відпрацьовано (дн)</th><th width=\"10%\">Години</th>"
                     "<th width=\"10%\">Відпустка (дн)</th><th width=\"10%\">Лікарняні (дн)</th>"
                     "<th width=\"10%\">Неявки (дн)</th></tr>");
  for (const TimesheetRow & row : ts.rows) {
    body +=
        QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td><td class=\"num\">%3</td>"
                       "<td class=\"num\">%4</td><td class=\"num\">%5</td><td class=\"num\">%6</td>"
                       "<td class=\"num\">%7</td></tr>")
            .arg(htmlEscape(row.employeeName), QString::number(row.workingDaysNorm),
                 QString::number(row.daysWorked), QString::number(row.hoursWorked),
                 QString::number(row.vacationDays), QString::number(row.sickDays),
                 QString::number(row.absenceDays));
  }
  body += QStringLiteral("</table>");
  body += signatures(company.value());
  return Result<QString>::ok(wrapHtml(QStringLiteral("Табель П-5"), body, Paper::Landscape));
}

Result<QString> PrintForms::payslipHtml(const PayrollLine & line, const Employee & emp,
                                        QDate month) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<h1>Розрахунковий листок</h1>");
  body += QStringLiteral("<h2>за %1.%2</h2>")
              .arg(QString::number(month.month()).rightJustified(2, QLatin1Char('0')),
                   QString::number(month.year()));
  body += companyBlock(company.value());
  body += QStringLiteral("<div class=\"meta\">Працівник: %1</div>").arg(htmlEscape(emp.name));
  if (!emp.position.isEmpty()) {
    body += QStringLiteral("<div class=\"meta\">Посада: %1</div>").arg(htmlEscape(emp.position));
  }
  if (!emp.department.isEmpty()) {
    body +=
        QStringLiteral("<div class=\"meta\">Підрозділ: %1</div>").arg(htmlEscape(emp.department));
  }
  body += tableOpen();
  body += QStringLiteral(
      "<tr><th width=\"50%\">Види нарахувань</th><th width=\"50%\">Види утримань</th></tr>");

  QString grossHtml = QStringLiteral("Нараховано: %1").arg(htmlEscape(line.gross.toDisplay()));
  QString dedHtml =
      QStringLiteral("ПДФО (18%): %1<br/>Військовий збір (5%): %2")
          .arg(htmlEscape(line.pit.toDisplay()), htmlEscape(line.military.toDisplay()));
  body += QStringLiteral("<tr><td class=\"pair\">%1</td><td class=\"pair\">%2</td></tr>")
              .arg(grossHtml, dedHtml);

  const Result<Money> totalDed = line.pit.plus(line.military);
  if (!totalDed) {
    return Result<QString>::fail(totalDed.error());
  }

  body += QStringLiteral("<tr><td><b>Всього нараховано: %1</b></td>"
                         "<td><b>Всього утримано: %2</b></td></tr>")
              .arg(htmlEscape(line.gross.toDisplay()), htmlEscape(totalDed.value().toDisplay()));
  body += QStringLiteral("<tr><td colspan=\"2\"><b>До виплати на руки: %1</b></td></tr>")
              .arg(htmlEscape(line.net.toDisplay()));
  body += QStringLiteral("</table>");
  body += QStringLiteral("<p class=\"muted\">ЄСВ нараховано на фонд оплати праці: %1</p>")
              .arg(htmlEscape(line.ssc.toDisplay()));
  return Result<QString>::ok(
      wrapHtml(QStringLiteral("Розрахунковий листок"), body, Paper::Portrait));
}

Result<QString> PrintForms::payrollSummaryHtml(const std::vector<PayrollLine> & lines,
                                               QDate month) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<div class=\"formcode\">Типова форма № П-6</div>");
  body += QStringLiteral("<h1>Розрахунково-платіжна відомість</h1>");
  body += QStringLiteral("<h2>за %1.%2</h2>")
              .arg(QString::number(month.month()).rightJustified(2, QLatin1Char('0')),
                   QString::number(month.year()));
  body += companyBlock(company.value());
  body += tableOpen();
  body += QStringLiteral("<tr><th width=\"30%\">ПІБ</th><th width=\"14%\">Нараховано</th>"
                         "<th width=\"14%\">ПДФО</th><th width=\"14%\">ВЗ</th>"
                         "<th width=\"14%\">ЄСВ</th><th width=\"14%\">До виплати</th></tr>");

  Money gross = Money::zero(), pit = Money::zero(), military = Money::zero(), ssc = Money::zero(),
        net = Money::zero();
  for (const PayrollLine & row : lines) {
    auto add = [](Money & total, Money amount) {
      if (const auto next = total.plus(amount)) {
        total = next.value();
      }
    };
    add(gross, row.gross);
    add(pit, row.pit);
    add(military, row.military);
    add(ssc, row.ssc);
    add(net, row.net);

    body += QStringLiteral(
                "<tr><td>%1</td><td class=\"num\">%2</td><td class=\"num\">%3</td>"
                "<td class=\"num\">%4</td><td class=\"num\">%5</td><td class=\"num\">%6</td></tr>")
                .arg(htmlEscape(row.name), htmlEscape(row.gross.toDisplay()),
                     htmlEscape(row.pit.toDisplay()), htmlEscape(row.military.toDisplay()),
                     htmlEscape(row.ssc.toDisplay()), htmlEscape(row.net.toDisplay()));
  }
  body += QStringLiteral("<tr><td><b>Разом</b></td><td class=\"num\"><b>%1</b></td>"
                         "<td class=\"num\"><b>%2</b></td><td class=\"num\"><b>%3</b></td>"
                         "<td class=\"num\"><b>%4</b></td><td class=\"num\"><b>%5</b></td></tr>")
              .arg(htmlEscape(gross.toDisplay()), htmlEscape(pit.toDisplay()),
                   htmlEscape(military.toDisplay()), htmlEscape(ssc.toDisplay()),
                   htmlEscape(net.toDisplay()));
  body += QStringLiteral("</table>");
  body += QStringLiteral("<p class=\"muted\">Дт 92 Кт 661 (оклад), Дт 92 Кт 651 (ЄСВ), "
                         "Дт 661 Кт 642 (ПДФО), Дт 661 Кт 645 (військовий збір). "
                         "Виплата нетто — банківський платіж або ВКО на 661.</p>");
  body += signatures(company.value());
  return Result<QString>::ok(wrapHtml(QStringLiteral("Відомість П-6"), body, Paper::Landscape));
}

Result<QString> PrintForms::vacationOrderHtml(const VacationCalc & calc, const Employee & emp) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<div class=\"formcode\">Типова форма № П-3</div>");
  body += QStringLiteral("<h1>Наказ (розпорядження) про надання відпустки</h1>");
  body += companyBlock(company.value());
  body += QStringLiteral("<p>Надати щорічну основну відпустку:</p>");
  body +=
      QStringLiteral("<div class=\"meta\">Працівник: <b>%1</b></div>").arg(htmlEscape(emp.name));
  if (!emp.position.isEmpty()) {
    body += QStringLiteral("<div class=\"meta\">Посада: %1</div>").arg(htmlEscape(emp.position));
  }
  body += QStringLiteral("<div class=\"meta\">Період відпустки: з %1 по %2</div>")
              .arg(htmlEscape(calc.vacationFrom.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(calc.vacationTo.toString(QStringLiteral("dd.MM.yyyy"))));
  body += QStringLiteral("<div class=\"meta\">Кількість календарних днів: <b>%1</b></div>")
              .arg(QString::number(calc.vacationDays));
  body += QStringLiteral("<br/><p>Розрахунок відпускних:</p>");
  body += tableOpen();
  body += QStringLiteral("<tr><th>Середньоденний заробіток</th><th>Сума відпускних</th></tr>");
  body += QStringLiteral("<tr><td class=\"num\">%1</td><td class=\"num\"><b>%2</b></td></tr>")
              .arg(htmlEscape(calc.avgDailyCents.toDisplay()), htmlEscape(calc.gross.toDisplay()));
  body += QStringLiteral("</table>");
  body += signatures(company.value());
  return Result<QString>::ok(
      wrapHtml(QStringLiteral("Наказ про відпустку"), body, Paper::Portrait));
}

Result<QString> PrintForms::sickLeaveCalcHtml(const SickLeaveCalc & calc,
                                              const Employee & emp) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString body;
  body += QStringLiteral("<h1>Розрахунок лікарняних</h1>");
  body += companyBlock(company.value());
  body +=
      QStringLiteral("<div class=\"meta\">Працівник: <b>%1</b></div>").arg(htmlEscape(emp.name));
  if (!emp.position.isEmpty()) {
    body += QStringLiteral("<div class=\"meta\">Посада: %1</div>").arg(htmlEscape(emp.position));
  }
  body += QStringLiteral("<div class=\"meta\">Період непрацездатності: з %1 по %2</div>")
              .arg(htmlEscape(calc.sickFrom.toString(QStringLiteral("dd.MM.yyyy"))),
                   htmlEscape(calc.sickTo.toString(QStringLiteral("dd.MM.yyyy"))));
  body += QStringLiteral("<div class=\"meta\">Страховий стаж: %1 р.</div>")
              .arg(QString::number(calc.experienceYears));
  body += tableOpen();
  body += QStringLiteral(
      "<tr><th>Показник</th><th class=\"num\">Значення</th></tr>"
      "<tr><td>Календарних днів усього</td><td class=\"num\">%1</td></tr>"
      "<tr><td>За рахунок роботодавця (перші 5 днів)</td><td class=\"num\">%2</td></tr>"
      "<tr><td>За рахунок ПФУ (не в проводках Фініка)</td><td class=\"num\">%3</td></tr>"
      "<tr><td>Середньоденний заробіток</td><td class=\"num\">%4</td></tr>"
      "<tr><td>Нараховано роботодавцем (брутто)</td><td class=\"num\"><b>%5</b></td></tr>"
      "<tr><td>ЄСВ</td><td class=\"num\">%6</td></tr>"
      "<tr><td>ПДФО</td><td class=\"num\">%7</td></tr>"
      "<tr><td>Військовий збір</td><td class=\"num\">%8</td></tr>")
      .arg(QString::number(calc.totalDays), QString::number(calc.employerDays),
           QString::number(calc.pfuDays), htmlEscape(calc.avgDailyCents.toDisplay()),
           htmlEscape(calc.employerGross.toDisplay()), htmlEscape(calc.taxes.ssc.toDisplay()),
           htmlEscape(calc.taxes.pit.toDisplay()), htmlEscape(calc.taxes.military.toDisplay()));
  body += QStringLiteral("</table>");
  body += QStringLiteral(
      "<p class=\"muted\">Частина ПФУ у цьому документі не проводиться — лише дні та сума "
      "роботодавця (перші 5 календарних днів).</p>");
  body += signatures(company.value());
  return Result<QString>::ok(
      wrapHtml(QStringLiteral("Розрахунок лікарняних"), body, Paper::Portrait));
}

Result<QString> PrintForms::hrOrderHtml(const HrOrder & order, const Employee & emp) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  QString formCode, title, content;
  if (order.orderType == HrOrderType::Hire) {
    formCode = QStringLiteral("Типова форма № П-1");
    title = QStringLiteral("Наказ (розпорядження) про прийняття на роботу");
    content =
        QStringLiteral(
            "Прийняти на роботу з <b>%1</b> на посаду <b>%2</b> з посадовим окладом <b>%3</b> грн.")
            .arg(htmlEscape(order.effectiveDate.toString(QStringLiteral("dd.MM.yyyy"))),
                 htmlEscape(order.newPosition), htmlEscape(order.newSalary.toDisplay()));
  } else if (order.orderType == HrOrderType::Transfer) {
    formCode = QStringLiteral("Типова форма № П-5 (переведення)");
    title = QStringLiteral("Наказ (розпорядження) про переведення");
    content = QStringLiteral(
                  "Перевести з <b>%1</b> на посаду <b>%2</b> з посадовим окладом <b>%3</b> грн.")
                  .arg(htmlEscape(order.effectiveDate.toString(QStringLiteral("dd.MM.yyyy"))),
                       htmlEscape(order.newPosition), htmlEscape(order.newSalary.toDisplay()));
  } else if (order.orderType == HrOrderType::Dismiss) {
    formCode = QStringLiteral("Типова форма № П-8");
    title = QStringLiteral("Наказ (розпорядження) про припинення трудового договору (звільнення)");
    content = QStringLiteral("Звільнити <b>%1</b> з посади <b>%2</b>. Причина: %3.")
                  .arg(htmlEscape(order.effectiveDate.toString(QStringLiteral("dd.MM.yyyy"))),
                       htmlEscape(emp.position), htmlEscape(order.dismissReason));
  }
  QString body;
  body += QStringLiteral("<div class=\"formcode\">%1</div>").arg(htmlEscape(formCode));
  body += QStringLiteral("<h1>%1</h1>").arg(htmlEscape(title));
  body += companyBlock(company.value());
  body += QStringLiteral("<div class=\"meta\">Працівник: <b>%1</b></div><br/>")
              .arg(htmlEscape(emp.name));
  body += QStringLiteral("<p>%1</p>").arg(content);
  body += QStringLiteral("<br/><div class=\"meta\">Керівник підприємства _________________</div>");
  body += QStringLiteral("<div class=\"meta\">З наказом ознайомлений _________________</div>");
  return Result<QString>::ok(wrapHtml(QStringLiteral("Кадровий наказ"), body, Paper::Portrait));
}

} // namespace fin
