#include "fin/db/LedgerRepository.h"

#include "fin/core/Sql.h"

#include <QVariant>

namespace fin {

LedgerRepository::LedgerRepository(QSqlDatabase db) : db_(std::move(db)) {}

Status LedgerRepository::removeForDocument(DocumentId documentId)
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("DELETE FROM ledger_entries WHERE document_id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(documentId.value));
  return query.exec();
}

Status LedgerRepository::replaceForDocument(DocumentId documentId,
                                            const std::vector<LedgerEntry> & entries)
{
  Status status = removeForDocument(documentId);
  if (!status) {
    return status;
  }
  sql::Query insert(db_);
  status = insert.prepare(
      QStringLiteral("INSERT INTO ledger_entries(document_id, date, debit_account_id, "
                     "credit_account_id, amount_cents, counterparty_id, description, "
                     "dt_subconto1_kind, dt_subconto1_id, dt_subconto2_kind, dt_subconto2_id, "
                     "dt_subconto3_kind, dt_subconto3_id, "
                     "ct_subconto1_kind, ct_subconto1_id, ct_subconto2_kind, ct_subconto2_id, "
                     "ct_subconto3_kind, ct_subconto3_id, "
                     "currency_code, currency_amount_cents, currency_rate_micros) "
                     "VALUES(:doc, :date, :dt, :ct, :amt, :cp, :desc, "
                     ":dt_s1_k, :dt_s1_id, :dt_s2_k, :dt_s2_id, :dt_s3_k, :dt_s3_id, "
                     ":ct_s1_k, :ct_s1_id, :ct_s2_k, :ct_s2_id, :ct_s3_k, :ct_s3_id, "
                     ":currcode, :curramt, :currrate)"));
  if (!status) {
    return status;
  }
  for (const LedgerEntry & entry : entries) {
    insert.bind(QStringLiteral(":doc"), static_cast<qlonglong>(documentId.value));
    insert.bind(QStringLiteral(":date"), entry.date.toString(Qt::ISODate));
    insert.bind(QStringLiteral(":dt"), static_cast<qlonglong>(entry.debitAccountId.value));
    insert.bind(QStringLiteral(":ct"), static_cast<qlonglong>(entry.creditAccountId.value));
    insert.bind(QStringLiteral(":amt"), static_cast<qlonglong>(entry.amount.cents()));
    if (entry.counterpartyId) {
      insert.bind(QStringLiteral(":cp"), static_cast<qlonglong>(entry.counterpartyId.value));
    } else {
      insert.bind(QStringLiteral(":cp"), QVariant());
    }
    insert.bind(QStringLiteral(":desc"), entry.description);

    // Дебет субконто
    auto bindSubconto = [&](const QString & kPrefix, const QString & idPrefix,
                            const SubcontoValue & val) {
      if (!val.empty()) {
        insert.bind(kPrefix, val.kindId);
        insert.bind(idPrefix, static_cast<qlonglong>(val.entityId));
      } else {
        insert.bind(kPrefix, QVariant());
        insert.bind(idPrefix, QVariant());
      }
    };

    bindSubconto(QStringLiteral(":dt_s1_k"), QStringLiteral(":dt_s1_id"), entry.debitSubconto.s1);
    bindSubconto(QStringLiteral(":dt_s2_k"), QStringLiteral(":dt_s2_id"), entry.debitSubconto.s2);
    bindSubconto(QStringLiteral(":dt_s3_k"), QStringLiteral(":dt_s3_id"), entry.debitSubconto.s3);

    // Кредит субконто
    bindSubconto(QStringLiteral(":ct_s1_k"), QStringLiteral(":ct_s1_id"), entry.creditSubconto.s1);
    bindSubconto(QStringLiteral(":ct_s2_k"), QStringLiteral(":ct_s2_id"), entry.creditSubconto.s2);
    bindSubconto(QStringLiteral(":ct_s3_k"), QStringLiteral(":ct_s3_id"), entry.creditSubconto.s3);

    // Валюта
    if (!entry.currencyCode.trimmed().isEmpty()) {
      insert.bind(QStringLiteral(":currcode"), entry.currencyCode.trimmed().toUpper());
      insert.bind(QStringLiteral(":curramt"), static_cast<qlonglong>(entry.currencyAmount.cents()));
      insert.bind(QStringLiteral(":currrate"), static_cast<qlonglong>(entry.currencyRateMicros));
    } else {
      insert.bind(QStringLiteral(":currcode"), QVariant());
      insert.bind(QStringLiteral(":curramt"), QVariant());
      insert.bind(QStringLiteral(":currrate"), QVariant());
    }

    status = insert.exec();
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

Result<std::vector<LedgerEntry>> LedgerRepository::listForDocument(DocumentId documentId) const
{
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("SELECT id, document_id, date, debit_account_id, credit_account_id, "
                     "amount_cents, counterparty_id, description, "
                     "dt_subconto1_kind, dt_subconto1_id, dt_subconto2_kind, dt_subconto2_id, "
                     "dt_subconto3_kind, dt_subconto3_id, "
                     "ct_subconto1_kind, ct_subconto1_id, ct_subconto2_kind, ct_subconto2_id, "
                     "ct_subconto3_kind, ct_subconto3_id, "
                     "currency_code, currency_amount_cents, currency_rate_micros "
                     "FROM ledger_entries "
                     "WHERE document_id = :id ORDER BY id"));
  if (!status) {
    return Result<std::vector<LedgerEntry>>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(documentId.value));
  status = query.exec();
  if (!status) {
    return Result<std::vector<LedgerEntry>>::fail(status.error());
  }
  std::vector<LedgerEntry> rows;
  while (query.next()) {
    LedgerEntry entry;
    entry.id = LedgerEntryId{query.value(0).toLongLong()};
    entry.documentId = DocumentId{query.value(1).toLongLong()};
    entry.date = QDate::fromString(query.value(2).toString(), Qt::ISODate);
    entry.debitAccountId = AccountId{query.value(3).toLongLong()};
    entry.creditAccountId = AccountId{query.value(4).toLongLong()};
    entry.amount = Money::fromCents(query.value(5).toLongLong());
    const std::int64_t cp = query.value(6).toLongLong();
    if (cp > 0) {
      entry.counterpartyId = CounterpartyId{cp};
    }
    entry.description = query.value(7).toString();

    // Дебет субконто
    entry.debitSubconto.s1.kindId = query.value(8).toString();
    entry.debitSubconto.s1.entityId = query.value(9).toLongLong();
    entry.debitSubconto.s2.kindId = query.value(10).toString();
    entry.debitSubconto.s2.entityId = query.value(11).toLongLong();
    entry.debitSubconto.s3.kindId = query.value(12).toString();
    entry.debitSubconto.s3.entityId = query.value(13).toLongLong();

    // Кредит субконто
    entry.creditSubconto.s1.kindId = query.value(14).toString();
    entry.creditSubconto.s1.entityId = query.value(15).toLongLong();
    entry.creditSubconto.s2.kindId = query.value(16).toString();
    entry.creditSubconto.s2.entityId = query.value(17).toLongLong();
    entry.creditSubconto.s3.kindId = query.value(18).toString();
    entry.creditSubconto.s3.entityId = query.value(19).toLongLong();

    // Валюта
    entry.currencyCode = query.value(20).toString();
    entry.currencyAmount = Money::fromCents(query.value(21).toLongLong());
    entry.currencyRateMicros = query.value(22).toLongLong();

    rows.push_back(entry);
  }
  return Result<std::vector<LedgerEntry>>::ok(std::move(rows));
}

Result<Money> LedgerRepository::balance(AccountId accountId, const QDate & date) const
{
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral(
      "SELECT "
      "  COALESCE(SUM(CASE WHEN debit_account_id = :acc THEN amount_cents ELSE 0 END), 0) - "
      "  COALESCE(SUM(CASE WHEN credit_account_id = :acc THEN amount_cents ELSE 0 END), 0) "
      "FROM ledger_entries "
      "WHERE date <= :date"));
  if (!status) {
    return Result<Money>::fail(status.error());
  }
  query.bind(QStringLiteral(":acc"), static_cast<qlonglong>(accountId.value));
  query.bind(QStringLiteral(":date"), date.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return Result<Money>::fail(status.error());
  }
  if (query.next()) {
    return Result<Money>::ok(Money::fromCents(query.value(0).toLongLong()));
  }
  return Result<Money>::ok(Money::zero());
}

} // namespace fin
