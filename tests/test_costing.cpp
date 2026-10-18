#include "support/Session.h"

#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"

using namespace fin;
using namespace fin::test;

TEST_CASE("goods credit uses weighted average cost not the draft amount")
{
  LedgerSession session = openSession();
  const NomenclatureId flour = item(session, QStringLiteral("Борошно"));
  const CounterpartyId mill = customer(session, QStringLiteral("Млин"));
  const CounterpartyId shop = customer(session, QStringLiteral("Крамниця"));

  Document first = makeDoc(DocumentType::Purchase, QDate(2026, 6, 1), QStringLiteral("НП-avg-1"));
  first.counterpartyId = mill;
  first.lines.push_back(stockLine(account(session, chart::kGoods),
                                  account(session, chart::kSuppliers), flour, 2000, 10000));
  saveAndPost(session, first);

  Document second = makeDoc(DocumentType::Purchase, QDate(2026, 6, 2), QStringLiteral("НП-avg-2"));
  second.counterpartyId = mill;
  second.lines.push_back(stockLine(account(session, chart::kGoods),
                                   account(session, chart::kSuppliers), flour, 2000, 30000));
  saveAndPost(session, second);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 6, 3), QStringLiteral("РТ-avg"));
  sale.counterpartyId = shop;
  sale.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 50000));
  DocumentLine cogs = stockLine(account(session, chart::kCogs), account(session, chart::kGoods),
                                flour, 1000, 99999);
  sale.lines.push_back(cogs);
  const DocumentId saleId = saveAndPost(session, sale);

  const Result<Document> posted = session.documents().findById(saleId);
  REQUIRE(posted);
  REQUIRE(posted.value().lines.size() == 2);
  REQUIRE(posted.value().lines[1].amount.cents() == 20000);

  const Result<StockReportData> stock =
      session.stock().build(QDate(2026, 6, 1), QDate(2026, 6, 30));
  REQUIRE(stock);
  REQUIRE(stock.value().rows.front().closingQty.milli() == 3000);
  REQUIRE(stock.value().rows.front().closingAmount.cents() == 60000);
  REQUIRE(session.verify());
}

TEST_CASE("full goods depletion takes remaining amount")
{
  LedgerSession session = openSession();
  const NomenclatureId salt = item(session, QStringLiteral("Сіль-вага"));
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 6, 4), QStringLiteral("НП-full"));
  purchase.counterpartyId = customer(session, QStringLiteral("Постачальник"));
  purchase.lines.push_back(stockLine(account(session, chart::kGoods),
                                     account(session, chart::kSuppliers), salt, 3000, 10000));
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 6, 5), QStringLiteral("РТ-full"));
  sale.counterpartyId = customer(session, QStringLiteral("Покупець"));
  sale.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 40000));
  sale.lines.push_back(
      stockLine(account(session, chart::kCogs), account(session, chart::kGoods), salt, 3000, 1));
  saveAndPost(session, sale);

  const Result<StockReportData> stock =
      session.stock().build(QDate(2026, 6, 1), QDate(2026, 6, 30));
  REQUIRE(stock);
  REQUIRE(stock.value().rows.front().closingQty.milli() == 0);
  REQUIRE(stock.value().rows.front().closingAmount.cents() == 0);
  REQUIRE(session.verify());
}

TEST_CASE("sale default lines are 361/701 and 902/281")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const std::vector<DocumentLine> lines = defaultLinesForType(DocumentType::Sale, accounts.value());
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0].debitAccountId == account(session, chart::kCustomers));
  REQUIRE(lines[0].creditAccountId == account(session, chart::kRevenue));
  REQUIRE(lines[1].debitAccountId == account(session, chart::kCogs));
  REQUIRE(lines[1].creditAccountId == account(session, chart::kGoods));

  bool lockDebit = false;
  bool lockCredit = false;
  accountLocksForType(DocumentType::CashIn, lockDebit, lockCredit);
  REQUIRE(lockDebit);
  REQUIRE_FALSE(lockCredit);
  accountLocksForType(DocumentType::Purchase, lockDebit, lockCredit);
  REQUIRE_FALSE(lockDebit);
  REQUIRE_FALSE(lockCredit);
  accountLocksForType(DocumentType::Manual, lockDebit, lockCredit);
  REQUIRE_FALSE(lockDebit);
  REQUIRE_FALSE(lockCredit);
}
