#include "support/Session.h"

using namespace fin;
using namespace fin::test;

TEST_CASE("counterparty validation")
{
  LedgerSession session = openSession();

  Counterparty empty;
  REQUIRE_FALSE(session.counterparties().insert(empty));

  Counterparty badCode;
  badCode.name = QStringLiteral("ТОВ Тест");
  badCode.edrpou = QStringLiteral("12AB");
  REQUIRE_FALSE(session.counterparties().insert(badCode));

  Counterparty ok;
  ok.name = QStringLiteral("  ТОВ Тест  ");
  ok.edrpou = QStringLiteral("12345678");
  const Result<CounterpartyId> id = session.counterparties().insert(ok);
  REQUIRE(id);

  const Result<Counterparty> loaded = session.counterparties().findById(id.value());
  REQUIRE(loaded);
  REQUIRE(loaded.value().name == QStringLiteral("ТОВ Тест"));

  REQUIRE(session.counterparties().remove(id.value()));
  REQUIRE_FALSE(session.counterparties().findById(id.value()));
}

TEST_CASE("cannot delete nomenclature used in posted document")
{
  LedgerSession session = openSession();
  Nomenclature empty;
  REQUIRE_FALSE(session.nomenclature().insert(empty));

  const NomenclatureId id = item(session, QStringLiteral("Цукор"));
  const Result<Nomenclature> loaded = session.nomenclature().findById(id);
  REQUIRE(loaded);
  REQUIRE(loaded.value().unit == QStringLiteral("шт"));

  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 4, 2), QStringLiteral("НП-nom"));
  purchase.counterpartyId = customer(session, QStringLiteral("ТОВ Склад"));
  purchase.lines.push_back(
      stockLine(account(session, "281"), account(session, "631"), id, 1000, 1000));
  saveAndPost(session, purchase);
  REQUIRE_FALSE(session.nomenclature().remove(id));
}

TEST_CASE("cannot delete counterparty used in posted document")
{
  LedgerSession session = openSession();
  const CounterpartyId id = customer(session, QStringLiteral("ТОВ Блок"));
  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 4, 1), QStringLiteral("РТ-cp"));
  sale.counterpartyId = id;
  sale.lines.push_back(line(account(session, "361"), account(session, "701"), 100));
  saveAndPost(session, sale);
  REQUIRE_FALSE(session.counterparties().remove(id));
}
