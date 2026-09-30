#include "fin/integrity/IntegrityChecker.h"

#include "fin/core/Sql.h"
#include "fin/db/CompanyRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"

namespace fin {

IntegrityChecker::IntegrityChecker(QSqlDatabase db) : db_(std::move(db)) {}

Status IntegrityChecker::queryHasNoRows(const QString & sql, const QString & message) const
{
  sql::Query query(db_);
  Status status = query.exec(sql);
  if (!status) {
    return status;
  }
  if (query.next()) {
    return Status::fail(Error::conflict(message));
  }
  return Status::ok();
}

Status IntegrityChecker::verifyEngine() const
{
  sql::Query fk(db_);
  Status status = fk.exec(QStringLiteral("PRAGMA foreign_keys"));
  if (!status) {
    return status;
  }
  if (!fk.next() || fk.value(0).toInt() != 1) {
    return Status::fail(Error::database(QStringLiteral("PRAGMA foreign_keys вимкнено")));
  }

  sql::Query check(db_);
  status = check.exec(QStringLiteral("PRAGMA quick_check"));
  if (!status) {
    return status;
  }
  if (!check.next() ||
      check.value(0).toString().compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
    return Status::fail(Error::database(QStringLiteral("SQLite quick_check не пройшов")));
  }

  sql::Query fkCheck(db_);
  status = fkCheck.exec(QStringLiteral("PRAGMA foreign_key_check"));
  if (!status) {
    return status;
  }
  if (fkCheck.next()) {
    return Status::fail(Error::database(QStringLiteral("порушення зовнішніх ключів")));
  }

  status = queryHasNoRows(
      QStringLiteral("SELECT d.id FROM documents d "
                     "LEFT JOIN (SELECT document_id, COUNT(*) AS n FROM ledger_entries "
                     "GROUP BY document_id) e ON e.document_id = d.id "
                     "WHERE (d.posted = 1 AND COALESCE(e.n, 0) = 0) "
                     "OR (d.posted = 0 AND COALESCE(e.n, 0) > 0) "
                     "LIMIT 1"),
      QStringLiteral("ознака проведення не збігається з журналом проводок"));
  if (!status) {
    return status;
  }

  status =
      queryHasNoRows(QStringLiteral("SELECT d.id FROM documents d "
                                    "JOIN (SELECT document_id, COUNT(*) AS n FROM document_lines "
                                    "GROUP BY document_id) l ON l.document_id = d.id "
                                    "JOIN (SELECT document_id, COUNT(*) AS n FROM ledger_entries "
                                    "GROUP BY document_id) e ON e.document_id = d.id "
                                    "WHERE d.posted = 1 AND l.n != e.n LIMIT 1"),
                     QStringLiteral("кількість проводок не збігається з рядками документа"));
  if (!status) {
    return status;
  }

  status = queryHasNoRows(QStringLiteral("SELECT 1 FROM ledger_entries e "
                                         "JOIN documents d ON d.id = e.document_id "
                                         "WHERE e.debit_account_id = e.credit_account_id "
                                         "AND d.type NOT IN ('stock_transfer','kit_assembly',"
                                         "'kit_disassembly') LIMIT 1"),
                          QStringLiteral("у журналі є проводка з однаковим дебетом і кредитом"));
  if (!status) {
    return status;
  }

  {
    sql::Query stockNet(db_);
    status = stockNet.prepare(
        QStringLiteral("SELECT "
                       "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                       " WHERE debit_account_id IN (SELECT id FROM accounts WHERE code IN ('281', "
                       "'201', '202', '203', '26') AND is_folder = 0)), 0) - "
                       "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                       " WHERE credit_account_id IN (SELECT id FROM accounts WHERE code IN ('281', "
                       "'201', '202', '203', '26') AND is_folder = 0)), 0) - "
                       "COALESCE((SELECT SUM(amount_cents) FROM inventory_movements), 0)"));
    if (!status) {
      return status;
    }
    status = stockNet.exec();
    if (!status) {
      return status;
    }
    if (stockNet.next() && stockNet.value(0).toLongLong() != 0) {
      return Status::fail(Error::conflict(QStringLiteral(
          "сума залишків на складі не збігається з рахунками запасів (281, 201, 202, 203, 26)")));
    }
  }

  status = queryHasNoRows(
      QStringLiteral("SELECT nomenclature_id FROM inventory_movements "
                     "GROUP BY nomenclature_id, warehouse_id "
                     "HAVING SUM(quantity_milli) = 0 AND SUM(amount_cents) != 0 LIMIT 1"),
      QStringLiteral("на складі лишилась сума без кількості"));
  if (!status) {
    return status;
  }

  status = queryHasNoRows(
      QStringLiteral("SELECT d.id FROM documents d "
                     "JOIN (SELECT document_id, SUM(amount_cents) AS s FROM document_lines "
                     "GROUP BY document_id) l ON l.document_id = d.id "
                     "JOIN (SELECT document_id, SUM(amount_cents) AS s, SUM(currency_amount_cents) "
                     "AS cs FROM ledger_entries "
                     "GROUP BY document_id) e ON e.document_id = d.id "
                     "WHERE d.posted = 1 AND ("
                     "  ((d.currency_code IS NULL OR d.currency_code = '' OR d.currency_code = "
                     "'UAH') AND l.s != e.s) OR "
                     "  (d.currency_code IS NOT NULL AND d.currency_code != '' AND d.currency_code "
                     "!= 'UAH' AND l.s != e.cs)"
                     ") LIMIT 1"),
      QStringLiteral("суми рядків документа не збігаються з журналом"));
  if (!status) {
    return status;
  }

  status =
      queryHasNoRows(QStringLiteral("SELECT d.id FROM documents d "
                                    "JOIN accounting_period p ON p.id = 1 "
                                    "WHERE p.closed_through IS NOT NULL AND p.closed_through != '' "
                                    "AND d.date <= p.closed_through AND d.posted = 0 "
                                    "AND d.type NOT IN (%1) LIMIT 1")
                         .arg(sqlNonLedgerDocumentTypes()),
                     QStringLiteral("у закритому періоді є непроведений документ"));
  if (!status) {
    return status;
  }

  status = queryHasNoRows(
      QStringLiteral("SELECT 1 FROM fixed_assets "
                     "WHERE status = 'in_use' "
                     "AND accumulated_cents > original_cents - salvage_cents LIMIT 1"),
      QStringLiteral("знос ОЗ перевищує вартість, що амортизується"));
  if (!status) {
    return status;
  }

  sql::Query dep(db_);
  status =
      dep.prepare(QStringLiteral("SELECT id FROM accounts WHERE code = :code AND is_folder = 0"));
  if (!status) {
    return status;
  }
  dep.bind(QStringLiteral(":code"), QString::fromLatin1(chart::kFaDepreciation));
  status = dep.exec();
  if (!status) {
    return status;
  }
  if (dep.next()) {
    const qlonglong depId = dep.value(0).toLongLong();
    sql::Query net(db_);
    status = net.prepare(QStringLiteral("SELECT "
                                        "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                                        " WHERE credit_account_id = :c), 0) - "
                                        "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                                        " WHERE debit_account_id = :d), 0) - "
                                        "COALESCE((SELECT SUM(accumulated_cents) FROM fixed_assets "
                                        " WHERE status = 'in_use'), 0)"));
    if (!status) {
      return status;
    }
    net.bind(QStringLiteral(":c"), depId);
    net.bind(QStringLiteral(":d"), depId);
    status = net.exec();
    if (!status) {
      return status;
    }
    if (net.next() && net.value(0).toLongLong() != 0) {
      return Status::fail(
          Error::conflict(QStringLiteral("знос на картках ОЗ не збігається з рахунком 131")));
    }
  }

  const char * faCodes[] = {chart::kMachinery, chart::kVehicles, chart::kOtherFixedAssets};
  for (const char * code : faCodes) {
    sql::Query acc(db_);
    status =
        acc.prepare(QStringLiteral("SELECT id FROM accounts WHERE code = :code AND is_folder = 0"));
    if (!status) {
      return status;
    }
    acc.bind(QStringLiteral(":code"), QString::fromLatin1(code));
    status = acc.exec();
    if (!status) {
      return status;
    }
    if (!acc.next()) {
      continue;
    }
    const qlonglong accountId = acc.value(0).toLongLong();
    sql::Query net(db_);
    status = net.prepare(QStringLiteral("SELECT "
                                        "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                                        " WHERE debit_account_id = :d), 0) - "
                                        "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                                        " WHERE credit_account_id = :c), 0) - "
                                        "COALESCE((SELECT SUM(original_cents) FROM fixed_assets "
                                        " WHERE status = 'in_use' AND asset_account_id = :a), 0)"));
    if (!status) {
      return status;
    }
    net.bind(QStringLiteral(":d"), accountId);
    net.bind(QStringLiteral(":c"), accountId);
    net.bind(QStringLiteral(":a"), accountId);
    status = net.exec();
    if (!status) {
      return status;
    }
    if (net.next() && net.value(0).toLongLong() != 0) {
      return Status::fail(
          Error::conflict(QStringLiteral("первісна вартість карток ОЗ не збігається з рахунком %1")
                              .arg(QLatin1String(code))));
    }
  }

  status = queryHasNoRows(
      QStringLiteral("SELECT 1 FROM intangible_assets "
                     "WHERE status = 'in_use' "
                     "AND accumulated_cents > original_cents - salvage_cents LIMIT 1"),
      QStringLiteral("амортизація НМА перевищує вартість, що амортизується"));
  if (!status) {
    return status;
  }

  sql::Query iaDep(db_);
  status =
      iaDep.prepare(QStringLiteral("SELECT id FROM accounts WHERE code = :code AND is_folder = 0"));
  if (!status) {
    return status;
  }
  iaDep.bind(QStringLiteral(":code"), QString::fromLatin1(chart::kIaAmortization));
  status = iaDep.exec();
  if (!status) {
    return status;
  }
  if (iaDep.next()) {
    const qlonglong depId = iaDep.value(0).toLongLong();
    sql::Query net(db_);
    status =
        net.prepare(QStringLiteral("SELECT "
                                   "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                                   " WHERE credit_account_id = :c), 0) - "
                                   "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                                   " WHERE debit_account_id = :d), 0) - "
                                   "COALESCE((SELECT SUM(accumulated_cents) FROM intangible_assets "
                                   " WHERE status = 'in_use'), 0)"));
    if (!status) {
      return status;
    }
    net.bind(QStringLiteral(":c"), depId);
    net.bind(QStringLiteral(":d"), depId);
    status = net.exec();
    if (!status) {
      return status;
    }
    if (net.next() && net.value(0).toLongLong() != 0) {
      return Status::fail(Error::conflict(
          QStringLiteral("амортизація на картках НМА не збігається з рахунком 133")));
    }
  }

  const char * iaCodes[] = {chart::kSoftware, chart::kOtherIntangibles};
  for (const char * code : iaCodes) {
    sql::Query acc(db_);
    status =
        acc.prepare(QStringLiteral("SELECT id FROM accounts WHERE code = :code AND is_folder = 0"));
    if (!status) {
      return status;
    }
    acc.bind(QStringLiteral(":code"), QString::fromLatin1(code));
    status = acc.exec();
    if (!status) {
      return status;
    }
    if (!acc.next()) {
      continue;
    }
    const qlonglong accountId = acc.value(0).toLongLong();
    sql::Query net(db_);
    status =
        net.prepare(QStringLiteral("SELECT "
                                   "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                                   " WHERE debit_account_id = :d), 0) - "
                                   "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                                   " WHERE credit_account_id = :c), 0) - "
                                   "COALESCE((SELECT SUM(original_cents) FROM intangible_assets "
                                   " WHERE status = 'in_use' AND asset_account_id = :a), 0)"));
    if (!status) {
      return status;
    }
    net.bind(QStringLiteral(":d"), accountId);
    net.bind(QStringLiteral(":c"), accountId);
    net.bind(QStringLiteral(":a"), accountId);
    status = net.exec();
    if (!status) {
      return status;
    }
    if (net.next() && net.value(0).toLongLong() != 0) {
      return Status::fail(
          Error::conflict(QStringLiteral("первісна вартість карток НМА не збігається з рахунком %1")
                              .arg(QLatin1String(code))));
    }
  }

  sql::Query lv(db_);
  status =
      lv.prepare(QStringLiteral("SELECT id FROM accounts WHERE code = :code AND is_folder = 0"));
  if (!status) {
    return status;
  }
  lv.bind(QStringLiteral(":code"), QString::fromLatin1(chart::kLowValue));
  status = lv.exec();
  if (!status) {
    return status;
  }
  if (lv.next()) {
    const qlonglong stockId = lv.value(0).toLongLong();
    sql::Query net(db_);
    status =
        net.prepare(QStringLiteral("SELECT "
                                   "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                                   " WHERE debit_account_id = :d), 0) - "
                                   "COALESCE((SELECT SUM(amount_cents) FROM ledger_entries "
                                   " WHERE credit_account_id = :c), 0) - "
                                   "COALESCE((SELECT SUM(amount_cents) FROM low_value_items), 0)"));
    if (!status) {
      return status;
    }
    net.bind(QStringLiteral(":d"), stockId);
    net.bind(QStringLiteral(":c"), stockId);
    status = net.exec();
    if (!status) {
      return status;
    }
    if (net.next() && net.value(0).toLongLong() != 0) {
      return Status::fail(
          Error::conflict(QStringLiteral("залишок карток МШП не збігається з рахунком 22")));
    }
  }

  status = queryHasNoRows(
      QStringLiteral("SELECT 1 FROM ledger_entries e "
                     "JOIN documents d ON d.id = e.document_id "
                     "JOIN accounts da ON da.id = e.debit_account_id "
                     "JOIN accounts ca ON ca.id = e.credit_account_id "
                     "WHERE d.type != 'tax_invoice' "
                     "AND (LOWER(da.code) IN ('641','643','644') "
                     "  OR LOWER(ca.code) IN ('641','643','644')) LIMIT 1"),
      QStringLiteral("рахунки 641/643/644 мають рухатись лише податковими накладними"));
  if (!status) {
    return status;
  }

  status = queryHasNoRows(
      QStringLiteral(
          "SELECT d.id FROM documents d "
          "WHERE d.posted = 1 AND d.type = 'tax_invoice' "
          "AND d.vat_registration_date IS NOT NULL "
          "AND trim(d.vat_registration_date) != '' "
          "AND ("
          " (SELECT COALESCE(SUM(CASE WHEN LOWER(da.code)='643' THEN e.amount_cents ELSE 0 END),0) "
          "    - COALESCE(SUM(CASE WHEN LOWER(ca.code)='643' THEN e.amount_cents ELSE 0 END),0) "
          "  FROM ledger_entries e "
          "  JOIN accounts da ON da.id = e.debit_account_id "
          "  JOIN accounts ca ON ca.id = e.credit_account_id "
          "  WHERE e.document_id = d.id) != 0 "
          " OR "
          " (SELECT COALESCE(SUM(CASE WHEN LOWER(da.code)='644' THEN e.amount_cents ELSE 0 END),0) "
          "    - COALESCE(SUM(CASE WHEN LOWER(ca.code)='644' THEN e.amount_cents ELSE 0 END),0) "
          "  FROM ledger_entries e "
          "  JOIN accounts da ON da.id = e.debit_account_id "
          "  JOIN accounts ca ON ca.id = e.credit_account_id "
          "  WHERE e.document_id = d.id) != 0 "
          ") LIMIT 1"),
      QStringLiteral("зареєстрована ПН має закривати 643/644 на 641"));
  if (!status) {
    return status;
  }

  return rejectNegativeStock();
}

Status IntegrityChecker::verifyPostedDocument(std::int64_t documentId) const
{
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("SELECT "
                     "(SELECT COUNT(*) FROM document_lines WHERE document_id = :id), "
                     "(SELECT COUNT(*) FROM ledger_entries WHERE document_id = :id2), "
                     "(SELECT posted FROM documents WHERE id = :id3)"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(documentId));
  query.bind(QStringLiteral(":id2"), static_cast<qlonglong>(documentId));
  query.bind(QStringLiteral(":id3"), static_cast<qlonglong>(documentId));
  status = query.exec();
  if (!status) {
    return status;
  }
  if (!query.next()) {
    return Status::fail(Error::notFound(QStringLiteral("документ не знайдено")));
  }
  const int lines = query.value(0).toInt();
  const int ledger = query.value(1).toInt();
  const int posted = query.value(2).toInt();
  if (posted != 1) {
    return Status::fail(Error::conflict(QStringLiteral("документ мав бути позначений проведеним")));
  }
  if (lines == 0 || lines != ledger) {
    return Status::fail(
        Error::conflict(QStringLiteral("журнал проводок не відповідає рядкам документа")));
  }
  return Status::ok();
}

Status IntegrityChecker::rejectNegativeStock() const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (company && company.value().allowNegativeStock) {
    return Status::ok();
  }
  return queryHasNoRows(
      QStringLiteral(
          "SELECT nomenclature_id FROM inventory_movements "
          "GROUP BY nomenclature_id, warehouse_id HAVING SUM(quantity_milli) < 0 LIMIT 1"),
      QStringLiteral("проведення створює від'ємний залишок товару"));
}

} // namespace fin
