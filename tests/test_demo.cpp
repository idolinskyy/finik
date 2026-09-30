#include "support/Session.h"

#include "fin/db/UserRepository.h"
#include "fin/demo/DemoSeed.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/print/PrintForms.h"

#include <QDate>

using namespace fin;
using namespace fin::test;

TEST_CASE("demo seed fills an empty session and refuses a second run")
{
  LedgerSession session = openSession();
  const Status seedSt = seedDemoData(session, 42);
  INFO(seedSt.message().toStdString());
  REQUIRE(seedSt);
  const Result<int> count = session.documents().count();
  REQUIRE(count);
  REQUIRE(count.value() >= 10);
  REQUIRE(session.verify());

  const int year = QDate::currentDate().year();
  const QDate from(year, 1, 1);
  const QDate to(year, 12, 31);
  const Result<TrialBalance> osv = session.trialBalance().build(from, to);
  REQUIRE(osv);
  REQUIRE(osv.value().totals.closingDebit == osv.value().totals.closingCredit);

  const Result<std::vector<DocumentHeader>> sales =
      session.documents().list(from, to, DocumentType::Sale);
  REQUIRE(sales);
  REQUIRE(sales.value().size() >= 4);

  const AccountId cash = account(session, chart::kCash);
  const Result<CashBook> cashBook = session.cashBook().build(from, to);
  REQUIRE(cashBook);
  const Result<AccountCard> cashCard = session.accountCard().build(cash, from, to);
  REQUIRE(cashCard);
  const Result<Money> cashNet = cashCard.value().closingDebit.minus(cashCard.value().closingCredit);
  REQUIRE(cashNet);
  REQUIRE(cashBook.value().closing == cashNet.value());

  const AccountId bank = account(session, chart::kBank);
  const Result<CashBook> bankBook = session.cashBook().buildFor(chart::kBank, from, to);
  REQUIRE(bankBook);
  const Result<AccountCard> bankCard = session.accountCard().build(bank, from, to);
  REQUIRE(bankCard);
  const Result<Money> bankNet = bankCard.value().closingDebit.minus(bankCard.value().closingCredit);
  REQUIRE(bankNet);
  REQUIRE(bankBook.value().closing == bankNet.value());

  const Result<StockReportData> stock = session.stock().build(from, to);
  REQUIRE(stock);
  PrintForms forms(session.handle());
  const Result<QString> osvCsv = forms.trialBalanceCsv(osv.value());
  REQUIRE(osvCsv);
  REQUIRE(osvCsv.value().contains(QStringLiteral("301")));
  const Result<QString> stockCsv = forms.stockCsv(stock.value());
  REQUIRE(stockCsv);
  REQUIRE(stockCsv.value().contains(QStringLiteral("Борошно")));
  const Result<QString> bankCsv = forms.cashBookCsv(bankBook.value());
  REQUIRE(bankCsv);

  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);
  REQUIRE(company.value().iban.startsWith(QStringLiteral("UA")));
  REQUIRE(company.value().taxSystem == TaxSystem::Simplified);
  REQUIRE(company.value().singleTaxRateBp == 500);

  const Result<std::vector<DocumentHeader>> services =
      session.documents().list(from, to, DocumentType::ServiceSale);
  REQUIRE(services);
  REQUIRE(services.value().size() >= 2);
  const Result<std::vector<DocumentHeader>> servicePurchases =
      session.documents().list(from, to, DocumentType::ServicePurchase);
  REQUIRE(servicePurchases);
  REQUIRE(servicePurchases.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> invoices =
      session.documents().list(from, to, DocumentType::CustomerInvoice);
  REQUIRE(invoices);
  REQUIRE(invoices.value().size() >= 1);
  REQUIRE_FALSE(invoices.value().front().posted);
  const Result<std::vector<DocumentHeader>> saleReturns =
      session.documents().list(from, to, DocumentType::SaleReturn);
  REQUIRE(saleReturns);
  REQUIRE(saleReturns.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> purchaseReturns =
      session.documents().list(from, to, DocumentType::PurchaseReturn);
  REQUIRE(purchaseReturns);
  REQUIRE(purchaseReturns.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> writeOffs =
      session.documents().list(from, to, DocumentType::WriteOff);
  REQUIRE(writeOffs);
  REQUIRE(writeOffs.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> stocktakes =
      session.documents().list(from, to, DocumentType::Stocktake);
  REQUIRE(stocktakes);
  REQUIRE(stocktakes.value().size() >= 1);

  const Result<std::vector<DocumentHeader>> commissions =
      session.documents().list(from, to, DocumentType::FaCommission);
  REQUIRE(commissions);
  REQUIRE(commissions.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> amorts =
      session.documents().list(from, to, DocumentType::FaDepreciation);
  REQUIRE(amorts);
  REQUIRE(amorts.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> writeOffsFa =
      session.documents().list(from, to, DocumentType::FaWriteOff);
  REQUIRE(writeOffsFa);
  REQUIRE(writeOffsFa.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> msReceipts =
      session.documents().list(from, to, DocumentType::MsReceipt);
  REQUIRE(msReceipts);
  REQUIRE(msReceipts.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> msIssues =
      session.documents().list(from, to, DocumentType::MsIssue);
  REQUIRE(msIssues);
  REQUIRE(msIssues.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> iaCommissions =
      session.documents().list(from, to, DocumentType::IaCommission);
  REQUIRE(iaCommissions);
  REQUIRE(iaCommissions.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> iaAmorts =
      session.documents().list(from, to, DocumentType::IaDepreciation);
  REQUIRE(iaAmorts);
  REQUIRE(iaAmorts.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> iaWriteOffs =
      session.documents().list(from, to, DocumentType::IaWriteOff);
  REQUIRE(iaWriteOffs);
  REQUIRE(iaWriteOffs.value().size() >= 1);
  REQUIRE(osvCsv.value().contains(QStringLiteral("104")));
  REQUIRE(osvCsv.value().contains(QStringLiteral("131")));
  REQUIRE(osvCsv.value().contains(QStringLiteral("125")));
  REQUIRE(osvCsv.value().contains(QStringLiteral("133")));
  const Result<std::vector<Employee>> staff = session.employees().listActive();
  REQUIRE(staff);
  REQUIRE(staff.value().size() >= 3);
  const Result<std::vector<DocumentHeader>> payrolls =
      session.documents().list(from, to, DocumentType::Payroll);
  REQUIRE(payrolls);
  REQUIRE(payrolls.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> taxInvoices =
      session.documents().list(from, to, DocumentType::TaxInvoice);
  REQUIRE(taxInvoices);
  REQUIRE(taxInvoices.value().size() >= 1);
  REQUIRE(osvCsv.value().contains(QStringLiteral("661")));
  REQUIRE(osvCsv.value().contains(QStringLiteral("641")));

  const Result<std::vector<User>> demoUsers = UserRepository(session.handle()).listAll();
  REQUIRE(demoUsers);
  REQUIRE(demoUsers.value().size() >= 3);

  REQUIRE_FALSE(seedDemoData(session, 42));
}

TEST_CASE("demo seed supplements an older demo company")
{
  LedgerSession session = openSession();
  CompanyProfile profile;
  profile.name = QStringLiteral("ТОВ «Демо-облік»");
  REQUIRE(session.company().save(profile));

  Counterparty buyer;
  buyer.name = QStringLiteral("ТОВ Покупець");
  buyer.edrpou = QStringLiteral("11111111");
  buyer.isCustomer = true;
  REQUIRE(session.counterparties().insert(buyer));
  Counterparty mill;
  mill.name = QStringLiteral("ТОВ ПостачЗерно");
  mill.edrpou = QStringLiteral("33333333");
  mill.isSupplier = true;
  REQUIRE(session.counterparties().insert(mill));

  const NomenclatureId flour = item(session, QStringLiteral("Борошно-old"));
  const Result<Nomenclature> flourRow = session.nomenclature().findById(flour);
  REQUIRE(flourRow);
  Nomenclature named = flourRow.value();
  named.sku = QStringLiteral("FL-01");
  REQUIRE(session.nomenclature().update(named));
  const NomenclatureId sugar = item(session, QStringLiteral("Цукор-old"));
  const Result<Nomenclature> sugarRow = session.nomenclature().findById(sugar);
  REQUIRE(sugarRow);
  Nomenclature sugarNamed = sugarRow.value();
  sugarNamed.sku = QStringLiteral("SG-02");
  REQUIRE(session.nomenclature().update(sugarNamed));
  const NomenclatureId coffee = item(session, QStringLiteral("Кава-old"));
  const Result<Nomenclature> coffeeRow = session.nomenclature().findById(coffee);
  REQUIRE(coffeeRow);
  Nomenclature coffeeNamed = coffeeRow.value();
  coffeeNamed.sku = QStringLiteral("CF-04");
  REQUIRE(session.nomenclature().update(coffeeNamed));

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  GoodsLine buyFlour;
  buyFlour.nomenclatureId = flour;
  buyFlour.quantity = Quantity::fromMilli(10000);
  buyFlour.unitPrice = Money::fromCents(4000);
  buyFlour.amount = Money::fromCents(40000);
  Document purchase = makeDoc(DocumentType::Purchase, QDate(QDate::currentDate().year(), 1, 12),
                              QStringLiteral("НП-old"));
  const Result<Counterparty> millSaved =
      session.counterparties().findByEdrpou(QStringLiteral("33333333"));
  REQUIRE(millSaved);
  purchase.counterpartyId = millSaved.value().id;
  const Result<std::vector<DocumentLine>> buyLines = composePurchase(accounts.value(), {buyFlour});
  REQUIRE(buyLines);
  purchase.lines = buyLines.value();
  saveAndPost(session, purchase);

  GoodsLine buySugar = buyFlour;
  buySugar.nomenclatureId = sugar;
  Document sugarDoc = purchase;
  sugarDoc.number = QStringLiteral("НП-old-s");
  sugarDoc.date = QDate(QDate::currentDate().year(), 3, 18);
  const Result<std::vector<DocumentLine>> sugarLines =
      composePurchase(accounts.value(), {buySugar});
  REQUIRE(sugarLines);
  sugarDoc.lines = sugarLines.value();
  sugarDoc.id = {};
  saveAndPost(session, sugarDoc);

  GoodsLine buyCoffee = buyFlour;
  buyCoffee.nomenclatureId = coffee;
  Document coffeeDoc = purchase;
  coffeeDoc.number = QStringLiteral("НП-old-c");
  coffeeDoc.date = QDate(QDate::currentDate().year(), 4, 1);
  const Result<std::vector<DocumentLine>> coffeeLines =
      composePurchase(accounts.value(), {buyCoffee});
  REQUIRE(coffeeLines);
  coffeeDoc.lines = coffeeLines.value();
  coffeeDoc.id = {};
  saveAndPost(session, coffeeDoc);

  REQUIRE(seedDemoData(session, 42));
  const int year = QDate::currentDate().year();
  const Result<std::vector<DocumentHeader>> services =
      session.documents().list(QDate(year, 1, 1), QDate(year, 12, 31), DocumentType::ServiceSale);
  REQUIRE(services);
  REQUIRE(services.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> servicePurchases = session.documents().list(
      QDate(year, 1, 1), QDate(year, 12, 31), DocumentType::ServicePurchase);
  REQUIRE(servicePurchases);
  REQUIRE(servicePurchases.value().size() >= 1);
  const Result<std::vector<DocumentHeader>> commissions =
      session.documents().list(QDate(year, 1, 1), QDate(year, 12, 31), DocumentType::FaCommission);
  REQUIRE(commissions);
  REQUIRE(commissions.value().size() >= 1);
  const Result<CompanyProfile> saved = session.company().load();
  REQUIRE(saved);
  REQUIRE(saved.value().iban.startsWith(QStringLiteral("UA")));
  REQUIRE_FALSE(seedDemoData(session, 42));
}
