#include "support/Session.h"

#include "fin/auth/Access.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/LowValueItem.h"
#include "fin/domain/MonthClose.h"
#include "fin/domain/Vat.h"
#include "fin/print/PrintForms.h"
#include "fin/reports/Reports.h"

using namespace fin;
using namespace fin::test;

namespace {

void enableVat(LedgerSession & session)
{
  Result<CompanyProfile> loaded = session.company().load();
  REQUIRE(loaded);
  CompanyProfile profile = loaded.value();
  profile.vatPayer = true;
  profile.vatRateBp = 2000;
  REQUIRE(session.company().save(profile));
}

DocumentLine supplierLine(LedgerSession & session, std::int64_t cents)
{
  return line(account(session, chart::kAdminExpense), account(session, chart::kSuppliers), cents);
}

GoodsLine goods(NomenclatureId id, std::int64_t milli, std::int64_t price, std::int64_t net,
                int rateBp = 2000)
{
  GoodsLine row;
  row.nomenclatureId = id;
  row.quantity = Quantity::fromMilli(milli);
  row.unitPrice = Money::fromCents(price);
  row.amount = Money::fromCents(net);
  row.vatRateBp = rateBp;
  const Result<Money> vat = vatOnNet(row.amount, rateBp);
  REQUIRE(vat);
  row.vatCents = vat.value().cents();
  return row;
}

} // namespace

TEST_CASE("vat arithmetic is integer kopiyky")
{
  const Result<Money> vat = vatOnNet(Money::fromCents(10000), 2000);
  REQUIRE(vat);
  REQUIRE(vat.value() == Money::fromCents(2000));
  const Result<Money> gross = grossFromNet(Money::fromCents(10000), 2000);
  REQUIRE(gross);
  REQUIRE(gross.value() == Money::fromCents(12000));
  const Result<Money> included = vatIncludedInGross(Money::fromCents(12000), 2000);
  REQUIRE(included);
  REQUIRE(included.value() == Money::fromCents(2000));
  const Result<Money> net = netFromGross(Money::fromCents(12000), 2000);
  REQUIRE(net);
  REQUIRE(net.value() == Money::fromCents(10000));
}

TEST_CASE("non-payer keeps old documents and cannot post a tax invoice")
{
  LedgerSession session = openSession();
  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);
  REQUIRE_FALSE(company.value().vatPayer);

  const CounterpartyId party = customer(session, QStringLiteral("Покупець"));
  const NomenclatureId flour = item(session, QStringLiteral("Борошно"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 3, 1), QStringLiteral("НП-vat0"));
  purchase.counterpartyId = party;
  const Result<std::vector<DocumentLine>> buy =
      composePurchase(accounts.value(), {goods(flour, 1000, 10000, 10000)});
  REQUIRE(buy);
  purchase.lines = buy.value();
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 3, 2), QStringLiteral("РТ-vat0"));
  sale.counterpartyId = party;
  const Result<std::vector<DocumentLine>> sell =
      composeSale(accounts.value(), {goods(flour, 1000, 15000, 15000)});
  REQUIRE(sell);
  sale.lines = sell.value();
  const DocumentId saleId = saveAndPost(session, sale);
  const Result<Document> posted = session.documents().findById(saleId);
  REQUIRE(posted);

  const Result<Document> pn = composeTaxInvoice(session.handle(), accounts.value(), posted.value(),
                                                company.value(), QDate(2026, 3, 2));
  REQUIRE_FALSE(pn);

  REQUIRE(session.accounts().findByCode(QString::fromLatin1(chart::kVatBudget)));
  REQUIRE(session.accounts().findByCode(QString::fromLatin1(chart::kVatLiability)));
  REQUIRE(session.accounts().findByCode(QString::fromLatin1(chart::kVatCredit)));
}

TEST_CASE("sale tax invoice posts 643 then registration moves to 641")
{
  LedgerSession session = openSession();
  enableVat(session);
  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);

  const CounterpartyId party = customer(session, QStringLiteral("ТОВ Покупець"));
  const NomenclatureId flour = item(session, QStringLiteral("Борошно"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 4, 1), QStringLiteral("НП-vat1"));
  purchase.counterpartyId = party;
  const Result<std::vector<DocumentLine>> buy =
      composePurchase(accounts.value(), {goods(flour, 1000, 10000, 10000)});
  REQUIRE(buy);
  purchase.lines = buy.value();
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 4, 2), QStringLiteral("РТ-vat1"));
  sale.counterpartyId = party;
  const Result<std::vector<DocumentLine>> sell =
      composeSale(accounts.value(), {goods(flour, 1000, 10000, 10000)});
  REQUIRE(sell);
  sale.lines = sell.value();
  const DocumentId saleId = saveAndPost(session, sale);
  const Result<Document> postedSale = session.documents().findById(saleId);
  REQUIRE(postedSale);

  Result<Document> pn = composeTaxInvoice(session.handle(), accounts.value(), postedSale.value(),
                                          company.value(), QDate(2026, 4, 2));
  REQUIRE(pn);
  pn.value().number = QStringLiteral("ПН-000001");
  const DocumentId pnId = saveAndPost(session, pn.value());
  const Result<std::vector<LedgerEntry>> ledger = session.ledger().listForDocument(pnId);
  REQUIRE(ledger);
  REQUIRE(ledger.value().size() == 1);
  REQUIRE(ledger.value().front().debitAccountId == account(session, chart::kCustomers));
  REQUIRE(ledger.value().front().creditAccountId == account(session, chart::kVatLiability));
  REQUIRE(ledger.value().front().amount == Money::fromCents(2000));

  REQUIRE(session.posting().registerTaxInvoice(pnId, QDate(2026, 4, 10)));
  const Result<std::vector<LedgerEntry>> registered = session.ledger().listForDocument(pnId);
  REQUIRE(registered);
  REQUIRE(registered.value().size() == 2);
  REQUIRE(registered.value().back().date == QDate(2026, 4, 10));
  REQUIRE(registered.value().back().debitAccountId == account(session, chart::kVatLiability));
  REQUIRE(registered.value().back().creditAccountId == account(session, chart::kVatBudget));
  REQUIRE(session.verify());

  REQUIRE(session.posting().unregisterTaxInvoice(pnId));
  const Result<std::vector<LedgerEntry>> undone = session.ledger().listForDocument(pnId);
  REQUIRE(undone);
  REQUIRE(undone.value().size() == 1);
  REQUIRE(session.verify());

  REQUIRE(session.posting().unpost(pnId));
  REQUIRE(session.verify());
}

TEST_CASE("first event: payment then shipment does not double VAT")
{
  LedgerSession session = openSession();
  enableVat(session);
  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);
  const CounterpartyId party = customer(session, QStringLiteral("Аванс-покупець"));
  const NomenclatureId flour = item(session, QStringLiteral("Цукор"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document bank = makeDoc(DocumentType::BankPayment, QDate(2026, 5, 1), QStringLiteral("РР-adv"));
  bank.counterpartyId = party;
  const Result<std::vector<DocumentLine>> pay =
      composeBank(accounts.value(), true,
                  {{account(session, chart::kAdvancesReceived), Money::fromCents(12000),
                    QStringLiteral("передоплата")}});
  REQUIRE(pay);
  bank.lines = pay.value();
  const DocumentId bankId = saveAndPost(session, bank);
  const Result<Document> postedBank = session.documents().findById(bankId);
  REQUIRE(postedBank);

  Result<Document> advancePn = composeTaxInvoice(
      session.handle(), accounts.value(), postedBank.value(), company.value(), QDate(2026, 5, 1));
  REQUIRE(advancePn);
  advancePn.value().number = QStringLiteral("ПН-adv");
  saveAndPost(session, advancePn.value());

  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 5, 2), QStringLiteral("НП-adv"));
  purchase.counterpartyId = party;
  purchase.lines = composePurchase(accounts.value(), {goods(flour, 1000, 10000, 10000)}).value();
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 5, 3), QStringLiteral("РТ-adv"));
  sale.counterpartyId = party;
  sale.lines = composeSale(accounts.value(), {goods(flour, 1000, 10000, 10000)}).value();
  const DocumentId saleId = saveAndPost(session, sale);
  const Result<Document> postedSale = session.documents().findById(saleId);
  REQUIRE(postedSale);

  const Result<Document> salePn = composeTaxInvoice(
      session.handle(), accounts.value(), postedSale.value(), company.value(), QDate(2026, 5, 3));
  REQUIRE_FALSE(salePn);

  const Result<AccountCard> liability = session.accountCard().build(
      account(session, chart::kVatLiability), QDate(2026, 5, 1), QDate(2026, 5, 31));
  REQUIRE(liability);
  REQUIRE(liability.value().turnoverCredit == Money::fromCents(2000));
  REQUIRE(session.verify());
}

TEST_CASE("first event: shipment then payment does not double VAT")
{
  LedgerSession session = openSession();
  enableVat(session);
  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);
  const CounterpartyId party = customer(session, QStringLiteral("Відвантаження"));
  const NomenclatureId flour = item(session, QStringLiteral("Кава"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 6, 1), QStringLiteral("НП-ship"));
  purchase.counterpartyId = party;
  purchase.lines = composePurchase(accounts.value(), {goods(flour, 1000, 10000, 10000)}).value();
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 6, 2), QStringLiteral("РТ-ship"));
  sale.counterpartyId = party;
  sale.lines = composeSale(accounts.value(), {goods(flour, 1000, 10000, 10000)}).value();
  const DocumentId saleId = saveAndPost(session, sale);
  const Result<Document> postedSale = session.documents().findById(saleId);
  REQUIRE(postedSale);
  Result<Document> pn = composeTaxInvoice(session.handle(), accounts.value(), postedSale.value(),
                                          company.value(), QDate(2026, 6, 2));
  REQUIRE(pn);
  pn.value().number = QStringLiteral("ПН-ship");
  saveAndPost(session, pn.value());

  Document bank = makeDoc(DocumentType::BankPayment, QDate(2026, 6, 4), QStringLiteral("РР-ship"));
  bank.counterpartyId = party;
  bank.lines = composeBank(accounts.value(), true,
                           {{account(session, chart::kAdvancesReceived), Money::fromCents(12000),
                             QString()}})
                   .value();
  const DocumentId bankId = saveAndPost(session, bank);
  const Result<Document> salePn = composeTaxInvoice(session.handle(), accounts.value(),
                                                    session.documents().findById(bankId).value(),
                                                    company.value(), QDate(2026, 6, 4));
  REQUIRE_FALSE(salePn);

  const Result<AccountCard> liability = session.accountCard().build(
      account(session, chart::kVatLiability), QDate(2026, 6, 1), QDate(2026, 6, 30));
  REQUIRE(liability);
  REQUIRE(liability.value().turnoverCredit == Money::fromCents(2000));
}

TEST_CASE("purchase and service purchase incoming VAT and the VAT report")
{
  LedgerSession session = openSession();
  enableVat(session);
  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);
  const CounterpartyId supplier = customer(session, QStringLiteral("Постачальник"));
  const NomenclatureId flour = item(session, QStringLiteral("Борошно-вхід"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 7, 1), QStringLiteral("НП-in"));
  purchase.counterpartyId = supplier;
  purchase.lines = composePurchase(accounts.value(), {goods(flour, 2000, 5000, 10000)}).value();
  const DocumentId purchaseId = saveAndPost(session, purchase);
  Result<Document> inPn = composeTaxInvoice(session.handle(), accounts.value(),
                                            session.documents().findById(purchaseId).value(),
                                            company.value(), QDate(2026, 7, 1));
  REQUIRE(inPn);
  inPn.value().number = QStringLiteral("ПН-in");
  const DocumentId inId = saveAndPost(session, inPn.value());
  const Result<std::vector<LedgerEntry>> inLedger = session.ledger().listForDocument(inId);
  REQUIRE(inLedger);
  REQUIRE(inLedger.value().front().debitAccountId == account(session, chart::kVatCredit));
  REQUIRE(inLedger.value().front().creditAccountId == account(session, chart::kSuppliers));

  CashLine service;
  service.correspondenceId = account(session, chart::kAdminExpense);
  service.amount = Money::fromCents(5000);
  service.vatRateBp = 2000;
  service.vatCents = 1000;
  Document expense =
      makeDoc(DocumentType::ServicePurchase, QDate(2026, 7, 5), QStringLiteral("ПУ-in"));
  expense.counterpartyId = supplier;
  expense.lines = composeServicePurchase(accounts.value(), {service}).value();
  const DocumentId expenseId = saveAndPost(session, expense);
  Result<Document> servicePn = composeTaxInvoice(session.handle(), accounts.value(),
                                                 session.documents().findById(expenseId).value(),
                                                 company.value(), QDate(2026, 7, 5));
  REQUIRE(servicePn);
  servicePn.value().number = QStringLiteral("ПН-svc");
  saveAndPost(session, servicePn.value());

  const Result<VatStatement> vat =
      VatStatementReport(session.handle()).build(QDate(2026, 7, 1), QDate(2026, 7, 31));
  REQUIRE(vat);
  REQUIRE(vat.value().credit.turnoverDebit == Money::fromCents(3000));
  const Result<QString> html = PrintForms(session.handle()).vatStatementHtml(vat.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("643")));
  REQUIRE(html.value().contains(QStringLiteral("644")));
  REQUIRE(html.value().contains(QStringLiteral("641")));
  REQUIRE(session.verify());
}

TEST_CASE("customer invoice print shows net and VAT; cashier cannot write PN")
{
  LedgerSession session = openSession();
  enableVat(session);
  const CounterpartyId party = customer(session, QStringLiteral("Рахунок"));
  const NomenclatureId flour = item(session, QStringLiteral("Товар"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document invoice =
      makeDoc(DocumentType::CustomerInvoice, QDate(2026, 8, 1), QStringLiteral("РХ-vat"));
  invoice.counterpartyId = party;
  invoice.lines =
      composeCustomerInvoice(accounts.value(), {goods(flour, 1000, 10000, 10000)}).value();
  const Result<DocumentId> id = session.documents().insert(invoice);
  REQUIRE(id);
  const Result<Document> loaded = session.documents().findById(id.value());
  REQUIRE(loaded);
  const Result<QString> html = PrintForms(session.handle()).documentHtml(loaded.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("ПДВ")));
  REQUIRE(html.value().contains(QStringLiteral("120.00")));
  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Cashier, DocumentType::TaxInvoice));
  REQUIRE(Access::canWriteDocument(UserRole::Accountant, DocumentType::TaxInvoice));
  REQUIRE(documentTypePrefix(DocumentType::TaxInvoice) == QStringLiteral("ПН"));
}

TEST_CASE("FA and MS receipt use net plus VAT for incoming tax invoice")
{
  LedgerSession session = openSession();
  enableVat(session);
  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);
  const CounterpartyId supplier = customer(session, QStringLiteral("ОЗ-постач"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  LowValueItem card;
  card.inventoryNo = QStringLiteral("МШ-1");
  card.name = QStringLiteral("Степлер");
  const Result<LowValueId> msId = session.lowValue().insert(card);
  REQUIRE(msId);
  MsLine ms;
  ms.itemId = msId.value();
  ms.quantity = Quantity::fromMilli(1000);
  ms.unitPrice = Money::fromCents(2000);
  ms.amount = Money::fromCents(2000);
  ms.vatRateBp = 2000;
  ms.vatCents = 400;
  Document receipt = makeDoc(DocumentType::MsReceipt, QDate(2026, 9, 1), QStringLiteral("МШ-vat"));
  receipt.counterpartyId = supplier;
  receipt.lines = composeMsReceipt(accounts.value(), {ms}).value();
  const DocumentId receiptId = saveAndPost(session, receipt);
  Result<Document> pn = composeTaxInvoice(session.handle(), accounts.value(),
                                          session.documents().findById(receiptId).value(),
                                          company.value(), QDate(2026, 9, 1));
  REQUIRE(pn);
  pn.value().number = QStringLiteral("ПН-ms");
  saveAndPost(session, pn.value());
  const Result<AccountCard> credit = session.accountCard().build(
      account(session, chart::kVatCredit), QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE(credit);
  REQUIRE(credit.value().turnoverDebit == Money::fromCents(400));
  REQUIRE(session.verify());
}

TEST_CASE("cannot post a second tax invoice from the same source")
{
  LedgerSession session = openSession();
  enableVat(session);
  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);
  const CounterpartyId party = customer(session, QStringLiteral("Дубль"));
  const NomenclatureId flour = item(session, QStringLiteral("Товар-дубль"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 10, 1), QStringLiteral("НП-dup"));
  purchase.counterpartyId = party;
  purchase.lines = composePurchase(accounts.value(), {goods(flour, 1000, 10000, 10000)}).value();
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 10, 2), QStringLiteral("РТ-dup"));
  sale.counterpartyId = party;
  sale.lines = composeSale(accounts.value(), {goods(flour, 1000, 10000, 10000)}).value();
  const DocumentId saleId = saveAndPost(session, sale);
  const Result<Document> postedSale = session.documents().findById(saleId);
  REQUIRE(postedSale);

  Result<Document> pn = composeTaxInvoice(session.handle(), accounts.value(), postedSale.value(),
                                          company.value(), QDate(2026, 10, 2));
  REQUIRE(pn);
  pn.value().number = QStringLiteral("ПН-dup1");
  saveAndPost(session, pn.value());

  const Result<Document> again = composeTaxInvoice(
      session.handle(), accounts.value(), postedSale.value(), company.value(), QDate(2026, 10, 2));
  REQUIRE_FALSE(again);
  REQUIRE_FALSE(session.posting().unpost(saleId));
}

TEST_CASE("leftover 643 does not block month close; register next month")
{
  LedgerSession session = openSession();
  enableVat(session);
  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);
  const CounterpartyId party = customer(session, QStringLiteral("Квітень"));
  const NomenclatureId flour = item(session, QStringLiteral("Борошно-квіт"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document purchase =
      makeDoc(DocumentType::Purchase, QDate(2026, 4, 1), QStringLiteral("НП-close"));
  purchase.counterpartyId = party;
  purchase.lines = composePurchase(accounts.value(), {goods(flour, 1000, 10000, 10000)}).value();
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 4, 2), QStringLiteral("РТ-close"));
  sale.counterpartyId = party;
  sale.lines = composeSale(accounts.value(), {goods(flour, 1000, 10000, 10000)}).value();
  const DocumentId saleId = saveAndPost(session, sale);
  const Result<Document> postedSale = session.documents().findById(saleId);
  REQUIRE(postedSale);

  Result<Document> pn = composeTaxInvoice(session.handle(), accounts.value(), postedSale.value(),
                                          company.value(), QDate(2026, 4, 2));
  REQUIRE(pn);
  pn.value().number = QStringLiteral("ПН-close");
  const DocumentId pnId = saveAndPost(session, pn.value());

  Result<Document> result = buildPeriodResultDocument(session.handle(), 2026, 4);
  REQUIRE(result);
  const Result<DocumentId> resultId = session.documents().insert(result.value());
  REQUIRE(resultId);
  REQUIRE(session.posting().post(resultId.value()));
  REQUIRE(session.period().closeThroughMonth(2026, 4));

  REQUIRE_FALSE(session.posting().registerTaxInvoice(pnId, QDate(2026, 4, 20)));
  REQUIRE(session.posting().registerTaxInvoice(pnId, QDate(2026, 5, 6)));
  const Result<std::vector<LedgerEntry>> ledger = session.ledger().listForDocument(pnId);
  REQUIRE(ledger);
  REQUIRE(ledger.value().size() == 2);
  REQUIRE(ledger.value().back().date == QDate(2026, 5, 6));
  REQUIRE(ledger.value().back().debitAccountId == account(session, chart::kVatLiability));
  REQUIRE(ledger.value().back().creditAccountId == account(session, chart::kVatBudget));
  REQUIRE(session.verify());
}

TEST_CASE("supplier VAT stamping refuses an empty company VAT rate")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  std::vector<DocumentLine> lines = {supplierLine(session, 12000)};
  // Платник ПДВ без ставки в картці — помилка, а не 0% у документі
  REQUIRE_FALSE(stampSupplierVat(lines, accounts.value(), 0));
  REQUIRE(lines[0].vatCents == 0);

  REQUIRE(stampSupplierVat(lines, accounts.value(), 2000));
  REQUIRE(lines[0].vatRateBp == 2000);
  REQUIRE(lines[0].vatCents == 2400);
}
