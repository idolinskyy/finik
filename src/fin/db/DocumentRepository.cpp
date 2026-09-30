#include "fin/db/DocumentRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"
#include "fin/core/Transaction.h"
#include "fin/db/AuditLog.h"

#include "fin/db/PeriodRepository.h"
#include <QSettings>

#include <QDateTime>
#include <QVariant>

namespace fin {
namespace {

QString iso(QDate date)
{
  return date.toString(Qt::ISODate);
}

DocumentHeader headerFromQuery(sql::Query & query)
{
  DocumentHeader header;
  header.id = DocumentId{query.value(0).toLongLong()};
  const auto type = documentTypeFromString(query.value(1).toString());
  header.type = type.value_or(DocumentType::Manual);
  header.number = query.value(2).toString();
  header.date = QDate::fromString(query.value(3).toString(), Qt::ISODate);
  const std::int64_t cp = query.value(4).toLongLong();
  if (cp > 0) {
    header.counterpartyId = CounterpartyId{cp};
  }
  header.counterpartyName = query.value(5).toString();
  header.posted = query.value(6).toInt() != 0;
  header.total = Money::fromCents(query.value(7).toLongLong());
  header.comment = query.value(8).toString();
  header.currencyCode = query.value(9).toString();
  return header;
}

} // namespace

DocumentRepository::DocumentRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<DocumentType> DocumentRepository::typeOf(DocumentId id) const
{
  if (!id) {
    return Result<DocumentType>::fail(
        Error::invalidArgument(QStringLiteral("порожній id документа")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral("SELECT type FROM documents WHERE id = :id"));
  if (!status) {
    return Result<DocumentType>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<DocumentType>::fail(status.error());
  }
  if (!query.next()) {
    return Result<DocumentType>::fail(
        Error::notFound(QStringLiteral("документ %1 не знайдено").arg(id.value)));
  }
  const auto type = documentTypeFromString(query.value(0).toString());
  if (!type) {
    return Result<DocumentType>::fail(
        Error::validation(QStringLiteral("невідомий тип документа %1").arg(id.value)));
  }
  return Result<DocumentType>::ok(type.value());
}

Status DocumentRepository::requireWriteAccess(DocumentId id) const
{
  const Result<DocumentType> type = typeOf(id);
  if (!type) {
    return type.asStatus();
  }
  return Access::requireDocumentWrite(db_, type.value());
}

Status DocumentRepository::requireWritableDraft(DocumentId id) const
{
  const Result<Document> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  const Status access = Access::requireDocumentWrite(db_, existing.value().type);
  if (!access) {
    return access;
  }
  if (existing.value().posted) {
    return Status::fail(
        Error::alreadyPosted(QStringLiteral("документ проведено — спочатку зніміть проведення")));
  }
  return Status::ok();
}

Result<std::vector<DocumentLine>> DocumentRepository::loadLines(DocumentId id) const
{
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("SELECT line_no, debit_account_id, credit_account_id, "
                     "amount_cents, description, nomenclature_id, quantity_milli, "
                     "price_cents, fixed_asset_id, low_value_id, intangible_id, "
                     "vat_rate_bp, vat_cents, posting_date, employee_id, warehouse_id, "
                     "counterparty_id "
                     "FROM document_lines "
                     "WHERE document_id = :id ORDER BY line_no"));
  if (!status) {
    return Result<std::vector<DocumentLine>>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<std::vector<DocumentLine>>::fail(status.error());
  }
  std::vector<DocumentLine> lines;
  while (query.next()) {
    DocumentLine line;
    line.lineNo = query.value(0).toInt();
    line.debitAccountId = AccountId{query.value(1).toLongLong()};
    line.creditAccountId = AccountId{query.value(2).toLongLong()};
    line.amount = Money::fromCents(query.value(3).toLongLong());
    line.description = query.value(4).toString();
    const std::int64_t nom = query.value(5).toLongLong();
    if (nom > 0) {
      line.nomenclatureId = NomenclatureId{nom};
    }
    line.quantity = Quantity::fromMilli(query.value(6).toLongLong());
    line.unitPrice = Money::fromCents(query.value(7).toLongLong());
    const std::int64_t fa = query.value(8).toLongLong();
    if (fa > 0) {
      line.fixedAssetId = FixedAssetId{fa};
    }
    const std::int64_t ms = query.value(9).toLongLong();
    if (ms > 0) {
      line.lowValueId = LowValueId{ms};
    }
    const std::int64_t ia = query.value(10).toLongLong();
    if (ia > 0) {
      line.intangibleId = IntangibleId{ia};
    }
    line.vatRateBp = query.value(11).toInt();
    line.vatCents = query.value(12).toLongLong();
    const QString posting = query.value(13).toString();
    if (!posting.trimmed().isEmpty()) {
      line.postingDate = QDate::fromString(posting, Qt::ISODate);
    }
    const std::int64_t emp = query.value(14).toLongLong();
    if (emp > 0) {
      line.employeeId = EmployeeId{emp};
    }
    const std::int64_t wh = query.value(15).toLongLong();
    if (wh > 0) {
      line.warehouseId = WarehouseId{wh};
    }
    const std::int64_t cp = query.value(16).toLongLong();
    if (cp > 0) {
      line.counterpartyId = CounterpartyId{cp};
    }
    lines.push_back(line);
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<Document> DocumentRepository::findById(DocumentId id) const
{
  if (!id) {
    return Result<Document>::fail(Error::invalidArgument(QStringLiteral("порожній id документа")));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("SELECT id, type, number, date, counterparty_id, comment, posted, "
                     "received_by, identity_doc, based_on_id, vat_registration_date, "
                     "vat_offset_cents, edo_xml, edo_sha256, edo_imported, "
                     "warehouse_id, target_warehouse_id, contract_id, order_status, "
                     "currency_code, currency_rate_micros, currency_nominal, "
                     "bom_id, employee_id, fiscal_receipt_id, fiscal_code, fiscal_status "
                     "FROM documents WHERE id = :id"));
  if (!status) {
    return Result<Document>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<Document>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Document>::fail(Error::notFound(QStringLiteral("документ не знайдено")));
  }
  Document document;
  document.id = DocumentId{query.value(0).toLongLong()};
  const auto type = documentTypeFromString(query.value(1).toString());
  if (!type) {
    return Result<Document>::fail(Error::database(QStringLiteral("невідомий тип документа")));
  }
  document.type = *type;
  document.number = query.value(2).toString();
  document.date = QDate::fromString(query.value(3).toString(), Qt::ISODate);
  const std::int64_t cp = query.value(4).toLongLong();
  if (cp > 0) {
    document.counterpartyId = CounterpartyId{cp};
  }
  document.comment = query.value(5).toString();
  document.posted = query.value(6).toInt() != 0;
  document.receivedBy = query.value(7).toString();
  document.identityDoc = query.value(8).toString();
  const std::int64_t basedOn = query.value(9).toLongLong();
  if (basedOn > 0) {
    document.basedOnId = DocumentId{basedOn};
  }
  const QString registered = query.value(10).toString();
  if (!registered.trimmed().isEmpty()) {
    document.vatRegistrationDate = QDate::fromString(registered, Qt::ISODate);
  }
  document.vatOffsetCents = query.value(11).toLongLong();
  document.edoXml = query.value(12).toString();
  document.edoSha256 = query.value(13).toString();
  document.edoImported = query.value(14).toInt() != 0;
  const std::int64_t wh = query.value(15).toLongLong();
  if (wh > 0) {
    document.warehouseId = WarehouseId{wh};
  }
  const std::int64_t twh = query.value(16).toLongLong();
  if (twh > 0) {
    document.targetWarehouseId = WarehouseId{twh};
  }
  const std::int64_t ct = query.value(17).toLongLong();
  if (ct > 0) {
    document.contractId = ContractId{ct};
  }
  document.orderStatus = query.value(18).toString();
  document.currencyCode = query.value(19).toString();
  document.currencyRateMicros = query.value(20).toLongLong();
  document.currencyNominal = query.value(21).toInt();
  if (document.currencyNominal <= 0) {
    document.currencyNominal = 1;
  }
  const std::int64_t bom = query.value(22).toLongLong();
  if (bom > 0) {
    document.bomId = BomId{bom};
  }
  const std::int64_t emp = query.value(23).toLongLong();
  if (emp > 0) {
    document.employeeId = EmployeeId{emp};
  }
  document.fiscalReceiptId = query.value(24).toString();
  document.fiscalCode = query.value(25).toString();
  document.fiscalStatus = query.value(26).toString();

  auto lines = loadLines(document.id);
  if (!lines) {
    return Result<Document>::fail(lines.error());
  }
  document.lines = std::move(lines.value());
  return Result<Document>::ok(std::move(document));
}

Result<std::vector<DocumentHeader>> DocumentRepository::list(QDate from, QDate to,
                                                             std::optional<DocumentType> type,
                                                             CounterpartyId counterparty) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<std::vector<DocumentHeader>>::fail(
        Error::invalidArgument(QStringLiteral("некоректний період")));
  }
  QString sql = QStringLiteral("SELECT d.id, d.type, d.number, d.date, d.counterparty_id, "
                               "COALESCE(c.name, ''), d.posted, "
                               "COALESCE(SUM(CASE "
                               "  WHEN LOWER(da.code) = '681' AND LOWER(ca.code) = '361' THEN 0 "
                               "  WHEN LOWER(da.code) = '631' AND LOWER(ca.code) = '371' THEN 0 "
                               "  WHEN LOWER(da.code) = '643' AND LOWER(ca.code) = '641' THEN 0 "
                               "  WHEN LOWER(da.code) = '641' AND LOWER(ca.code) = '644' THEN 0 "
                               "  WHEN LOWER(da.code) = '641' AND LOWER(ca.code) = '643' THEN 0 "
                               "  WHEN LOWER(da.code) = '644' AND LOWER(ca.code) = '641' THEN 0 "
                               "  WHEN LOWER(da.code) = '661' AND LOWER(ca.code) = '642' THEN 0 "
                               "  WHEN LOWER(da.code) = '661' AND LOWER(ca.code) = '645' THEN 0 "
                               "  ELSE l.amount_cents END), 0), "
                               "COALESCE(d.comment, ''), "
                               "COALESCE(d.currency_code, '') "
                               "FROM documents d "
                               "LEFT JOIN counterparties c ON c.id = d.counterparty_id "
                               "LEFT JOIN document_lines l ON l.document_id = d.id "
                               "LEFT JOIN accounts da ON da.id = l.debit_account_id "
                               "LEFT JOIN accounts ca ON ca.id = l.credit_account_id "
                               "WHERE d.date >= :from AND d.date <= :to");
  if (type.has_value()) {
    sql += QStringLiteral(" AND d.type = :type");
  }
  if (counterparty) {
    sql += QStringLiteral(" AND d.counterparty_id = :cp");
  }
  sql += QStringLiteral(" GROUP BY d.id ORDER BY d.date, d.number");

  sql::Query query(db_);
  Status status = query.prepare(sql);
  if (!status) {
    return Result<std::vector<DocumentHeader>>::fail(status.error());
  }
  query.bind(QStringLiteral(":from"), iso(from));
  query.bind(QStringLiteral(":to"), iso(to));
  if (type.has_value()) {
    query.bind(QStringLiteral(":type"), documentTypeToString(*type));
  }
  if (counterparty) {
    query.bind(QStringLiteral(":cp"), static_cast<qlonglong>(counterparty.value));
  }
  status = query.exec();
  if (!status) {
    return Result<std::vector<DocumentHeader>>::fail(status.error());
  }
  std::vector<DocumentHeader> rows;
  while (query.next()) {
    rows.push_back(headerFromQuery(query));
  }
  return Result<std::vector<DocumentHeader>>::ok(std::move(rows));
}

Result<std::vector<DocumentHeader>> DocumentRepository::listRecent(int limit) const
{
  if (limit <= 0) {
    limit = 10;
  }
  QString sql = QStringLiteral("SELECT d.id, d.type, d.number, d.date, d.counterparty_id, "
                               "COALESCE(c.name, ''), d.posted, "
                               "COALESCE(SUM(CASE "
                               "  WHEN LOWER(da.code) = '681' AND LOWER(ca.code) = '361' THEN 0 "
                               "  WHEN LOWER(da.code) = '631' AND LOWER(ca.code) = '371' THEN 0 "
                               "  WHEN LOWER(da.code) = '643' AND LOWER(ca.code) = '641' THEN 0 "
                               "  WHEN LOWER(da.code) = '641' AND LOWER(ca.code) = '644' THEN 0 "
                               "  WHEN LOWER(da.code) = '641' AND LOWER(ca.code) = '643' THEN 0 "
                               "  WHEN LOWER(da.code) = '644' AND LOWER(ca.code) = '641' THEN 0 "
                               "  WHEN LOWER(da.code) = '661' AND LOWER(ca.code) = '642' THEN 0 "
                               "  WHEN LOWER(da.code) = '661' AND LOWER(ca.code) = '645' THEN 0 "
                               "  ELSE l.amount_cents END), 0), "
                               "COALESCE(d.comment, ''), "
                               "COALESCE(d.currency_code, '') "
                               "FROM documents d "
                               "LEFT JOIN counterparties c ON c.id = d.counterparty_id "
                               "LEFT JOIN document_lines l ON l.document_id = d.id "
                               "LEFT JOIN accounts da ON da.id = l.debit_account_id "
                               "LEFT JOIN accounts ca ON ca.id = l.credit_account_id "
                               "GROUP BY d.id ORDER BY d.id DESC LIMIT :limit");

  sql::Query query(db_);
  Status status = query.prepare(sql);
  if (!status) {
    return Result<std::vector<DocumentHeader>>::fail(status.error());
  }
  query.bind(QStringLiteral(":limit"), limit);
  status = query.exec();
  if (!status) {
    return Result<std::vector<DocumentHeader>>::fail(status.error());
  }
  std::vector<DocumentHeader> rows;
  while (query.next()) {
    rows.push_back(headerFromQuery(query));
  }
  return Result<std::vector<DocumentHeader>>::ok(std::move(rows));
}

Result<int> DocumentRepository::count() const
{
  sql::Query query(db_);
  Status status = query.exec(QStringLiteral("SELECT COUNT(*) FROM documents"));
  if (!status) {
    return Result<int>::fail(status.error());
  }
  if (!query.next()) {
    return Result<int>::fail(Error::database(QStringLiteral("не вдалося полічити документи")));
  }
  return Result<int>::ok(query.value(0).toInt());
}

Result<QString> DocumentRepository::allocateNumber(DocumentType type)
{
  Status access = Access::requireDocumentWrite(db_, type);
  if (!access) {
    return Result<QString>::fail(access.error());
  }
  Transaction txn(db_);
  if (!txn.started()) {
    return Result<QString>::fail(txn.startError().error());
  }

  const QString typeKey = documentTypeToString(type);
  sql::Query select(db_);
  Status status = select.prepare(
      QStringLiteral("SELECT last_number FROM document_sequences WHERE type = :type"));
  if (!status) {
    return Result<QString>::fail(status.error());
  }
  select.bind(QStringLiteral(":type"), typeKey);
  status = select.exec();
  if (!status) {
    return Result<QString>::fail(status.error());
  }

  int next = 1;
  if (select.next()) {
    next = select.value(0).toInt() + 1;
    sql::Query update(db_);
    status = update.prepare(
        QStringLiteral("UPDATE document_sequences SET last_number = :n WHERE type = :type"));
    if (!status) {
      return Result<QString>::fail(status.error());
    }
    update.bind(QStringLiteral(":n"), next);
    update.bind(QStringLiteral(":type"), typeKey);
    status = update.exec();
    if (!status) {
      return Result<QString>::fail(status.error());
    }
  } else {
    sql::Query insert(db_);
    status = insert.prepare(
        QStringLiteral("INSERT INTO document_sequences(type, last_number) VALUES(:type, :n)"));
    if (!status) {
      return Result<QString>::fail(status.error());
    }
    insert.bind(QStringLiteral(":type"), typeKey);
    insert.bind(QStringLiteral(":n"), next);
    status = insert.exec();
    if (!status) {
      return Result<QString>::fail(status.error());
    }
  }

  status = txn.commit();
  if (!status) {
    return Result<QString>::fail(status.error());
  }

  QSettings settings;
  QString strat =
      settings.value(QStringLiteral("business/prefix_strategy"), QStringLiteral("standard"))
          .toString();
  QString customPrefix =
      settings.value(QStringLiteral("business/doc_prefix"), QStringLiteral("")).toString();

  QString prefix = documentTypePrefix(type);
  if (strat == QStringLiteral("global") && !customPrefix.isEmpty()) {
    prefix = customPrefix;
  } else if (strat == QStringLiteral("per_type") && !customPrefix.isEmpty()) {
    if (customPrefix.endsWith(QLatin1Char('-'))) {
      prefix = customPrefix + prefix;
    } else {
      prefix = customPrefix + QLatin1Char('-') + prefix;
    }
  }

  if (prefix.endsWith(QLatin1Char('-'))) {
    prefix.chop(1);
  }

  const QString number = QStringLiteral("%1-%2").arg(prefix).arg(next, 6, 10, QLatin1Char('0'));
  return Result<QString>::ok(number);
}

Status DocumentRepository::replaceLines(DocumentId id, const std::vector<DocumentLine> & lines,
                                        bool allowPosted)
{
  if (allowPosted) {
    const Result<Document> existing = findById(id);
    if (!existing) {
      return existing.asStatus();
    }
    const Status access = Access::requireDocumentWrite(db_, existing.value().type);
    if (!access) {
      return access;
    }
    if (existing.value().posted && existing.value().type != DocumentType::TaxInvoice) {
      return Status::fail(Error::alreadyPosted(QStringLiteral(
          "зміну рядків проведеного документа дозволено лише для податкової накладної")));
    }
  } else {
    const Status access = requireWritableDraft(id);
    if (!access) {
      return access;
    }
  }

  sql::Query del(db_);
  Status status = del.prepare(QStringLiteral("DELETE FROM document_lines WHERE document_id = :id"));
  if (!status) {
    return status;
  }
  del.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = del.exec();
  if (!status) {
    return status;
  }

  sql::Query insert(db_);
  status = insert.prepare(
      QStringLiteral("INSERT INTO document_lines(document_id, line_no, "
                     "debit_account_id, credit_account_id, amount_cents, description, "
                     "nomenclature_id, quantity_milli, price_cents, fixed_asset_id, "
                     "low_value_id, intangible_id, vat_rate_bp, vat_cents, posting_date, "
                     "employee_id, warehouse_id, counterparty_id) "
                     "VALUES(:doc, :no, :dt, :ct, :amt, :desc, :nom, :qty, :price, :fa, "
                     ":ms, :ia, :vatrate, :vat, :pdate, :emp, :wh, :cp)"));
  if (!status) {
    return status;
  }
  int lineNo = 1;
  for (const DocumentLine & line : lines) {
    insert.bind(QStringLiteral(":doc"), static_cast<qlonglong>(id.value));
    insert.bind(QStringLiteral(":no"), lineNo);
    insert.bind(QStringLiteral(":dt"), static_cast<qlonglong>(line.debitAccountId.value));
    insert.bind(QStringLiteral(":ct"), static_cast<qlonglong>(line.creditAccountId.value));
    insert.bind(QStringLiteral(":amt"), static_cast<qlonglong>(line.amount.cents()));
    insert.bind(QStringLiteral(":desc"), line.description);
    if (line.nomenclatureId) {
      insert.bind(QStringLiteral(":nom"), static_cast<qlonglong>(line.nomenclatureId.value));
    } else {
      insert.bind(QStringLiteral(":nom"), QVariant());
    }
    insert.bind(QStringLiteral(":qty"), static_cast<qlonglong>(line.quantity.milli()));
    insert.bind(QStringLiteral(":price"), static_cast<qlonglong>(line.unitPrice.cents()));
    if (line.fixedAssetId) {
      insert.bind(QStringLiteral(":fa"), static_cast<qlonglong>(line.fixedAssetId.value));
    } else {
      insert.bind(QStringLiteral(":fa"), QVariant());
    }
    if (line.lowValueId) {
      insert.bind(QStringLiteral(":ms"), static_cast<qlonglong>(line.lowValueId.value));
    } else {
      insert.bind(QStringLiteral(":ms"), QVariant());
    }
    if (line.intangibleId) {
      insert.bind(QStringLiteral(":ia"), static_cast<qlonglong>(line.intangibleId.value));
    } else {
      insert.bind(QStringLiteral(":ia"), QVariant());
    }
    insert.bind(QStringLiteral(":vatrate"), line.vatRateBp);
    insert.bind(QStringLiteral(":vat"), static_cast<qlonglong>(line.vatCents));
    if (line.postingDate.isValid()) {
      insert.bind(QStringLiteral(":pdate"), iso(line.postingDate));
    } else {
      insert.bind(QStringLiteral(":pdate"), QVariant());
    }
    if (line.employeeId) {
      insert.bind(QStringLiteral(":emp"), static_cast<qlonglong>(line.employeeId.value));
    } else {
      insert.bind(QStringLiteral(":emp"), QVariant());
    }
    if (line.warehouseId) {
      insert.bind(QStringLiteral(":wh"), static_cast<qlonglong>(line.warehouseId.value));
    } else {
      insert.bind(QStringLiteral(":wh"), QVariant());
    }
    if (line.counterpartyId) {
      insert.bind(QStringLiteral(":cp"), static_cast<qlonglong>(line.counterpartyId.value));
    } else {
      insert.bind(QStringLiteral(":cp"), QVariant());
    }
    status = insert.exec();
    if (!status) {
      return status;
    }
    ++lineNo;
  }
  return Status::ok();
}

Result<DocumentId> DocumentRepository::insert(const Document & document)
{
  Status access = Access::requireDocumentWrite(db_, document.type);
  if (!access) {
    return Result<DocumentId>::fail(access.error());
  }
  if (!document.date.isValid() || document.date.year() < 1991 || document.date.year() > 2100) {
    return Result<DocumentId>::fail(Error::validation(QStringLiteral("некоректна дата документа")));
  }
  Status writable = PeriodRepository(db_).ensureDateWritable(document.date);
  if (!writable) {
    return Result<DocumentId>::fail(writable.error());
  }
  if (document.number.trimmed().isEmpty()) {
    return Result<DocumentId>::fail(
        Error::validation(QStringLiteral("номер документа обов'язковий")));
  }

  Transaction txn(db_);
  if (!txn.started()) {
    return Result<DocumentId>::fail(txn.startError().error());
  }

  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("INSERT INTO documents(type, number, date, counterparty_id, comment, "
                     "posted, "
                     "received_by, identity_doc, based_on_id, vat_registration_date, "
                     "vat_offset_cents, "
                     "edo_xml, edo_sha256, edo_imported, warehouse_id, target_warehouse_id, "
                     "contract_id, "
                     "order_status, currency_code, currency_rate_micros, currency_nominal, "
                     "bom_id, employee_id) "
                     "VALUES(:type, :number, :date, :cp, :comment, 0, :recv, :ident, :based, "
                     ":vatreg, :vatoff, :edoxml, :edosha, :edoimp, :wh, :twh, :ct, :ordstat, "
                     ":currcode, :currrate, :currnom, :bom, :emp)"));
  if (!status) {
    return Result<DocumentId>::fail(status.error());
  }
  query.bind(QStringLiteral(":type"), documentTypeToString(document.type));
  query.bind(QStringLiteral(":number"), document.number.trimmed());
  query.bind(QStringLiteral(":date"), iso(document.date));
  if (document.counterpartyId) {
    query.bind(QStringLiteral(":cp"), static_cast<qlonglong>(document.counterpartyId.value));
  } else {
    query.bind(QStringLiteral(":cp"), QVariant());
  }
  query.bind(QStringLiteral(":comment"), document.comment);
  query.bind(QStringLiteral(":recv"), document.receivedBy.trimmed());
  query.bind(QStringLiteral(":ident"), document.identityDoc.trimmed());
  if (document.basedOnId) {
    query.bind(QStringLiteral(":based"), static_cast<qlonglong>(document.basedOnId.value));
  } else {
    query.bind(QStringLiteral(":based"), QVariant());
  }
  if (document.vatRegistrationDate.isValid()) {
    query.bind(QStringLiteral(":vatreg"), iso(document.vatRegistrationDate));
  } else {
    query.bind(QStringLiteral(":vatreg"), QVariant());
  }
  query.bind(QStringLiteral(":vatoff"), static_cast<qlonglong>(document.vatOffsetCents));
  query.bind(QStringLiteral(":edoxml"), document.edoXml);
  query.bind(QStringLiteral(":edosha"), document.edoSha256);
  query.bind(QStringLiteral(":edoimp"), document.edoImported ? 1 : 0);
  if (document.warehouseId) {
    query.bind(QStringLiteral(":wh"), static_cast<qlonglong>(document.warehouseId.value));
  } else {
    query.bind(QStringLiteral(":wh"), QVariant());
  }
  if (document.targetWarehouseId) {
    query.bind(QStringLiteral(":twh"), static_cast<qlonglong>(document.targetWarehouseId.value));
  } else {
    query.bind(QStringLiteral(":twh"), QVariant());
  }
  if (document.contractId) {
    query.bind(QStringLiteral(":ct"), static_cast<qlonglong>(document.contractId.value));
  } else {
    query.bind(QStringLiteral(":ct"), QVariant());
  }
  query.bind(QStringLiteral(":ordstat"), document.orderStatus.trimmed());
  if (!document.currencyCode.trimmed().isEmpty()) {
    query.bind(QStringLiteral(":currcode"), document.currencyCode.trimmed().toUpper());
    query.bind(QStringLiteral(":currrate"), static_cast<qlonglong>(document.currencyRateMicros));
    query.bind(QStringLiteral(":currnom"),
               document.currencyNominal > 0 ? document.currencyNominal : 1);
  } else {
    query.bind(QStringLiteral(":currcode"), QVariant());
    query.bind(QStringLiteral(":currrate"), QVariant());
    query.bind(QStringLiteral(":currnom"), 1);
  }
  if (document.bomId) {
    query.bind(QStringLiteral(":bom"), static_cast<qlonglong>(document.bomId.value));
  } else {
    query.bind(QStringLiteral(":bom"), QVariant());
  }
  if (document.employeeId) {
    query.bind(QStringLiteral(":emp"), static_cast<qlonglong>(document.employeeId.value));
  } else {
    query.bind(QStringLiteral(":emp"), QVariant());
  }
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Result<DocumentId>::fail(
          Error::conflict(QStringLiteral("документ з таким номером уже існує")));
    }
    return Result<DocumentId>::fail(status.error());
  }
  const DocumentId id{query.lastInsertId()};
  status = replaceLines(id, document.lines);
  if (!status) {
    return Result<DocumentId>::fail(status.error());
  }
  status = appendAudit(db_, QStringLiteral("insert"), QStringLiteral("document"), id.value,
                       documentTypeLabelUk(document.type) + QLatin1Char(' ') +
                           document.number.trimmed());
  if (!status) {
    return Result<DocumentId>::fail(status.error());
  }
  status = txn.commit();
  if (!status) {
    return Result<DocumentId>::fail(status.error());
  }
  return Result<DocumentId>::ok(id);
}

Status DocumentRepository::update(const Document & document)
{
  if (!document.id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id документа")));
  }
  Status access = Access::requireDocumentWrite(db_, document.type);
  if (!access) {
    return access;
  }
  if (!document.date.isValid() || document.date.year() < 1991 || document.date.year() > 2100) {
    return Status::fail(Error::validation(QStringLiteral("некоректна дата документа")));
  }

  Result<Document> existing = findById(document.id);
  if (!existing) {
    return existing.asStatus();
  }
  access = Access::requireDocumentWrite(db_, existing.value().type);
  if (!access) {
    return access;
  }
  if (existing.value().posted) {
    return Status::fail(
        Error::alreadyPosted(QStringLiteral("проведений документ не можна змінювати")));
  }

  PeriodRepository periods(db_);
  Status writable = periods.ensureDateWritable(existing.value().date);
  if (!writable) {
    return writable;
  }
  writable = periods.ensureDateWritable(document.date);
  if (!writable) {
    return writable;
  }

  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE documents SET type = :type, number = :number, date = :date, "
                     "counterparty_id = :cp, comment = :comment, received_by = :recv, "
                     "identity_doc = :ident, based_on_id = :based, "
                     "vat_registration_date = :vatreg, vat_offset_cents = :vatoff, "
                     "edo_xml = :edoxml, edo_sha256 = :edosha, edo_imported = :edoimp, "
                     "warehouse_id = :wh, target_warehouse_id = :twh, contract_id = :ct, "
                     "order_status = :ordstat, currency_code = :currcode, "
                     "currency_rate_micros = :currrate, currency_nominal = :currnom, "
                     "bom_id = :bom, employee_id = :emp "
                     "WHERE id = :id AND posted = 0"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":type"), documentTypeToString(document.type));
  query.bind(QStringLiteral(":number"), document.number.trimmed());
  query.bind(QStringLiteral(":date"), iso(document.date));
  if (document.counterpartyId) {
    query.bind(QStringLiteral(":cp"), static_cast<qlonglong>(document.counterpartyId.value));
  } else {
    query.bind(QStringLiteral(":cp"), QVariant());
  }
  query.bind(QStringLiteral(":comment"), document.comment);
  query.bind(QStringLiteral(":recv"), document.receivedBy.trimmed());
  query.bind(QStringLiteral(":ident"), document.identityDoc.trimmed());
  if (document.basedOnId) {
    query.bind(QStringLiteral(":based"), static_cast<qlonglong>(document.basedOnId.value));
  } else {
    query.bind(QStringLiteral(":based"), QVariant());
  }
  if (document.vatRegistrationDate.isValid()) {
    query.bind(QStringLiteral(":vatreg"), iso(document.vatRegistrationDate));
  } else {
    query.bind(QStringLiteral(":vatreg"), QVariant());
  }
  query.bind(QStringLiteral(":vatoff"), static_cast<qlonglong>(document.vatOffsetCents));
  query.bind(QStringLiteral(":edoxml"), document.edoXml);
  query.bind(QStringLiteral(":edosha"), document.edoSha256);
  query.bind(QStringLiteral(":edoimp"), document.edoImported ? 1 : 0);
  if (document.warehouseId) {
    query.bind(QStringLiteral(":wh"), static_cast<qlonglong>(document.warehouseId.value));
  } else {
    query.bind(QStringLiteral(":wh"), QVariant());
  }
  if (document.targetWarehouseId) {
    query.bind(QStringLiteral(":twh"), static_cast<qlonglong>(document.targetWarehouseId.value));
  } else {
    query.bind(QStringLiteral(":twh"), QVariant());
  }
  if (document.contractId) {
    query.bind(QStringLiteral(":ct"), static_cast<qlonglong>(document.contractId.value));
  } else {
    query.bind(QStringLiteral(":ct"), QVariant());
  }
  query.bind(QStringLiteral(":ordstat"), document.orderStatus.trimmed());
  if (!document.currencyCode.trimmed().isEmpty()) {
    query.bind(QStringLiteral(":currcode"), document.currencyCode.trimmed().toUpper());
    query.bind(QStringLiteral(":currrate"), static_cast<qlonglong>(document.currencyRateMicros));
    query.bind(QStringLiteral(":currnom"),
               document.currencyNominal > 0 ? document.currencyNominal : 1);
  } else {
    query.bind(QStringLiteral(":currcode"), QVariant());
    query.bind(QStringLiteral(":currrate"), QVariant());
    query.bind(QStringLiteral(":currnom"), 1);
  }
  if (document.bomId) {
    query.bind(QStringLiteral(":bom"), static_cast<qlonglong>(document.bomId.value));
  } else {
    query.bind(QStringLiteral(":bom"), QVariant());
  }
  if (document.employeeId) {
    query.bind(QStringLiteral(":emp"), static_cast<qlonglong>(document.employeeId.value));
  } else {
    query.bind(QStringLiteral(":emp"), QVariant());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(document.id.value));
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Status::fail(Error::conflict(QStringLiteral("документ з таким номером уже існує")));
    }
    return status;
  }
  status = replaceLines(document.id, document.lines);
  if (!status) {
    return status;
  }
  status = appendAudit(db_, QStringLiteral("update"), QStringLiteral("document"), document.id.value,
                       documentTypeLabelUk(document.type) + QLatin1Char(' ') +
                           document.number.trimmed());
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status DocumentRepository::remove(DocumentId id)
{
  Result<Document> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  Status access = Access::requireDocumentWrite(db_, existing.value().type);
  if (!access) {
    return access;
  }
  if (existing.value().posted) {
    return Status::fail(
        Error::alreadyPosted(QStringLiteral("спочатку зніміть документ з проведення")));
  }
  Status writable = PeriodRepository(db_).ensureDateWritable(existing.value().date);
  if (!writable) {
    return writable;
  }
  sql::Query clear(db_);
  Status status = clear.prepare(
      QStringLiteral("UPDATE documents SET based_on_id = NULL WHERE based_on_id = :id"));
  if (!status) {
    return status;
  }
  clear.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = clear.exec();
  if (!status) {
    return status;
  }
  sql::Query query(db_);
  status = query.prepare(QStringLiteral("DELETE FROM documents WHERE id = :id AND posted = 0"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return status;
  }
  return appendAudit(db_, QStringLiteral("delete"), QStringLiteral("document"), id.value,
                     documentTypeLabelUk(existing.value().type) + QLatin1Char(' ') +
                         existing.value().number);
}

Status DocumentRepository::setPosted(DocumentId id, bool posted, bool expectedPosted)
{
  const Status access = requireWriteAccess(id);
  if (!access) {
    return access;
  }

  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("UPDATE documents SET posted = :posted, posted_at = :at "
                                   "WHERE id = :id AND posted = :expected"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":posted"), posted ? 1 : 0);
  query.bind(QStringLiteral(":at"),
             posted ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : QVariant());
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  query.bind(QStringLiteral(":expected"), expectedPosted ? 1 : 0);
  status = query.exec();
  if (!status) {
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(
        Error::conflict(QStringLiteral("стан проведення документа змінився, повторіть дію")));
  }
  return Status::ok();
}

Status DocumentRepository::setVatRegistration(DocumentId id, QDate date)
{
  const Status access = requireWriteAccess(id);
  if (!access) {
    return access;
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE documents SET vat_registration_date = :d WHERE id = :id"));
  if (!status) {
    return status;
  }
  if (date.isValid()) {
    query.bind(QStringLiteral(":d"), iso(date));
  } else {
    query.bind(QStringLiteral(":d"), QVariant());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status DocumentRepository::setEdoPayload(DocumentId id, const QString & xml, const QString & sha256,
                                         bool imported)
{
  const Status access = requireWriteAccess(id);
  if (!access) {
    return access;
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("UPDATE documents SET edo_xml = :xml, edo_sha256 = :sha, "
                                   "edo_imported = :imp WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":xml"), xml);
  query.bind(QStringLiteral(":sha"), sha256);
  query.bind(QStringLiteral(":imp"), imported ? 1 : 0);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("документ не знайдено")));
  }
  return Status::ok();
}

Status DocumentRepository::setFiscalPayload(DocumentId id, const QString & receiptId,
                                            const QString & fiscalCode, const QString & status)
{
  const Status access = requireWriteAccess(id);
  if (!access) {
    return access;
  }
  sql::Query query(db_);
  Status st = query.prepare(
      QStringLiteral("UPDATE documents SET fiscal_receipt_id = :rid, fiscal_code = :code, "
                     "fiscal_status = :st WHERE id = :id"));
  if (!st) {
    return st;
  }
  query.bind(QStringLiteral(":rid"), receiptId);
  query.bind(QStringLiteral(":code"), fiscalCode);
  query.bind(QStringLiteral(":st"), status);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  st = query.exec();
  if (!st) {
    return st;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("документ не знайдено")));
  }
  return Status::ok();
}

Result<Document> DocumentRepository::findByTypeAndNumber(DocumentType type,
                                                         const QString & number) const
{
  const QString key = number.trimmed();
  if (key.isEmpty()) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("номер документа обов'язковий")));
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT id FROM documents WHERE type = :type AND number = :n"));
  if (!status) {
    return Result<Document>::fail(status.error());
  }
  query.bind(QStringLiteral(":type"), documentTypeToString(type));
  query.bind(QStringLiteral(":n"), key);
  status = query.exec();
  if (!status) {
    return Result<Document>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Document>::fail(
        Error::notFound(QStringLiteral("документ %1 не знайдено").arg(key)));
  }
  return findById(DocumentId{query.value(0).toLongLong()});
}

Status DocumentRepository::updateLineAmounts(DocumentId id, const std::vector<DocumentLine> & lines)
{
  const Status access = requireWritableDraft(id);
  if (!access) {
    return access;
  }

  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral("UPDATE document_lines SET amount_cents = :amt "
                                               "WHERE document_id = :id AND line_no = :no"));
  if (!status) {
    return status;
  }
  for (const DocumentLine & line : lines) {
    query.bind(QStringLiteral(":amt"), static_cast<qlonglong>(line.amount.cents()));
    query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
    query.bind(QStringLiteral(":no"), line.lineNo);
    status = query.exec();
    if (!status) {
      return status;
    }
    if (query.raw().numRowsAffected() != 1) {
      return Status::fail(
          Error::conflict(QStringLiteral("не вдалося оновити суму рядка %1").arg(line.lineNo)));
    }
  }
  return Status::ok();
}

} // namespace fin
