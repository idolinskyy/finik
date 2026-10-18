#pragma once

#include "fin/domain/ChartCodes.h"
#include "fin/domain/Nomenclature.h"
#include "fin/session/LedgerSession.h"

#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace fin::test {

inline LedgerSession openSession()
{
  Result<LedgerSession> session = LedgerSession::openMemory();
  if (!session) {
    UNSCOPED_INFO(session.error().message.toStdString());
  }
  REQUIRE(session);
  return std::move(session.value());
}

inline AccountId account(LedgerSession & session, const char * code)
{
  Result<Account> found = session.accounts().findByCode(QString::fromLatin1(code));
  REQUIRE(found);
  return found.value().id;
}

inline CounterpartyId customer(LedgerSession & session, const QString & name)
{
  Counterparty row;
  row.name = name;
  row.isCustomer = true;
  row.isSupplier = true;
  Result<CounterpartyId> id = session.counterparties().insert(row);
  REQUIRE(id);
  return id.value();
}

inline Document makeDoc(DocumentType type, QDate date, const QString & number)
{
  Document document;
  document.type = type;
  document.date = date;
  document.number = number;
  return document;
}

inline DocumentLine line(AccountId debit, AccountId credit, std::int64_t cents,
                         const QString & description = QString())
{
  DocumentLine row;
  row.debitAccountId = debit;
  row.creditAccountId = credit;
  row.amount = Money::fromCents(cents);
  row.description = description;
  return row;
}

inline NomenclatureId item(LedgerSession & session, const QString & name)
{
  Nomenclature row;
  row.name = name;
  Result<NomenclatureId> id = session.nomenclature().insert(row);
  REQUIRE(id);
  return id.value();
}

inline DocumentLine stockLine(AccountId debit, AccountId credit, NomenclatureId nomenclature,
                              std::int64_t milli, std::int64_t priceCents)
{
  DocumentLine row;
  row.debitAccountId = debit;
  row.creditAccountId = credit;
  row.nomenclatureId = nomenclature;
  row.quantity = Quantity::fromMilli(milli);
  row.unitPrice = Money::fromCents(priceCents);
  const Result<Money> amount = row.quantity.times(row.unitPrice);
  REQUIRE(amount);
  row.amount = amount.value();
  return row;
}

inline DocumentId saveAndPost(LedgerSession & session, Document document)
{
  Result<DocumentId> id = session.documents().insert(document);
  REQUIRE(id);
  const Status posted = session.posting().post(id.value());
  REQUIRE(posted);
  return id.value();
}

} // namespace fin::test
