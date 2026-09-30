#include "support/Session.h"

#include "fin/demo/DemoSeed.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/search/GlobalSearch.h"

using namespace fin;
using namespace fin::test;

TEST_CASE("global search finds documents, directories and accounts")
{
  LedgerSession session = openSession();
  REQUIRE(seedDemoData(session, 42));
  GlobalSearch search(session.handle());

  const Result<std::vector<SearchHit>> empty = search.find(QStringLiteral("а"));
  REQUIRE(empty);
  REQUIRE(empty.value().empty());

  const Result<std::vector<SearchHit>> mill = search.find(QStringLiteral("постачзерно"));
  REQUIRE(mill);
  bool sawParty = false;
  bool sawDoc = false;
  for (const SearchHit & hit : mill.value()) {
    if (hit.kind == SearchKind::Counterparty && hit.title.contains(QStringLiteral("ПостачЗерно"))) {
      sawParty = true;
    }
    if (hit.kind == SearchKind::Document) {
      sawDoc = true;
    }
  }
  REQUIRE(sawParty);
  REQUIRE(sawDoc);

  const Result<std::vector<SearchHit>> sku = search.find(QStringLiteral("FL-01"));
  REQUIRE(sku);
  bool sawFlour = false;
  for (const SearchHit & hit : sku.value()) {
    if (hit.kind == SearchKind::Nomenclature) {
      sawFlour = true;
    }
  }
  REQUIRE(sawFlour);

  const Result<std::vector<SearchHit>> cash = search.find(QStringLiteral("301"));
  REQUIRE(cash);
  bool sawCash = false;
  for (const SearchHit & hit : cash.value()) {
    if (hit.kind == SearchKind::Account && hit.title.startsWith(QStringLiteral("301"))) {
      sawCash = true;
      REQUIRE_FALSE(hit.accountIsFolder);
    }
  }
  REQUIRE(sawCash);

  const Result<std::vector<SearchHit>> rent = search.find(QStringLiteral("оренда"));
  REQUIRE(rent);
  bool sawServicePurchase = false;
  for (const SearchHit & hit : rent.value()) {
    if (hit.kind == SearchKind::Document &&
        hit.subtitle.contains(QStringLiteral("Надходження послуг"))) {
      sawServicePurchase = true;
    }
  }
  REQUIRE(sawServicePurchase);

  const Result<std::vector<SearchHit>> fa = search.find(QStringLiteral("ОЗ-0001"));
  REQUIRE(fa);
  bool sawAsset = false;
  for (const SearchHit & hit : fa.value()) {
    if (hit.kind == SearchKind::FixedAsset) {
      sawAsset = true;
    }
  }
  REQUIRE(sawAsset);

  const Result<std::vector<SearchHit>> ia = search.find(QStringLiteral("НМА-0001"));
  REQUIRE(ia);
  bool sawIa = false;
  for (const SearchHit & hit : ia.value()) {
    if (hit.kind == SearchKind::Intangible) {
      sawIa = true;
    }
  }
  REQUIRE(sawIa);

  REQUIRE(searchKindLabelUk(SearchKind::Document) == QStringLiteral("Документ"));
}

TEST_CASE("global search matches a document line description")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const CounterpartyId mill = customer(session, QStringLiteral("Орендодавець"));
  CashLine rent;
  rent.correspondenceId = account(session, chart::kAdminExpense);
  rent.amount = Money::fromCents(800000);
  rent.description = QStringLiteral("оренда офісу");
  Document document =
      makeDoc(DocumentType::ServicePurchase, QDate(2026, 5, 15), QStringLiteral("ПУ-search"));
  document.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> lines = composeServicePurchase(accounts.value(), {rent});
  REQUIRE(lines);
  document.lines = lines.value();
  const DocumentId id = saveAndPost(session, document);

  GlobalSearch search(session.handle());
  const Result<std::vector<SearchHit>> hits = search.find(QStringLiteral("ОФІСУ"));
  REQUIRE(hits);
  bool found = false;
  for (const SearchHit & hit : hits.value()) {
    if (hit.kind == SearchKind::Document && hit.objectId == id.value) {
      found = true;
    }
  }
  REQUIRE(found);
}
