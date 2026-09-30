#include "support/Session.h"

#include "fin/auth/Access.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"
#include "fin/domain/Vat.h"
#include "fin/edo/TaxInvoiceXml.h"
#include "fin/edo/DeclarationXml.h"
#include "fin/edo/PayrollTaxDigest.h"

using namespace fin;
using namespace fin::test;

namespace {

void enableVat(LedgerSession & session, const QString & edrpou)
{
  Result<CompanyProfile> loaded = session.company().load();
  REQUIRE(loaded);
  CompanyProfile profile = loaded.value();
  profile.vatPayer = true;
  profile.vatRateBp = 2000;
  profile.edrpou = edrpou;
  REQUIRE(session.company().save(profile));
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

CounterpartyId partyWithTin(LedgerSession & session, const QString & name, const QString & edrpou)
{
  Counterparty row;
  row.name = name;
  row.edrpou = edrpou;
  row.isCustomer = true;
  row.isSupplier = true;
  Result<CounterpartyId> id = session.counterparties().insert(row);
  REQUIRE(id);
  return id.value();
}

Document postedSale(LedgerSession & session, CounterpartyId party)
{
  const NomenclatureId flour = item(session, QStringLiteral("Борошно-edo"));
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 6, 1), QStringLiteral("НП-edo"));
  purchase.counterpartyId = party;
  purchase.lines = composePurchase(accounts.value(), {goods(flour, 1000, 10000, 10000)}).value();
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 6, 2), QStringLiteral("РТ-edo"));
  sale.counterpartyId = party;
  sale.lines = composeSale(accounts.value(), {goods(flour, 1000, 15000, 15000)}).value();
  const DocumentId saleId = saveAndPost(session, sale);
  Result<Document> posted = session.documents().findById(saleId);
  REQUIRE(posted);
  return posted.value();
}

} // namespace

TEST_CASE("tax invoice xml roundtrip is deterministic utf-8 sha256")
{
  const QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                     "<TaxInvoice xmlns=\"urn:finik:edo:j12\" version=\"1\">"
                                     "<Number>ПН-1</Number><Date>2026-06-02</Date>"
                                     "<Seller><Name>Продавець</Name><Tin>11111111</Tin></Seller>"
                                     "<Buyer><Name>Покупець</Name><Tin>22222222</Tin></Buyer>"
                                     "<VatCents>3000</VatCents><VatRateBp>2000</VatRateBp>"
                                     "</TaxInvoice>");
  const Result<ParsedTaxInvoiceXml> parsed = parseTaxInvoiceXml(xml);
  REQUIRE(parsed);
  REQUIRE(parsed.value().number == QStringLiteral("ПН-1"));
  REQUIRE(parsed.value().date == QDate(2026, 6, 2));
  REQUIRE(parsed.value().sellerTin == QStringLiteral("11111111"));
  REQUIRE(parsed.value().buyerTin == QStringLiteral("22222222"));
  REQUIRE(parsed.value().vat == Money::fromCents(3000));
  const QString hash = sha256HexUtf8(xml);
  REQUIRE(hash.size() == 64);
  REQUIRE(hash == sha256HexUtf8(xml));
}

TEST_CASE("export attaches xml to outgoing tax invoice; import attaches by number")
{
  LedgerSession session = openSession();
  enableVat(session, QStringLiteral("11111111"));
  const CounterpartyId party =
      partyWithTin(session, QStringLiteral("Покупець XML"), QStringLiteral("22222222"));
  const Document sale = postedSale(session, party);
  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Result<Document> pn = composeTaxInvoice(session.handle(), accounts.value(), sale, company.value(),
                                          QDate(2026, 6, 2));
  REQUIRE(pn);
  pn.value().number = QStringLiteral("ПН-XML1");
  const DocumentId pnId = saveAndPost(session, pn.value());

  const Result<QString> xml = exportTaxInvoiceXml(session.handle(), pnId);
  REQUIRE(xml);
  REQUIRE(xml.value().contains(QStringLiteral("11111111")));
  REQUIRE(xml.value().contains(QStringLiteral("22222222")));
  REQUIRE_FALSE(xml.value().contains(QStringLiteral("KEP")));

  Result<Document> stored = session.documents().findById(pnId);
  REQUIRE(stored);
  REQUIRE(stored.value().edoSha256 == sha256HexUtf8(xml.value()));
  REQUIRE_FALSE(stored.value().edoImported);

  const Result<EdoImportResult> again = importTaxInvoiceXml(session.handle(), xml.value());
  REQUIRE(again);
  REQUIRE(again.value().kind == EdoImportKind::AttachedOutgoing);
  REQUIRE(again.value().documentId == pnId);
  stored = session.documents().findById(pnId);
  REQUIRE(stored);
  REQUIRE(stored.value().edoImported);
  REQUIRE(stored.value().posted);
}

TEST_CASE("incoming xml creates 644/631 draft without based_on")
{
  LedgerSession session = openSession();
  enableVat(session, QStringLiteral("22222222"));
  const QString xml =
      QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<TaxInvoice xmlns=\"urn:finik:edo:j12\" version=\"1\">"
                     "<Number>PN-IN-1</Number><Date>2026-06-10</Date>"
                     "<Seller><Name>Постачальник XML</Name><Tin>33333333</Tin></Seller>"
                     "<Buyer><Name>Ми</Name><Tin>22222222</Tin></Buyer>"
                     "<VatCents>4000</VatCents><VatRateBp>2000</VatRateBp>"
                     "</TaxInvoice>");
  const Result<EdoImportResult> imported = importTaxInvoiceXml(session.handle(), xml);
  REQUIRE(imported);
  REQUIRE(imported.value().kind == EdoImportKind::CreatedIncoming);
  Result<Document> pn = session.documents().findById(imported.value().documentId);
  REQUIRE(pn);
  REQUIRE(pn.value().edoImported);
  REQUIRE_FALSE(pn.value().basedOnId);
  REQUIRE(pn.value().counterpartyId);
  REQUIRE(session.posting().post(pn.value().id));
  pn = session.documents().findById(pn.value().id);
  REQUIRE(pn);
  REQUIRE(pn.value().posted);
  REQUIRE(pn.value().lines.size() == 1);
  REQUIRE(pn.value().lines[0].debitAccountId == account(session, chart::kVatCredit));
  REQUIRE(pn.value().lines[0].creditAccountId == account(session, chart::kSuppliers));
  REQUIRE(pn.value().lines[0].amount == Money::fromCents(4000));
  REQUIRE(session.verify());
}

TEST_CASE("non-payer cannot export or import tax invoice xml")
{
  LedgerSession session = openSession();
  const Result<CompanyProfile> company = session.company().load();
  REQUIRE(company);
  REQUIRE_FALSE(company.value().vatPayer);
  const QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                     "<TaxInvoice xmlns=\"urn:finik:edo:j12\" version=\"1\">"
                                     "<Number>X</Number><Date>2026-06-10</Date>"
                                     "<Seller><Name>A</Name><Tin>11111111</Tin></Seller>"
                                     "<Buyer><Name>B</Name><Tin>22222222</Tin></Buyer>"
                                     "<VatCents>100</VatCents></TaxInvoice>");
  REQUIRE_FALSE(importTaxInvoiceXml(session.handle(), xml));
}

TEST_CASE("DeclarationXml builds VAT declaration XML")
{
  VatStatement report;
  report.from = QDate(2026, 9, 1);
  report.to = QDate(2026, 9, 30);
  report.liability.closingCredit = Money::fromCents(3000); // 30 UAH ПДВ = 150 база
  report.credit.closingDebit = Money::fromCents(1000);     // 10 UAH ПДВ = 50 база
  report.budget.closingCredit = Money::fromCents(2000);    // 20 UAH до сплати

  CompanyProfile company;
  company.edrpou = QStringLiteral("12345678");
  company.name = QStringLiteral("ТОВ Тест");
  company.address = QStringLiteral("Київ");
  company.accountant = QStringLiteral("Іванов І.І.");
  company.vatRateBp = 2000;
  company.taxOfficeRegion = 26;
  company.taxOfficeDistrict = 50;
  company.taxOfficeCode = 2650;

  Result<QString> xml = buildVatDeclarationXml(report, company);
  REQUIRE(xml);
  REQUIRE(xml.value().contains(QStringLiteral("J0200125.xsd")));
  REQUIRE(xml.value().contains(QStringLiteral("<TIN>12345678</TIN>")));
  REQUIRE(xml.value().contains(QStringLiteral("<C_REG>26</C_REG>")));
  REQUIRE(xml.value().contains(QStringLiteral("<C_STI_ORIG>2650</C_STI_ORIG>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R011G3>150.00</R011G3>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R011G4>30.00</R011G4>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R101G3>50.00</R101G3>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R101G4>10.00</R101G4>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R18G3>20.00</R18G3>")));

  company.vatRateBp = 700; // 7%
  Result<QString> xml7 = buildVatDeclarationXml(report, company);
  REQUIRE(xml7);
  // 30 UAH ПДВ при 7% → база ≈ 428.57
  REQUIRE(xml7.value().contains(QStringLiteral("<R011G3>428.57</R011G3>")));

  CompanyProfile noSti = company;
  noSti.taxOfficeCode = 0;
  REQUIRE_FALSE(buildVatDeclarationXml(report, noSti));

  // Без ставки ПДВ базу оподаткування не порахувати — 20% не підставляємо
  CompanyProfile noRate = company;
  noRate.vatRateBp = 0;
  REQUIRE_FALSE(buildVatDeclarationXml(report, noRate));
  noRate.vatRateBp = 12000;
  REQUIRE_FALSE(buildVatDeclarationXml(report, noRate));
}

TEST_CASE("every DECLAR export refuses a company card without tax office codes")
{
  CompanyProfile company;
  company.name = QStringLiteral("ТОВ Без ДПІ");
  company.edrpou = QStringLiteral("12345678");
  company.vatRateBp = 2000;
  REQUIRE(company.taxOfficeCode == 0);

  VatStatement vat;
  vat.from = QDate(2026, 9, 1);
  vat.to = QDate(2026, 9, 30);
  CHECK_FALSE(buildVatDeclarationXml(vat, company));

  BalanceSheet balance;
  IncomeStatement income;
  CHECK_FALSE(buildBalance1MXml(balance, income, company));

  ConsolidatedTaxReport payroll;
  payroll.year = 2026;
  payroll.quarter = 1;
  payroll.month = 3;
  CHECK_FALSE(buildConsolidatedTaxXml(payroll, company));
  CHECK_FALSE(buildAnnex4DfXml(payroll, company));
  CHECK_FALSE(buildAnnexEssD1Xml(payroll, company));
  CHECK_FALSE(buildAnnexD5Xml(payroll, company));

  IncomeBook book;
  book.from = QDate(2026, 4, 1);
  book.to = QDate(2026, 6, 30);
  book.taxSystem = TaxSystem::Simplified;
  book.rateBp = 500;
  CHECK_FALSE(buildSingleTaxDeclarationXml(book, company));

  // З повними кодами ДПІ ті самі виклики проходять
  company.taxOfficeRegion = 26;
  company.taxOfficeDistrict = 50;
  company.taxOfficeCode = 2650;
  CHECK(buildVatDeclarationXml(vat, company));
  CHECK(buildBalance1MXml(balance, income, company));
  CHECK(buildConsolidatedTaxXml(payroll, company));
  CHECK(buildSingleTaxDeclarationXml(book, company));
}

TEST_CASE("consolidated tax report from payroll and XML annex 4DF")
{
  LedgerSession session = openSession();
  CompanyProfile company = session.company().load().value();
  company.edrpou = QStringLiteral("87654321");
  company.name = QStringLiteral("ТОВ Зарплата");
  company.taxOfficeRegion = 26;
  company.taxOfficeDistrict = 50;
  company.taxOfficeCode = 2650;
  REQUIRE(session.company().save(company));

  Employee emp;
  emp.taxId = QStringLiteral("1122334455");
  emp.name = QStringLiteral("Петренко Петро Іванович");
  emp.hiredOn = QDate(2026, 1, 15);
  emp.salary = Money::fromCents(1000000);
  emp.isActive = true;
  Result<EmployeeId> eid = session.employees().insert(emp);
  REQUIRE(eid);
  emp.id = eid.value();

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Result<PayrollLine> row = payrollLineFor(emp, QDate(2026, 3, 31));
  REQUIRE(row);
  Document document = makeDoc(DocumentType::Payroll, QDate(2026, 3, 31), QStringLiteral("ЗП-Т"));
  Result<std::vector<DocumentLine>> lines = composePayroll(accounts.value(), {row.value()});
  REQUIRE(lines);
  document.lines = lines.value();
  DocumentId id = saveAndPost(session, document);
  REQUIRE(id);

  Result<ConsolidatedTaxReport> unpaid = buildConsolidatedTaxReport(session.handle(), 2026, 1);
  REQUIRE(unpaid);
  REQUIRE(unpaid.value().totalGross == Money::fromCents(1000000));
  REQUIRE(unpaid.value().totalPaidGross.isZero());

  Result<Document> postedPay = session.documents().findById(id);
  REQUIRE(postedPay);
  Result<Document> bank =
      buildBasedOn(postedPay.value(), DocumentType::BankPayment, accounts.value());
  REQUIRE(bank);
  bank.value().date = QDate(2026, 3, 31);
  bank.value().number = QStringLiteral("РР-ЗП-1");
  DocumentId bankId = saveAndPost(session, bank.value());
  REQUIRE(bankId);

  Result<ConsolidatedTaxReport> report = buildConsolidatedTaxReport(session.handle(), 2026, 1);
  REQUIRE(report);
  REQUIRE(report.value().employeeCount == 1);
  REQUIRE(report.value().employees[0].taxId == QStringLiteral("1122334455"));
  REQUIRE(report.value().employees[0].hiredOn == QDate(2026, 1, 15));
  REQUIRE(report.value().totalGross == Money::fromCents(1000000));
  REQUIRE(report.value().totalPaidGross == Money::fromCents(1000000));
  REQUIRE(report.value().totalPit.isPositive());
  REQUIRE(report.value().totalPaidPit == report.value().totalPit);
  REQUIRE(report.value().totalSsc.isPositive());

  Result<QString> xml = buildConsolidatedTaxXml(report.value(), company);
  REQUIRE(xml);
  REQUIRE(xml.value().contains(QStringLiteral("J0500111.xsd")));
  REQUIRE_FALSE(xml.value().contains(QStringLiteral("FINIK_EMPLOYEES")));
  REQUIRE(xml.value().contains(QStringLiteral("<R01G1>1</R01G1>")));
  REQUIRE(validateDeclarStructure(xml.value(), QStringLiteral("J0500111.xsd")));

  Result<QString> annex = buildAnnex4DfXml(report.value(), company);
  REQUIRE(annex);
  REQUIRE(annex.value().contains(QStringLiteral("J0510411.xsd")));
  REQUIRE(annex.value().contains(QStringLiteral("C_DOC_SUB")));
  REQUIRE(annex.value().contains(QStringLiteral("104")));
  REQUIRE(annex.value().contains(QStringLiteral("T1RXXXXG02")));
  REQUIRE(annex.value().contains(QStringLiteral("1122334455")));
  REQUIRE(annex.value().contains(QStringLiteral("T1RXXXXG05")));
  REQUIRE(annex.value().contains(QStringLiteral(">101<")));
  REQUIRE(annex.value().contains(QStringLiteral("15.01.2026")));
  REQUIRE(annex.value().contains(QStringLiteral("R01G03A")));
  REQUIRE(annex.value().contains(QStringLiteral("R00G01I")));
  REQUIRE(annex.value().contains(QStringLiteral("<C_DOC_VER>11</C_DOC_VER>")));

  Result<ConsolidatedTaxReport> monthly = buildMonthlyPayrollTaxReport(session.handle(), 2026, 3);
  REQUIRE(monthly);
  REQUIRE(monthly.value().month == 3);
  Result<QString> d1 = buildAnnexEssD1Xml(monthly.value(), company);
  REQUIRE(d1);
  REQUIRE(d1.value().contains(QStringLiteral("J0510111.xsd")));
  REQUIRE(d1.value().contains(QStringLiteral("T1RXXXXG7S")));
  REQUIRE(d1.value().contains(QStringLiteral("T1RXXXXG20")));
  REQUIRE(d1.value().contains(QStringLiteral("Петренко")));
  REQUIRE(validateDeclarStructure(d1.value(), QStringLiteral("J0510111.xsd")));

  Result<QString> d5 = buildAnnexD5Xml(report.value(), company);
  REQUIRE(d5);
  REQUIRE(d5.value().contains(QStringLiteral("J0510511.xsd")));
  REQUIRE(d5.value().contains(QStringLiteral("C_DOC_SUB")));
  REQUIRE(d5.value().contains(QStringLiteral("105")));
  REQUIRE(d5.value().contains(QStringLiteral("T1RXXXXG02S")));
  REQUIRE(d5.value().contains(QStringLiteral("1122334455")));
  REQUIRE(d5.value().contains(QStringLiteral("T1RXXXXG06D")));
  REQUIRE(d5.value().contains(QStringLiteral("15.01.2026")));
  REQUIRE(d5.value().contains(QStringLiteral("R00G01I")));
  REQUIRE(validateDeclarStructure(d5.value(), QStringLiteral("J0510511.xsd")));
}

TEST_CASE("single tax declaration XML from income book")
{
  LedgerSession session = openSession();
  CompanyProfile company = session.company().load().value();
  company.edrpou = QStringLiteral("1234567890");
  company.name = QStringLiteral("ФОП Тест");
  company.taxSystem = TaxSystem::Simplified;
  company.singleTaxRateBp = 500;
  company.taxOfficeRegion = 26;
  company.taxOfficeDistrict = 50;
  company.taxOfficeCode = 2650;
  REQUIRE(session.company().save(company));

  IncomeBook book;
  book.from = QDate(2026, 4, 1);
  book.to = QDate(2026, 6, 30);
  book.taxSystem = TaxSystem::Simplified;
  book.rateBp = 500;
  book.income = Money::fromCents(10000000); // 100000.00
  book.tax = Money::fromCents(500000);      // 5000.00

  const Money priorTax = Money::fromCents(200000); // 2000
  const Money priorMil = Money::fromCents(80000);  // 800
  Result<QString> xml = buildSingleTaxDeclarationXml(book, company, priorTax, priorMil);
  REQUIRE(xml);
  REQUIRE(xml.value().contains(QStringLiteral("F0103308.xsd")));
  REQUIRE(xml.value().contains(QStringLiteral("<C_DOC>F01</C_DOC>")));
  REQUIRE(xml.value().contains(QStringLiteral("<C_DOC_SUB>033</C_DOC_SUB>")));
  REQUIRE(xml.value().contains(QStringLiteral("<H1KV>2</H1KV>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R06G3>100000.00</R06G3>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R11G3>5000.00</R11G3>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R12G3>5000.00</R12G3>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R13G3>2000.00</R13G3>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R14G3>3000.00</R14G3>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R23G3>1000.00</R23G3>"))); // 1% of 100000
  REQUIRE(xml.value().contains(QStringLiteral("<R24G3>800.00</R24G3>")));
  REQUIRE(xml.value().contains(QStringLiteral("<R25G3>200.00</R25G3>")));
  REQUIRE(validateDeclarStructure(xml.value(), QStringLiteral("F0103308.xsd")));

  company.edrpou = QStringLiteral("12345678");
  book.rateBp = 300;
  book.tax = Money::fromCents(300000);
  Result<QString> legal = buildSingleTaxDeclarationXml(book, company);
  REQUIRE(legal);
  REQUIRE(legal.value().contains(QStringLiteral("J0103508.xsd")));
  REQUIRE(legal.value().contains(QStringLiteral("<R05G3>100000.00</R05G3>")));
  REQUIRE(legal.value().contains(QStringLiteral("<R09G3>3000.00</R09G3>")));
  REQUIRE_FALSE(legal.value().contains(QStringLiteral("R23G3")));
}
