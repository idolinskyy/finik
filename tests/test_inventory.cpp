#include "support/Session.h"

#include "fin/domain/ChartCodes.h"

using namespace fin;
using namespace fin::test;

TEST_CASE("purchase without nomenclature is rejected")
{
  LedgerSession session = openSession();
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 6, 1), QStringLiteral("НП-bad"));
  purchase.counterpartyId = customer(session, QStringLiteral("ТОВ Без номенклатури"));
  purchase.lines.push_back(
      line(account(session, chart::kGoods), account(session, chart::kSuppliers), 1000));
  const Result<DocumentId> id = session.documents().insert(purchase);
  REQUIRE(id);
  REQUIRE_FALSE(session.posting().post(id.value()));
}

TEST_CASE("issue without stock is rejected or accepted based on settings")
{
  LedgerSession session = openSession();
  const NomenclatureId sugar = item(session, QStringLiteral("Цукор-пісок"));
  const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Магазин"));
  const CounterpartyId supplier = customer(session, QStringLiteral("ТОВ Сировина-NEG"));

  // Частковий залишок: 1 од., продаж 8 од. — мінус лише з оцінкою з наявної суми.
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 6, 19), QStringLiteral("НП-neg"));
  purchase.counterpartyId = supplier;
  purchase.lines.push_back(stockLine(account(session, chart::kGoods),
                                     account(session, chart::kSuppliers), sugar, 1000, 20000));
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 6, 20), QStringLiteral("РТ-stock"));
  sale.counterpartyId = buyer;
  sale.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 400000));
  sale.lines.push_back(stockLine(account(session, chart::kCogs), account(session, chart::kGoods),
                                 sugar, 8000, 20000));
  const Result<DocumentId> saleId = session.documents().insert(sale);
  REQUIRE(saleId);

  CompanyProfile profile = session.company().load().value();
  profile.allowNegativeStock = false;
  REQUIRE(session.company().save(profile));
  REQUIRE_FALSE(session.posting().post(saleId.value()));

  profile.allowNegativeStock = true;
  REQUIRE(session.company().save(profile));
  REQUIRE(session.posting().post(saleId.value()));
}

TEST_CASE("stock movements match account 281 and unpost restores qty")
{
  LedgerSession session = openSession();
  const NomenclatureId sugar = item(session, QStringLiteral("Цукор-пісок"));
  const CounterpartyId supplier = customer(session, QStringLiteral("ТОВ Сировина"));
  const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Магазин"));

  Document opening =
      makeDoc(DocumentType::OpeningBalance, QDate(2026, 5, 31), QStringLiteral("ЗС-stock"));
  opening.lines.push_back(stockLine(account(session, chart::kGoods),
                                    account(session, chart::kAuxiliary), sugar, 5000, 20000));
  saveAndPost(session, opening);

  Document purchase =
      makeDoc(DocumentType::Purchase, QDate(2026, 6, 10), QStringLiteral("НП-stock"));
  purchase.counterpartyId = supplier;
  purchase.lines.push_back(stockLine(account(session, chart::kGoods),
                                     account(session, chart::kSuppliers), sugar, 10000, 20000));
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 6, 20), QStringLiteral("РТ-stock"));
  sale.counterpartyId = buyer;
  sale.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 400000));
  sale.lines.push_back(stockLine(account(session, chart::kCogs), account(session, chart::kGoods),
                                 sugar, 8000, 20000));
  const DocumentId saleId = saveAndPost(session, sale);

  const QDate from(2026, 6, 1);
  const QDate to(2026, 6, 30);
  const Result<StockReportData> stock = session.stock().build(from, to);
  REQUIRE(stock);
  REQUIRE(stock.value().rows.size() == 1);
  REQUIRE(stock.value().rows.front().openingQty.milli() == 5000);
  REQUIRE(stock.value().rows.front().inQty.milli() == 10000);
  REQUIRE(stock.value().rows.front().outQty.milli() == 8000);
  REQUIRE(stock.value().rows.front().closingQty.milli() == 7000);
  REQUIRE(stock.value().rows.front().closingAmount.cents() == 140000);

  const Result<TrialBalance> osv = session.trialBalance().build(from, to);
  REQUIRE(osv);
  std::int64_t goodsNet = 0;
  for (const TrialBalanceRow & row : osv.value().rows) {
    if (row.code == QLatin1String(chart::kGoods)) {
      goodsNet = row.closingDebit.cents() - row.closingCredit.cents();
    }
  }
  REQUIRE(goodsNet == stock.value().totals.closingAmount.cents());

  REQUIRE(session.posting().unpost(saleId));
  const Result<StockReportData> restored = session.stock().build(from, to);
  REQUIRE(restored);
  REQUIRE(restored.value().rows.front().closingQty.milli() == 15000);
}

TEST_CASE("counterparty turnover and account card filter agree")
{
  LedgerSession session = openSession();
  const CounterpartyId first = customer(session, QStringLiteral("Перший"));
  const CounterpartyId second = customer(session, QStringLiteral("Другий"));
  const AccountId customers = account(session, chart::kCustomers);
  const AccountId revenue = account(session, chart::kRevenue);

  Document sale1 = makeDoc(DocumentType::Sale, QDate(2026, 7, 1), QStringLiteral("РТ-a"));
  sale1.counterpartyId = first;
  sale1.lines.push_back(line(customers, revenue, 100000));
  saveAndPost(session, sale1);

  Document sale2 = makeDoc(DocumentType::Sale, QDate(2026, 7, 2), QStringLiteral("РТ-b"));
  sale2.counterpartyId = second;
  sale2.lines.push_back(line(customers, revenue, 250000));
  saveAndPost(session, sale2);

  const QDate from(2026, 7, 1);
  const QDate to(2026, 7, 31);
  const Result<CounterpartyTurnover> turnover =
      session.counterpartyTurnover().build(customers, from, to);
  REQUIRE(turnover);
  REQUIRE(turnover.value().rows.size() == 2);
  REQUIRE(turnover.value().totals.turnoverDebit.cents() == 350000);
  REQUIRE(turnover.value().totals.closingDebit.cents() == 350000);

  const Result<AccountCard> firstCard = session.accountCard().build(customers, from, to, first);
  REQUIRE(firstCard);
  REQUIRE(firstCard.value().turnoverDebit.cents() == 100000);
  REQUIRE(firstCard.value().closingDebit.cents() == 100000);
  REQUIRE(firstCard.value().rows.size() == 1);
}
