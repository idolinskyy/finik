#include "support/Session.h"

#include "fin/core/Money.h"
#include "fin/domain/ChartCodes.h"

#include <limits>

using namespace fin;
using namespace fin::test;

TEST_CASE("integrity holds after a posted circuit")
{
  LedgerSession session = openSession();
  REQUIRE(session.verify());

  const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Аудит"));
  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 8, 1), QStringLiteral("РТ-int"));
  sale.counterpartyId = buyer;
  sale.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 50000));
  saveAndPost(session, sale);
  REQUIRE(session.verify());
}

TEST_CASE("sale cannot drive goods quantity negative")
{
  LedgerSession session = openSession();
  const NomenclatureId tea = item(session, QStringLiteral("Чай"));
  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 8, 2), QStringLiteral("РТ-neg"));
  sale.counterpartyId = customer(session, QStringLiteral("ТОВ Мінус"));
  sale.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 1000));
  sale.lines.push_back(
      stockLine(account(session, chart::kCogs), account(session, chart::kGoods), tea, 1000, 1000));
  const Result<DocumentId> id = session.documents().insert(sale);
  REQUIRE(id);
  REQUIRE_FALSE(session.posting().post(id.value()));
  REQUIRE(session.verify());
}

TEST_CASE("unpost restores integrity")
{
  LedgerSession session = openSession();
  const NomenclatureId salt = item(session, QStringLiteral("Сіль"));
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 8, 3), QStringLiteral("НП-int"));
  purchase.counterpartyId = customer(session, QStringLiteral("ТОВ Сіль"));
  purchase.lines.push_back(stockLine(account(session, chart::kGoods),
                                     account(session, chart::kSuppliers), salt, 3000, 2000));
  const DocumentId id = saveAndPost(session, purchase);
  REQUIRE(session.posting().unpost(id));
  REQUIRE(session.verify());
}

TEST_CASE("reject impossible document dates")
{
  LedgerSession session = openSession();
  Document document = makeDoc(DocumentType::Manual, QDate(1800, 1, 1), QStringLiteral("БО-date"));
  document.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 100));
  REQUIRE_FALSE(session.documents().insert(document));
}

TEST_CASE("Money negated detects int64 overflow")
{
  const Money min = Money::fromCents(std::numeric_limits<std::int64_t>::min());
  REQUIRE_FALSE(min.negated());
  REQUIRE_FALSE(Money::zero().minus(min));
}
