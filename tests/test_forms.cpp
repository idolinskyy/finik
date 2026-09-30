#include "support/Session.h"

#include "fin/auth/Access.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"

using namespace fin;
using namespace fin::test;

TEST_CASE("compose purchase and sale round-trip without debit-credit in the form")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId flour = item(session, QStringLiteral("Борошно"));

  GoodsLine row;
  row.nomenclatureId = flour;
  row.quantity = Quantity::fromMilli(2500);
  row.unitPrice = Money::fromCents(4000);
  row.amount = Money::fromCents(10000);
  row.description = QStringLiteral("мішок");

  const Result<std::vector<DocumentLine>> purchase = composePurchase(accounts.value(), {row});
  REQUIRE(purchase);
  REQUIRE(purchase.value().size() == 1);
  REQUIRE(purchase.value()[0].debitAccountId == account(session, chart::kGoods));
  REQUIRE(purchase.value()[0].creditAccountId == account(session, chart::kSuppliers));

  const std::vector<GoodsLine> extractedPurchase =
      extractGoodsLines(DocumentType::Purchase, purchase.value(), accounts.value());
  REQUIRE(extractedPurchase.size() == 1);
  REQUIRE(extractedPurchase[0].nomenclatureId == flour);
  REQUIRE(extractedPurchase[0].amount.cents() == 10000);

  const Result<std::vector<DocumentLine>> sale = composeSale(accounts.value(), {row});
  REQUIRE(sale);
  REQUIRE(sale.value().size() == 2);
  REQUIRE(sale.value()[0].amount.cents() == 10000);
  REQUIRE(sale.value()[1].nomenclatureId == flour);
  REQUIRE(sale.value()[1].quantity.milli() == 2500);
  REQUIRE(sale.value()[1].amount.cents() == 1);

  const std::vector<GoodsLine> extractedSale =
      extractGoodsLines(DocumentType::Sale, sale.value(), accounts.value());
  REQUIRE(extractedSale.size() == 1);
  REQUIRE(extractedSale[0].amount.cents() == 10000);
  REQUIRE(extractedSale[0].quantity.milli() == 2500);
}

TEST_CASE("composed sale posts with average cost")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId tea = item(session, QStringLiteral("Чай листовий"));
  const CounterpartyId mill = customer(session, QStringLiteral("Опт"));
  const CounterpartyId shop = customer(session, QStringLiteral("Роздріб"));

  GoodsLine buy;
  buy.nomenclatureId = tea;
  buy.quantity = Quantity::fromMilli(2000);
  buy.unitPrice = Money::fromCents(10000);
  buy.amount = Money::fromCents(20000);
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 9, 1), QStringLiteral("НП-form"));
  purchase.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> buyLines = composePurchase(accounts.value(), {buy});
  REQUIRE(buyLines);
  purchase.lines = buyLines.value();
  saveAndPost(session, purchase);

  GoodsLine sell;
  sell.nomenclatureId = tea;
  sell.quantity = Quantity::fromMilli(1000);
  sell.unitPrice = Money::fromCents(18000);
  sell.amount = Money::fromCents(18000);
  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 9, 2), QStringLiteral("РТ-form"));
  sale.counterpartyId = shop;
  const Result<std::vector<DocumentLine>> sellLines = composeSale(accounts.value(), {sell});
  REQUIRE(sellLines);
  sale.lines = sellLines.value();
  const DocumentId id = saveAndPost(session, sale);
  const Result<Document> posted = session.documents().findById(id);
  REQUIRE(posted);
  REQUIRE(posted.value().lines[0].amount.cents() == 18000);
  REQUIRE(posted.value().lines[1].amount.cents() == 10000);
}

TEST_CASE("compose cash in uses 301 debit")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  CashLine row;
  row.correspondenceId = account(session, chart::kCustomers);
  row.amount = Money::fromCents(50000);
  row.description = QStringLiteral("оплата");
  const Result<std::vector<DocumentLine>> lines = composeCashIn(accounts.value(), {row});
  REQUIRE(lines);
  REQUIRE(lines.value().size() == 1);
  REQUIRE(lines.value()[0].debitAccountId == account(session, chart::kCash));
  REQUIRE(lines.value()[0].creditAccountId == account(session, chart::kCustomers));
  const std::vector<CashLine> back =
      extractCashLines(DocumentType::CashIn, lines.value(), accounts.value());
  REQUIRE(back.size() == 1);
  REQUIRE(back[0].amount.cents() == 50000);
}

TEST_CASE("compose cash out uses 301 credit")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  CashLine row;
  row.correspondenceId = account(session, chart::kAdminExpense);
  row.amount = Money::fromCents(1200);
  const Result<std::vector<DocumentLine>> lines = composeCashOut(accounts.value(), {row});
  REQUIRE(lines);
  REQUIRE(lines.value()[0].debitAccountId == account(session, chart::kAdminExpense));
  REQUIRE(lines.value()[0].creditAccountId == account(session, chart::kCash));
}

TEST_CASE("compose bank incoming and outgoing without mixing")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  BankLine row;
  row.amount = Money::fromCents(90000);
  const Result<std::vector<DocumentLine>> incoming = composeBank(accounts.value(), true, {row});
  REQUIRE(incoming);
  REQUIRE(incoming.value()[0].debitAccountId == account(session, chart::kBank));
  REQUIRE(incoming.value()[0].creditAccountId == account(session, chart::kCustomers));
  REQUIRE(bankDocumentIncoming(incoming.value(), accounts.value()));

  const Result<std::vector<DocumentLine>> outgoing = composeBank(accounts.value(), false, {row});
  REQUIRE(outgoing);
  REQUIRE(outgoing.value()[0].debitAccountId == account(session, chart::kSuppliers));
  REQUIRE_FALSE(bankDocumentIncoming(outgoing.value(), accounts.value()));

  BankLine fee;
  fee.amount = Money::fromCents(1500);
  fee.correspondenceId = account(session, chart::kAdminExpense);
  const Result<std::vector<DocumentLine>> commission = composeBank(accounts.value(), false, {fee});
  REQUIRE(commission);
  REQUIRE(commission.value()[0].debitAccountId == account(session, chart::kAdminExpense));
  REQUIRE(commission.value()[0].creditAccountId == account(session, chart::kBank));

  BankLine cashIn;
  cashIn.amount = Money::fromCents(3000);
  cashIn.correspondenceId = account(session, chart::kCash);
  const Result<std::vector<DocumentLine>> toCash = composeBank(accounts.value(), false, {cashIn});
  REQUIRE(toCash);
  REQUIRE(toCash.value()[0].debitAccountId == account(session, chart::kCash));
  const std::vector<BankLine> extracted = extractBankLines(toCash.value(), accounts.value());
  REQUIRE(extracted.size() == 1);
  REQUIRE(extracted[0].correspondenceId == account(session, chart::kCash));
}

TEST_CASE("compose opening uses 00 and goods qty")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId flour = item(session, QStringLiteral("Борошно-open"));
  OpeningMoneyLine cash;
  cash.accountId = account(session, chart::kCash);
  cash.debit = Money::fromCents(1000);
  GoodsLine goods;
  goods.nomenclatureId = flour;
  goods.quantity = Quantity::fromMilli(2000);
  goods.unitPrice = Money::fromCents(500);
  goods.amount = Money::fromCents(1000);
  const Result<std::vector<DocumentLine>> lines = composeOpening(accounts.value(), {cash}, {goods});
  REQUIRE(lines);
  REQUIRE(lines.value().size() == 2);
  REQUIRE(lines.value()[0].creditAccountId == account(session, chart::kAuxiliary));
  REQUIRE(lines.value()[1].nomenclatureId == flour);
  REQUIRE(lines.value()[1].quantity.milli() == 2000);

  OpeningMoneyLine capital;
  capital.accountId = account(session, chart::kCapital);
  capital.credit = Money::fromCents(2000);
  const Result<std::vector<DocumentLine>> balanced =
      composeOpening(accounts.value(), {cash, capital}, {goods});
  REQUIRE(balanced);
  Document opening =
      makeDoc(DocumentType::OpeningBalance, QDate(2026, 1, 1), QStringLiteral("ЗС-open"));
  opening.lines = balanced.value();
  saveAndPost(session, opening);
  REQUIRE(session.verify());
}

TEST_CASE("sale return restores stock at average cost")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId tea = item(session, QStringLiteral("Чай-повернення"));
  const CounterpartyId mill = customer(session, QStringLiteral("Опт-пов"));
  const CounterpartyId shop = customer(session, QStringLiteral("Роздріб-пов"));

  GoodsLine buy;
  buy.nomenclatureId = tea;
  buy.quantity = Quantity::fromMilli(2000);
  buy.unitPrice = Money::fromCents(10000);
  buy.amount = Money::fromCents(20000);
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 9, 1), QStringLiteral("НП-ret"));
  purchase.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> buyLines = composePurchase(accounts.value(), {buy});
  REQUIRE(buyLines);
  purchase.lines = buyLines.value();
  saveAndPost(session, purchase);

  GoodsLine sell = buy;
  sell.quantity = Quantity::fromMilli(1000);
  sell.unitPrice = Money::fromCents(18000);
  sell.amount = Money::fromCents(18000);
  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 9, 2), QStringLiteral("РТ-ret"));
  sale.counterpartyId = shop;
  const Result<std::vector<DocumentLine>> sellLines = composeSale(accounts.value(), {sell});
  REQUIRE(sellLines);
  sale.lines = sellLines.value();
  saveAndPost(session, sale);

  GoodsLine back = sell;
  Document ret = makeDoc(DocumentType::SaleReturn, QDate(2026, 9, 3), QStringLiteral("ПВ-ret"));
  ret.counterpartyId = shop;
  const Result<std::vector<DocumentLine>> retLines = composeSaleReturn(accounts.value(), {back});
  REQUIRE(retLines);
  ret.lines = retLines.value();
  const DocumentId id = saveAndPost(session, ret);
  const Result<Document> posted = session.documents().findById(id);
  REQUIRE(posted);
  REQUIRE(posted.value().lines[0].amount.cents() == 18000);
  REQUIRE(posted.value().lines[1].amount.cents() == 10000);
  REQUIRE(session.verify());
}

TEST_CASE("write-off and stocktake keep 281 in agreement with stock")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId salt = item(session, QStringLiteral("Сіль"));
  const CounterpartyId mill = customer(session, QStringLiteral("Сільзавод"));

  GoodsLine buy;
  buy.nomenclatureId = salt;
  buy.quantity = Quantity::fromMilli(5000);
  buy.unitPrice = Money::fromCents(2000);
  buy.amount = Money::fromCents(10000);
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 9, 1), QStringLiteral("НП-inv"));
  purchase.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> buyLines = composePurchase(accounts.value(), {buy});
  REQUIRE(buyLines);
  purchase.lines = buyLines.value();
  saveAndPost(session, purchase);

  GoodsLine scrap;
  scrap.nomenclatureId = salt;
  scrap.quantity = Quantity::fromMilli(1000);
  Document writeOff = makeDoc(DocumentType::WriteOff, QDate(2026, 9, 4), QStringLiteral("СП-inv"));
  const Result<std::vector<DocumentLine>> woLines = composeWriteOff(accounts.value(), {scrap});
  REQUIRE(woLines);
  writeOff.lines = woLines.value();
  saveAndPost(session, writeOff);

  const Result<std::vector<StockBalance>> onHand = session.inventory().balances();
  REQUIRE(onHand);
  GoodsLine counted;
  counted.nomenclatureId = salt;
  counted.quantity = Quantity::fromMilli(3500);
  Document stocktake =
      makeDoc(DocumentType::Stocktake, QDate(2026, 9, 5), QStringLiteral("ІН-inv"));
  const Result<std::vector<DocumentLine>> takeLines =
      composeStocktake(accounts.value(), {counted}, onHand.value());
  REQUIRE(takeLines);
  REQUIRE(takeLines.value().size() == 1);
  stocktake.lines = takeLines.value();
  saveAndPost(session, stocktake);
  REQUIRE(session.verify());
}

TEST_CASE("service sale posts 361/703 without stock")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const CounterpartyId buyer = customer(session, QStringLiteral("Замовник послуг"));
  GoodsLine row;
  row.description = QStringLiteral("консультація");
  row.amount = Money::fromCents(250000);
  const Result<std::vector<DocumentLine>> lines = composeServiceSale(accounts.value(), {row});
  REQUIRE(lines);
  REQUIRE(lines.value().size() == 1);
  REQUIRE(lines.value()[0].debitAccountId == account(session, chart::kCustomers));
  REQUIRE(lines.value()[0].creditAccountId == account(session, chart::kServiceRevenue));
  Document document =
      makeDoc(DocumentType::ServiceSale, QDate(2026, 9, 8), QStringLiteral("РП-svc"));
  document.counterpartyId = buyer;
  document.lines = lines.value();
  saveAndPost(session, document);
  REQUIRE(extractGoodsLines(DocumentType::ServiceSale, lines.value(), accounts.value())
              .front()
              .amount.cents() == 250000);
}

TEST_CASE("service purchase posts 92/631 and rejects goods 281")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const CounterpartyId mill = customer(session, QStringLiteral("Орендодавець"));

  CashLine rent;
  rent.correspondenceId = account(session, chart::kAdminExpense);
  rent.amount = Money::fromCents(800000);
  rent.description = QStringLiteral("оренда офісу");
  const Result<std::vector<DocumentLine>> expenseLines =
      composeServicePurchase(accounts.value(), {rent});
  REQUIRE(expenseLines);
  REQUIRE(expenseLines.value().size() == 1);
  REQUIRE(expenseLines.value()[0].debitAccountId == account(session, chart::kAdminExpense));
  REQUIRE(expenseLines.value()[0].creditAccountId == account(session, chart::kSuppliers));
  Document expense =
      makeDoc(DocumentType::ServicePurchase, QDate(2026, 5, 15), QStringLiteral("ПУ-92"));
  expense.counterpartyId = mill;
  expense.lines = expenseLines.value();
  saveAndPost(session, expense);
  REQUIRE(extractServicePurchaseLines(expense.lines, accounts.value()).front().amount.cents() ==
          800000);

  CashLine capex;
  capex.correspondenceId = account(session, chart::kCapexFa);
  capex.amount = Money::fromCents(1200000);
  capex.description = QStringLiteral("монтаж верстата");
  const Result<std::vector<DocumentLine>> capexLines =
      composeServicePurchase(accounts.value(), {capex});
  REQUIRE(capexLines);
  REQUIRE(capexLines.value()[0].debitAccountId == account(session, chart::kCapexFa));
  Document capexDoc =
      makeDoc(DocumentType::ServicePurchase, QDate(2026, 5, 16), QStringLiteral("ПУ-152"));
  capexDoc.counterpartyId = mill;
  capexDoc.lines = capexLines.value();
  saveAndPost(session, capexDoc);

  CashLine goods;
  goods.correspondenceId = account(session, chart::kGoods);
  goods.amount = Money::fromCents(10000);
  REQUIRE_FALSE(composeServicePurchase(accounts.value(), {goods}));

  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Cashier, DocumentType::ServicePurchase));
  REQUIRE(Access::canWriteDocument(UserRole::Accountant, DocumentType::ServicePurchase));
}

TEST_CASE("customer invoice is stored but never posted")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId tea = item(session, QStringLiteral("Чай-рахунок"));
  const CounterpartyId buyer = customer(session, QStringLiteral("Покупець рахунку"));
  GoodsLine row;
  row.nomenclatureId = tea;
  row.quantity = Quantity::fromMilli(1000);
  row.unitPrice = Money::fromCents(10000);
  row.amount = Money::fromCents(10000);
  const Result<std::vector<DocumentLine>> lines = composeCustomerInvoice(accounts.value(), {row});
  REQUIRE(lines);
  Document invoice =
      makeDoc(DocumentType::CustomerInvoice, QDate(2026, 9, 8), QStringLiteral("РХ-1"));
  invoice.counterpartyId = buyer;
  invoice.lines = lines.value();
  const Result<DocumentId> id = session.documents().insert(invoice);
  REQUIRE(id);
  REQUIRE_FALSE(session.posting().post(id.value()));
  REQUIRE(requiresLedgerPosting(DocumentType::Sale));
  REQUIRE_FALSE(requiresLedgerPosting(DocumentType::CustomerInvoice));
}

TEST_CASE("purchase return writes 631/281 at average cost")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId tea = item(session, QStringLiteral("Чай-пост"));
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник-пов"));

  GoodsLine buy;
  buy.nomenclatureId = tea;
  buy.quantity = Quantity::fromMilli(2000);
  buy.unitPrice = Money::fromCents(10000);
  buy.amount = Money::fromCents(20000);
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 9, 1), QStringLiteral("НП-pret"));
  purchase.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> buyLines = composePurchase(accounts.value(), {buy});
  REQUIRE(buyLines);
  purchase.lines = buyLines.value();
  saveAndPost(session, purchase);

  GoodsLine back;
  back.nomenclatureId = tea;
  back.quantity = Quantity::fromMilli(1000);
  Document ret =
      makeDoc(DocumentType::PurchaseReturn, QDate(2026, 9, 2), QStringLiteral("ПП-pret"));
  ret.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> retLines =
      composePurchaseReturn(accounts.value(), {back});
  REQUIRE(retLines);
  ret.lines = retLines.value();
  const DocumentId id = saveAndPost(session, ret);
  const Result<Document> posted = session.documents().findById(id);
  REQUIRE(posted);
  REQUIRE(posted.value().lines[0].debitAccountId == account(session, chart::kSuppliers));
  REQUIRE(posted.value().lines[0].amount.cents() == 10000);
  REQUIRE(session.verify());
}

TEST_CASE("stocktake surplus posts 281/71")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId tea = item(session, QStringLiteral("Чай-надлишок"));
  const CounterpartyId mill = customer(session, QStringLiteral("Склад-надлишок"));
  GoodsLine buy;
  buy.nomenclatureId = tea;
  buy.quantity = Quantity::fromMilli(1000);
  buy.unitPrice = Money::fromCents(4000);
  buy.amount = Money::fromCents(4000);
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 9, 1), QStringLiteral("НП-sur"));
  purchase.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> buyLines = composePurchase(accounts.value(), {buy});
  REQUIRE(buyLines);
  purchase.lines = buyLines.value();
  saveAndPost(session, purchase);

  const Result<std::vector<StockBalance>> onHand = session.inventory().balances();
  REQUIRE(onHand);
  GoodsLine counted;
  counted.nomenclatureId = tea;
  counted.quantity = Quantity::fromMilli(1500);
  Document stocktake =
      makeDoc(DocumentType::Stocktake, QDate(2026, 9, 6), QStringLiteral("ІН-sur"));
  const Result<std::vector<DocumentLine>> takeLines =
      composeStocktake(accounts.value(), {counted}, onHand.value());
  REQUIRE(takeLines);
  REQUIRE(takeLines.value().size() == 1);
  REQUIRE(takeLines.value()[0].debitAccountId == account(session, chart::kGoods));
  REQUIRE(takeLines.value()[0].creditAccountId == account(session, chart::kOtherIncome));
  stocktake.lines = takeLines.value();
  saveAndPost(session, stocktake);
  REQUIRE(session.verify());
}
