#include "support/Session.h"

#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/posting/AdvanceOffset.h"
#include "fin/print/PrintForms.h"
#include "fin/reports/Reports.h"

using namespace fin;
using namespace fin::test;

TEST_CASE("chart includes advance accounts 371 and 681")
{
  LedgerSession session = openSession();
  REQUIRE(account(session, chart::kAdvancesIssued).isValid());
  REQUIRE(account(session, chart::kAdvancesReceived).isValid());
  const Result<Account> folder = session.accounts().findByCode(QStringLiteral("37"));
  REQUIRE(folder);
  REQUIRE(folder.value().isFolder);
}

TEST_CASE("bank advance 681 requires a counterparty")
{
  LedgerSession session = openSession();
  Document document =
      makeDoc(DocumentType::BankPayment, QDate(2026, 3, 1), QStringLiteral("РР-adv"));
  document.lines.push_back(
      line(account(session, chart::kBank), account(session, chart::kAdvancesReceived), 50000));
  const Result<DocumentId> id = session.documents().insert(document);
  REQUIRE(id);
  REQUIRE_FALSE(session.posting().post(id.value()));
}

TEST_CASE("sale offsets received customer advance and journal total stays revenue")
{
  LedgerSession session = openSession();
  const CounterpartyId buyer = customer(session, QStringLiteral("Авансовий покупець"));
  const NomenclatureId flour = item(session, QStringLiteral("Борошно"));

  Document opening =
      makeDoc(DocumentType::OpeningBalance, QDate(2026, 1, 1), QStringLiteral("ЗС-adv"));
  DocumentLine stock = stockLine(account(session, chart::kGoods),
                                 account(session, chart::kAuxiliary), flour, 10000, 4000);
  opening.lines.push_back(stock);
  saveAndPost(session, opening);

  Document advance =
      makeDoc(DocumentType::BankPayment, QDate(2026, 2, 1), QStringLiteral("РР-681"));
  advance.counterpartyId = buyer;
  advance.lines.push_back(line(account(session, chart::kBank),
                               account(session, chart::kAdvancesReceived), 80000,
                               QStringLiteral("передоплата")));
  saveAndPost(session, advance);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 2, 10), QStringLiteral("РТ-offset"));
  sale.counterpartyId = buyer;
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  GoodsLine row;
  row.nomenclatureId = flour;
  row.quantity = Quantity::fromMilli(2000);
  row.unitPrice = Money::fromCents(5000);
  row.amount = Money::fromCents(10000);
  const Result<std::vector<DocumentLine>> composed = composeSale(accounts.value(), {row});
  REQUIRE(composed);
  sale.lines = composed.value();
  const DocumentId saleId = saveAndPost(session, sale);

  Result<Document> posted = session.documents().findById(saleId);
  REQUIRE(posted);
  REQUIRE(
      advanceOffsetAmount(accounts.value(), posted.value().type, posted.value().lines).cents() ==
      10000);

  const Result<std::vector<DocumentHeader>> list =
      session.documents().list(QDate(2026, 2, 1), QDate(2026, 2, 28), DocumentType::Sale);
  REQUIRE(list);
  REQUIRE(list.value().size() == 1);
  REQUIRE(list.value().front().total.cents() == 18000);

  const Result<AccountCard> customers = session.accountCard().build(
      account(session, chart::kCustomers), QDate(2026, 2, 1), QDate(2026, 2, 28), buyer);
  REQUIRE(customers);
  REQUIRE(customers.value().closingDebit.isZero());
  REQUIRE(customers.value().closingCredit.isZero());

  const Result<AccountCard> received = session.accountCard().build(
      account(session, chart::kAdvancesReceived), QDate(2026, 2, 1), QDate(2026, 2, 28), buyer);
  REQUIRE(received);
  REQUIRE(received.value().closingCredit.cents() == 70000);

  REQUIRE(session.posting().unpost(saleId));
  Result<Document> draft = session.documents().findById(saleId);
  REQUIRE(draft);
  REQUIRE(advanceOffsetAmount(accounts.value(), draft.value().type, draft.value().lines).isZero());
}

TEST_CASE("purchase offsets issued supplier advance")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Млин"));
  const NomenclatureId sugar = item(session, QStringLiteral("Цукор"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document prepaid =
      makeDoc(DocumentType::BankPayment, QDate(2026, 4, 1), QStringLiteral("РР-371"));
  prepaid.counterpartyId = mill;
  prepaid.lines.push_back(
      line(account(session, chart::kAdvancesIssued), account(session, chart::kBank), 50000));
  saveAndPost(session, prepaid);

  Document purchase =
      makeDoc(DocumentType::Purchase, QDate(2026, 4, 5), QStringLiteral("НП-offset"));
  purchase.counterpartyId = mill;
  GoodsLine row;
  row.nomenclatureId = sugar;
  row.quantity = Quantity::fromMilli(1000);
  row.unitPrice = Money::fromCents(30000);
  row.amount = Money::fromCents(30000);
  const Result<std::vector<DocumentLine>> composed = composePurchase(accounts.value(), {row});
  REQUIRE(composed);
  purchase.lines = composed.value();
  const DocumentId id = saveAndPost(session, purchase);
  Result<Document> posted = session.documents().findById(id);
  REQUIRE(posted);
  REQUIRE(
      advanceOffsetAmount(accounts.value(), posted.value().type, posted.value().lines).cents() ==
      30000);
}

TEST_CASE("service purchase offsets issued supplier advance")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Оренда-аванс"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document prepaid =
      makeDoc(DocumentType::BankPayment, QDate(2026, 5, 1), QStringLiteral("РР-371-посл"));
  prepaid.counterpartyId = mill;
  prepaid.lines.push_back(
      line(account(session, chart::kAdvancesIssued), account(session, chart::kBank), 500000));
  saveAndPost(session, prepaid);

  CashLine rent;
  rent.correspondenceId = account(session, chart::kAdminExpense);
  rent.amount = Money::fromCents(300000);
  rent.description = QStringLiteral("оренда травень");
  Document purchase =
      makeDoc(DocumentType::ServicePurchase, QDate(2026, 5, 15), QStringLiteral("ПУ-offset"));
  purchase.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> composed =
      composeServicePurchase(accounts.value(), {rent});
  REQUIRE(composed);
  purchase.lines = composed.value();
  const DocumentId id = saveAndPost(session, purchase);
  Result<Document> posted = session.documents().findById(id);
  REQUIRE(posted);
  REQUIRE(
      advanceOffsetAmount(accounts.value(), posted.value().type, posted.value().lines).cents() ==
      300000);

  Result<Document> bank = buildBasedOn(posted.value(), DocumentType::BankPayment, accounts.value());
  REQUIRE(bank);
  REQUIRE_FALSE(bankDocumentIncoming(bank.value().lines, accounts.value()));
  REQUIRE(bank.value().lines.front().amount.cents() == 300000);

  Result<Document> cash = buildBasedOn(posted.value(), DocumentType::CashOut, accounts.value());
  REQUIRE(cash);
  REQUIRE(cash.value().type == DocumentType::CashOut);
  REQUIRE_FALSE(buildBasedOn(posted.value(), DocumentType::Sale, accounts.value()));
}

TEST_CASE("based-on copies invoice goods into a sale draft")
{
  LedgerSession session = openSession();
  const CounterpartyId buyer = customer(session, QStringLiteral("Рахунок"));
  const NomenclatureId itemId = item(session, QStringLiteral("Кава"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document invoice =
      makeDoc(DocumentType::CustomerInvoice, QDate(2026, 5, 1), QStringLiteral("РХ-1"));
  invoice.counterpartyId = buyer;
  invoice.id = DocumentId{42};
  GoodsLine row;
  row.nomenclatureId = itemId;
  row.quantity = Quantity::fromMilli(2000);
  row.unitPrice = Money::fromCents(26000);
  row.amount = Money::fromCents(52000);
  const Result<std::vector<DocumentLine>> lines = composeCustomerInvoice(accounts.value(), {row});
  REQUIRE(lines);
  invoice.lines = lines.value();

  Result<Document> sale = buildBasedOn(invoice, DocumentType::Sale, accounts.value());
  REQUIRE(sale);
  REQUIRE(sale.value().type == DocumentType::Sale);
  REQUIRE(sale.value().counterpartyId == buyer);
  REQUIRE(sale.value().basedOnId.value == 42);
  REQUIRE(saleRevenueTotal(sale.value().lines, accounts.value()).cents() == 52000);

  Result<Document> bank = buildBasedOn(invoice, DocumentType::BankPayment, accounts.value());
  REQUIRE(bank);
  REQUIRE(bankDocumentIncoming(bank.value().lines, accounts.value()));
  REQUIRE(bank.value().lines.front().amount.cents() == 52000);

  REQUIRE_FALSE(buildBasedOn(invoice, DocumentType::WriteOff, accounts.value()));
}

TEST_CASE("reconciliation act matches the account card")
{
  LedgerSession session = openSession();
  const CounterpartyId buyer = customer(session, QStringLiteral("Звірка"));
  Document cash = makeDoc(DocumentType::CashIn, QDate(2026, 6, 2), QStringLiteral("ПКО-rec"));
  cash.counterpartyId = buyer;
  cash.lines.push_back(line(account(session, chart::kCash), account(session, chart::kCustomers),
                            15000, QStringLiteral("оплата")));
  saveAndPost(session, cash);

  const AccountId customers = account(session, chart::kCustomers);
  const Result<ReconciliationAct> act =
      ReconciliationReport(session.handle())
          .build(customers, buyer, QDate(2026, 6, 1), QDate(2026, 6, 30));
  REQUIRE(act);
  const Result<AccountCard> card =
      session.accountCard().build(customers, QDate(2026, 6, 1), QDate(2026, 6, 30), buyer);
  REQUIRE(card);
  REQUIRE(act.value().closingCredit == card.value().closingCredit);
  REQUIRE(act.value().turnoverCredit == card.value().turnoverCredit);
  REQUIRE(act.value().rows.size() == card.value().rows.size());

  const Result<QString> html = PrintForms(session.handle()).reconciliationHtml(act.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("Акт звірки")));
  REQUIRE(html.value().contains(QStringLiteral("Звірка")));
}

TEST_CASE("stock shortage names the item")
{
  LedgerSession session = openSession();
  const CounterpartyId buyer = customer(session, QStringLiteral("Покупець"));
  const NomenclatureId flour = item(session, QStringLiteral("Борошно в/г"));
  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 7, 1), QStringLiteral("РТ-stock"));
  sale.counterpartyId = buyer;
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  GoodsLine row;
  row.nomenclatureId = flour;
  row.quantity = Quantity::fromMilli(1000);
  row.unitPrice = Money::fromCents(1000);
  row.amount = Money::fromCents(1000);
  const Result<std::vector<DocumentLine>> composed = composeSale(accounts.value(), {row});
  REQUIRE(composed);
  sale.lines = composed.value();
  const Result<DocumentId> id = session.documents().insert(sale);
  REQUIRE(id);
  const Status posted = session.posting().post(id.value());
  REQUIRE_FALSE(posted);
  REQUIRE(posted.message().contains(QStringLiteral("Борошно в/г")));
}
