#include "fin/edo/TaxInvoiceXml.h"

#include "fin/auth/Access.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/CompanyRepository.h"
#include "fin/db/CounterpartyRepository.h"
#include "fin/db/DocumentRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"

#include <QCryptographicHash>
#include <QStringList>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace fin {
namespace {

QString digits(const QString & text)
{
  QString out;
  for (const QChar c : text.trimmed()) {
    if (c.isDigit()) {
      out += c;
    }
  }
  return out;
}

bool sameTin(const QString & left, const QString & right)
{
  const QString a = digits(left);
  const QString b = digits(right);
  return !a.isEmpty() && a == b;
}

VatDirection directionOf(const Document & document, const std::vector<Account> & accounts)
{
  for (const DocumentLine & line : document.lines) {
    if (!isVatObligationLine(accounts, line)) {
      continue;
    }
    if (accountHasCode(accounts, line.creditAccountId, chart::kVatLiability) ||
        accountHasCode(accounts, line.debitAccountId, chart::kVatLiability)) {
      return VatDirection::Output;
    }
    return VatDirection::Input;
  }
  return VatDirection::Output;
}

Money obligationVat(const Document & document, const std::vector<Account> & accounts)
{
  Money vat = Money::zero();
  for (const DocumentLine & line : document.lines) {
    if (!isVatObligationLine(accounts, line)) {
      continue;
    }
    const Result<Money> next = vat.plus(line.amount);
    if (!next) {
      return vat;
    }
    vat = next.value();
  }
  return vat;
}

QString textOf(QXmlStreamReader & reader, QStringList & stack)
{
  const QString text = reader.readElementText().trimmed();
  if (!stack.isEmpty()) {
    stack.removeLast();
  }
  return text;
}

Counterparty ensureParty(QSqlDatabase db, const QString & tin, const QString & name, bool supplier)
{
  const QString code = digits(tin);
  CounterpartyRepository parties(db);
  const Result<Counterparty> existing = parties.findByEdrpou(code);
  if (existing) {
    return existing.value();
  }
  Counterparty row;
  row.edrpou = code;
  row.name = name.trimmed().isEmpty() ? QStringLiteral("ЄДРПОУ %1").arg(code) : name.trimmed();
  row.isCustomer = true;
  row.isSupplier = supplier;
  const Result<CounterpartyId> id = parties.insert(row);
  if (!id) {
    row.id = {};
    return row;
  }
  row.id = id.value();
  return row;
}

Result<std::vector<DocumentLine>> incomingObligation(const std::vector<Account> & accounts,
                                                     Money vat)
{
  const AccountId credit = accountIdByCode(accounts, chart::kVatCredit);
  const AccountId suppliers = accountIdByCode(accounts, chart::kSuppliers);
  if (!credit || !suppliers) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 644 або 631")));
  }
  if (!vat.isPositive()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у XML немає суми ПДВ")));
  }
  DocumentLine line;
  line.debitAccountId = credit;
  line.creditAccountId = suppliers;
  line.amount = vat;
  line.description = QStringLiteral("ПДВ, податковий кредит (імпорт XML)");
  return Result<std::vector<DocumentLine>>::ok({line});
}

} // namespace

QString sha256HexUtf8(const QString & text)
{
  const QByteArray hash = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
  return QString::fromLatin1(hash.toHex());
}

Result<QString> buildTaxInvoiceXml(const Document & document, const CompanyProfile & company,
                                   const Counterparty & party,
                                   const std::vector<Account> & accounts)
{
  if (document.type != DocumentType::TaxInvoice) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("XML будується лише для податкової накладної")));
  }
  if (digits(company.edrpou).isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("у картці підприємства немає ЄДРПОУ")));
  }
  if (!party.id || digits(party.edrpou).isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("у контрагента немає ЄДРПОУ / ІПН")));
  }
  if (company.vatRateBp <= 0 || company.vatRateBp > 10000) {
    return Result<QString>::fail(Error::validation(
        QStringLiteral("у картці підприємства не задано базову ставку ПДВ (0 < ставка ≤ 100%)")));
  }
  const Money vat = obligationVat(document, accounts);
  if (!vat.isPositive()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("податкова накладна не містить суми ПДВ")));
  }
  const VatDirection direction = directionOf(document, accounts);
  const bool outgoing = direction == VatDirection::Output;
  QString xml;
  QXmlStreamWriter writer(&xml);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  writer.writeStartDocument(QStringLiteral("1.0"), true);
  writer.writeStartElement(QStringLiteral("TaxInvoice"));
  writer.writeDefaultNamespace(QStringLiteral("urn:finik:edo:j12"));
  writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1"));
  writer.writeTextElement(QStringLiteral("Number"), document.number.trimmed());
  writer.writeTextElement(QStringLiteral("Date"), document.date.toString(Qt::ISODate));
  writer.writeTextElement(QStringLiteral("Direction"),
                          outgoing ? QStringLiteral("Output") : QStringLiteral("Input"));
  writer.writeStartElement(QStringLiteral("Seller"));
  writer.writeTextElement(QStringLiteral("Name"),
                          outgoing ? company.name.trimmed() : party.name.trimmed());
  writer.writeTextElement(QStringLiteral("Tin"),
                          outgoing ? digits(company.edrpou) : digits(party.edrpou));
  writer.writeEndElement();
  writer.writeStartElement(QStringLiteral("Buyer"));
  writer.writeTextElement(QStringLiteral("Name"),
                          outgoing ? party.name.trimmed() : company.name.trimmed());
  writer.writeTextElement(QStringLiteral("Tin"),
                          outgoing ? digits(party.edrpou) : digits(company.edrpou));
  writer.writeEndElement();
  writer.writeTextElement(QStringLiteral("VatCents"), QString::number(vat.cents()));
  writer.writeTextElement(QStringLiteral("VatRateBp"), QString::number(company.vatRateBp));
  if (document.basedOnId) {
    writer.writeTextElement(QStringLiteral("SourceId"), QString::number(document.basedOnId.value));
  }
  writer.writeEndElement();
  writer.writeEndDocument();
  return Result<QString>::ok(xml);
}

Result<ParsedTaxInvoiceXml> parseTaxInvoiceXml(const QString & xml)
{
  if (xml.trimmed().isEmpty()) {
    return Result<ParsedTaxInvoiceXml>::fail(
        Error::validation(QStringLiteral("порожній XML податкової накладної")));
  }
  QXmlStreamReader reader(xml);
  ParsedTaxInvoiceXml parsed;
  QStringList stack;
  while (!reader.atEnd()) {
    const QXmlStreamReader::TokenType token = reader.readNext();
    if (token == QXmlStreamReader::StartElement) {
      const QString name = reader.name().toString();
      stack.push_back(name);
      const QString parent = stack.size() >= 2 ? stack.at(stack.size() - 2) : QString();
      if (name == QLatin1String("Number")) {
        parsed.number = textOf(reader, stack);
      } else if (name == QLatin1String("Date")) {
        parsed.date = QDate::fromString(textOf(reader, stack), Qt::ISODate);
      } else if (name == QLatin1String("VatCents")) {
        bool ok = false;
        const qlonglong cents = textOf(reader, stack).toLongLong(&ok);
        if (!ok || cents <= 0) {
          return Result<ParsedTaxInvoiceXml>::fail(
              Error::validation(QStringLiteral("некоректна сума ПДВ у XML")));
        }
        parsed.vat = Money::fromCents(cents);
      } else if (name == QLatin1String("VatRateBp")) {
        parsed.vatRateBp = textOf(reader, stack).toInt();
      } else if (name == QLatin1String("NetCents")) {
        bool ok = false;
        const qlonglong cents = textOf(reader, stack).toLongLong(&ok);
        if (ok && cents > 0) {
          parsed.net = Money::fromCents(cents);
        }
      } else if (name == QLatin1String("SourceNumber")) {
        parsed.sourceNumber = textOf(reader, stack);
      } else if (name == QLatin1String("SourceType")) {
        parsed.sourceType = textOf(reader, stack);
      } else if (name == QLatin1String("Name") || name == QLatin1String("Tin")) {
        const QString value = textOf(reader, stack);
        if (parent == QLatin1String("Seller")) {
          if (name == QLatin1String("Name")) {
            parsed.sellerName = value;
          } else {
            parsed.sellerTin = digits(value);
          }
        } else if (parent == QLatin1String("Buyer")) {
          if (name == QLatin1String("Name")) {
            parsed.buyerName = value;
          } else {
            parsed.buyerTin = digits(value);
          }
        }
      }
    } else if (token == QXmlStreamReader::EndElement) {
      if (!stack.isEmpty()) {
        stack.removeLast();
      }
    }
  }
  if (reader.hasError()) {
    return Result<ParsedTaxInvoiceXml>::fail(Error::validation(
        QStringLiteral("XML податкової накладної: %1").arg(reader.errorString())));
  }
  if (parsed.number.trimmed().isEmpty() || !parsed.date.isValid() || parsed.sellerTin.isEmpty() ||
      parsed.buyerTin.isEmpty() || !parsed.vat.isPositive()) {
    return Result<ParsedTaxInvoiceXml>::fail(
        Error::validation(QStringLiteral("у XML немає номера, дати, ІПН сторін або суми ПДВ")));
  }
  return Result<ParsedTaxInvoiceXml>::ok(parsed);
}

Status setDocumentEdoPayload(QSqlDatabase db, DocumentId id, const QString & xml, bool imported)
{
  Status access = Access::requireDocumentWrite(db, DocumentType::TaxInvoice);
  if (!access) {
    return access;
  }
  if (!id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id ПН")));
  }
  if (xml.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("порожній XML податкової накладної")));
  }
  return DocumentRepository(db).setEdoPayload(id, xml, sha256HexUtf8(xml), imported);
}

Result<QString> exportTaxInvoiceXml(QSqlDatabase db, DocumentId id)
{
  Status access = Access::requireDocumentWrite(db, DocumentType::TaxInvoice);
  if (!access) {
    return Result<QString>::fail(access.error());
  }
  const Result<Document> document = DocumentRepository(db).findById(id);
  if (!document) {
    return Result<QString>::fail(document.error());
  }
  if (document.value().type != DocumentType::TaxInvoice) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("експорт XML лише для податкової накладної")));
  }
  const Result<CompanyProfile> company = CompanyRepository(db).load();
  if (!company) {
    return Result<QString>::fail(company.error());
  }
  if (!company.value().vatPayer) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("XML податкової накладної формує лише платник ПДВ")));
  }
  if (!document.value().counterpartyId) {
    return Result<QString>::fail(Error::validation(QStringLiteral("у ПН немає контрагента")));
  }
  const Result<Counterparty> party =
      CounterpartyRepository(db).findById(document.value().counterpartyId);
  if (!party) {
    return Result<QString>::fail(party.error());
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db).listPostable();
  if (!accounts) {
    return Result<QString>::fail(accounts.error());
  }
  const Result<QString> xml =
      buildTaxInvoiceXml(document.value(), company.value(), party.value(), accounts.value());
  if (!xml) {
    return xml;
  }
  Status stored = setDocumentEdoPayload(db, id, xml.value(), false);
  if (!stored) {
    return Result<QString>::fail(stored.error());
  }
  return xml;
}

Result<EdoImportResult> importTaxInvoiceXml(QSqlDatabase db, const QString & xml)
{
  Status access = Access::requireDocumentWrite(db, DocumentType::TaxInvoice);
  if (!access) {
    return Result<EdoImportResult>::fail(access.error());
  }
  const Result<CompanyProfile> company = CompanyRepository(db).load();
  if (!company) {
    return Result<EdoImportResult>::fail(company.error());
  }
  if (!company.value().vatPayer) {
    return Result<EdoImportResult>::fail(
        Error::validation(QStringLiteral("імпорт ПН доступний лише платнику ПДВ")));
  }
  const QString ours = digits(company.value().edrpou);
  if (ours.isEmpty()) {
    return Result<EdoImportResult>::fail(
        Error::validation(QStringLiteral("у картці підприємства немає ЄДРПОУ")));
  }
  const Result<ParsedTaxInvoiceXml> parsed = parseTaxInvoiceXml(xml);
  if (!parsed) {
    return Result<EdoImportResult>::fail(parsed.error());
  }
  DocumentRepository documents(db);
  if (sameTin(parsed.value().sellerTin, ours)) {
    const Result<Document> existing =
        documents.findByTypeAndNumber(DocumentType::TaxInvoice, parsed.value().number);
    if (!existing) {
      return Result<EdoImportResult>::fail(
          Error::notFound(QStringLiteral("немає податкової накладної %1 для прикріплення XML")
                              .arg(parsed.value().number)));
    }
    Status stored = setDocumentEdoPayload(db, existing.value().id, xml, true);
    if (!stored) {
      return Result<EdoImportResult>::fail(stored.error());
    }
    EdoImportResult result;
    result.documentId = existing.value().id;
    result.kind = EdoImportKind::AttachedOutgoing;
    return Result<EdoImportResult>::ok(result);
  }
  if (!sameTin(parsed.value().buyerTin, ours)) {
    return Result<EdoImportResult>::fail(Error::validation(
        QStringLiteral("ЄДРПОУ підприємства не збігається з продавцем або покупцем XML")));
  }

  const Result<std::vector<Account>> accounts = AccountRepository(db).listPostable();
  if (!accounts) {
    return Result<EdoImportResult>::fail(accounts.error());
  }
  Counterparty party = ensureParty(db, parsed.value().sellerTin, parsed.value().sellerName, true);
  if (!party.id) {
    return Result<EdoImportResult>::fail(
        Error::validation(QStringLiteral("не вдалося створити контрагента з XML")));
  }
  const Result<std::vector<DocumentLine>> lines =
      incomingObligation(accounts.value(), parsed.value().vat);
  if (!lines) {
    return Result<EdoImportResult>::fail(lines.error());
  }

  Document document;
  document.type = DocumentType::TaxInvoice;
  document.date = parsed.value().date;
  document.counterpartyId = party.id;
  document.edoImported = true;
  document.edoXml = xml;
  document.edoSha256 = sha256HexUtf8(xml);
  document.comment = QStringLiteral("Імпорт XML від %1").arg(party.name);
  document.lines = lines.value();

  const Result<Document> clash =
      documents.findByTypeAndNumber(DocumentType::TaxInvoice, parsed.value().number);
  if (clash) {
    const Result<QString> allocated = documents.allocateNumber(DocumentType::TaxInvoice);
    if (!allocated) {
      return Result<EdoImportResult>::fail(allocated.error());
    }
    document.number = allocated.value();
    document.comment += QStringLiteral("; номер продавця %1").arg(parsed.value().number);
  } else if (clash.error().code == ErrorCode::NotFound) {
    document.number = parsed.value().number;
  } else {
    return Result<EdoImportResult>::fail(clash.error());
  }

  const Result<DocumentId> id = documents.insert(document);
  if (!id) {
    return Result<EdoImportResult>::fail(id.error());
  }
  EdoImportResult result;
  result.documentId = id.value();
  result.kind = EdoImportKind::CreatedIncoming;
  return Result<EdoImportResult>::ok(result);
}

} // namespace fin
