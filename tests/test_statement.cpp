#include "support/Session.h"

#include "fin/bank/PaymentExport.h"
#include "fin/bank/StatementImport.h"
#include "fin/domain/Contract.h"
#include "fin/domain/DocumentBlueprint.h"

using namespace fin;
using namespace fin::test;

TEST_CASE("bank statement csv imports matched counterparties and skips the rest")
{
  LedgerSession session = openSession();
  Counterparty mill;
  mill.name = QStringLiteral("ТОВ Млин-CSV");
  mill.edrpou = QStringLiteral("12345678");
  mill.isCustomer = true;
  mill.isSupplier = true;
  const Result<CounterpartyId> millId = session.counterparties().insert(mill);
  REQUIRE(millId);

  const QString csv = QStringLiteral("date;amount;direction;purpose;edrpou;name\n"
                                     "2026-09-10;1500,00;in;оплата;12345678;ТОВ Млин-CSV\n"
                                     "2026-09-11;200,00;out;передоплата;99999999;Немає\n"
                                     "2026-09-13;50,00;in;оплата;11111111;\n"
                                     "2026-09-10;1500,00;in;оплата;12345678;ТОВ Млин-CSV\n"
                                     "10.09.2026;300,00;out;без коду;;\n");
  Result<StatementParse> parsed = parseBankStatement(csv, QStringLiteral("stmt.csv"));
  REQUIRE(parsed);
  REQUIRE(parsed.value().rows.size() == 5);
  const Result<int> created = importBankStatement(session, parsed.value());
  REQUIRE(created);
  REQUIRE(created.value() == 3);
  const Result<Counterparty> createdParty =
      session.counterparties().findByEdrpou(QStringLiteral("99999999"));
  REQUIRE(createdParty);
  REQUIRE(createdParty.value().name == QStringLiteral("Немає"));
  const Result<Counterparty> unnamed =
      session.counterparties().findByEdrpou(QStringLiteral("11111111"));
  REQUIRE(unnamed);
  REQUIRE(unnamed.value().name == QStringLiteral("ЄДРПОУ 11111111"));

  const Result<std::vector<DocumentHeader>> list =
      session.documents().list(QDate(2026, 9, 1), QDate(2026, 9, 30), DocumentType::BankPayment);
  REQUIRE(list);
  REQUIRE(list.value().size() == 3);
  REQUIRE(list.value().front().posted == false);

  const Result<int> again = importBankStatement(session, parsed.value());
  REQUIRE(again);
  REQUIRE(again.value() == 0);
}

TEST_CASE("bank statement xml imports outgoing payment")
{
  LedgerSession session = openSession();
  Counterparty mill;
  mill.name = QStringLiteral("ТОВ Млин-XML");
  mill.edrpou = QStringLiteral("87654321");
  mill.isSupplier = true;
  const Result<CounterpartyId> millId = session.counterparties().insert(mill);
  REQUIRE(millId);

  const QString xml =
      QStringLiteral("<statement><row date=\"2026-09-12\" amount=\"80.50\" incoming=\"0\" "
                     "purpose=\"оплата рахунку\" edrpou=\"87654321\" name=\"ТОВ Млин-XML\"/>"
                     "</statement>");
  Result<StatementParse> parsed = parseBankStatement(xml, QStringLiteral("stmt.xml"));
  REQUIRE(parsed);
  const Result<int> created = importBankStatement(session, parsed.value());
  REQUIRE(created);
  REQUIRE(created.value() == 1);
  const Result<std::vector<DocumentHeader>> list =
      session.documents().list(QDate(2026, 9, 12), QDate(2026, 9, 12), DocumentType::BankPayment);
  REQUIRE(list);
  REQUIRE(list.value().size() == 1);
  const Result<Document> document = session.documents().findById(list.value().front().id);
  REQUIRE(document);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  REQUIRE_FALSE(bankDocumentIncoming(document.value().lines, accounts.value()));
  REQUIRE(document.value().lines.front().amount.cents() == 8050);
}

TEST_CASE("bank statement imports contract from column and purpose")
{
  LedgerSession session = openSession();
  Counterparty mill;
  mill.name = QStringLiteral("ТОВ Млин-договір");
  mill.edrpou = QStringLiteral("11223355");
  mill.isCustomer = true;
  mill.isSupplier = true;
  const Result<CounterpartyId> millId = session.counterparties().insert(mill);
  REQUIRE(millId);

  Contract contract;
  contract.counterpartyId = millId.value();
  contract.number = QStringLiteral("12-П/2026");
  contract.name = QStringLiteral("Поставка");
  contract.date = QDate(2026, 1, 1);
  const Result<ContractId> cid = session.contracts().insert(contract);
  REQUIRE(cid);

  const QString csv =
      QStringLiteral("date;amount;direction;purpose;edrpou;name;contract\n"
                     "2026-09-10;1500,00;in;оплата;11223355;ТОВ Млин-договір;12-П/2026\n");
  Result<StatementParse> parsed = parseBankStatement(csv, QStringLiteral("stmt.csv"));
  REQUIRE(parsed);
  REQUIRE(parsed.value().rows.front().contractNumber == QStringLiteral("12-П/2026"));
  const Result<int> created = importBankStatement(session, parsed.value());
  REQUIRE(created);
  REQUIRE(created.value() == 1);
  REQUIRE(parsed.value().rows.front().contractId == cid.value());

  const Result<std::vector<DocumentHeader>> list =
      session.documents().list(QDate(2026, 9, 10), QDate(2026, 9, 10), DocumentType::BankPayment);
  REQUIRE(list);
  REQUIRE(list.value().size() == 1);
  const Result<Document> document = session.documents().findById(list.value().front().id);
  REQUIRE(document);
  REQUIRE(document.value().contractId == cid.value());
}

TEST_CASE("bank statement picks contract number from payment purpose")
{
  LedgerSession session = openSession();
  Counterparty mill;
  mill.name = QStringLiteral("ТОВ Призначення");
  mill.edrpou = QStringLiteral("55667788");
  mill.isCustomer = true;
  const Result<CounterpartyId> millId = session.counterparties().insert(mill);
  REQUIRE(millId);

  Contract contract;
  contract.counterpartyId = millId.value();
  contract.number = QStringLiteral("12-П/2026");
  contract.name = QStringLiteral("Поставка");
  contract.date = QDate(2026, 2, 1);
  const Result<ContractId> cid = session.contracts().insert(contract);
  REQUIRE(cid);

  const QString xml = QStringLiteral(
      "<statement><row date=\"2026-09-18\" amount=\"90.00\" incoming=\"1\" "
      "purpose=\"оплата за договором № 12-П/2026 від 01.02.2026\" "
      "edrpou=\"55667788\" name=\"ТОВ Призначення\"/></statement>");
  Result<StatementParse> parsed = parseBankStatement(xml, QStringLiteral("stmt.xml"));
  REQUIRE(parsed);
  REQUIRE(parsed.value().rows.front().contractNumber == QStringLiteral("12-П/2026"));
  const Result<int> created = importBankStatement(session, parsed.value());
  REQUIRE(created);
  REQUIRE(created.value() == 1);
  const Result<std::vector<DocumentHeader>> list =
      session.documents().list(QDate(2026, 9, 18), QDate(2026, 9, 18), DocumentType::BankPayment);
  REQUIRE(list);
  const Result<Document> document = session.documents().findById(list.value().front().id);
  REQUIRE(document);
  REQUIRE(document.value().contractId == cid.value());
}

TEST_CASE("bank statement leaves contract empty when number is unknown")
{
  LedgerSession session = openSession();
  Counterparty mill;
  mill.name = QStringLiteral("ТОВ Без договору");
  mill.edrpou = QStringLiteral("99887766");
  mill.isCustomer = true;
  REQUIRE(session.counterparties().insert(mill));

  const QString csv =
      QStringLiteral("date;amount;direction;purpose;edrpou;name;contract\n"
                     "2026-09-19;10,00;in;оплата;99887766;ТОВ Без договору;НЕМАЄ-1\n");
  Result<StatementParse> parsed = parseBankStatement(csv, QStringLiteral("stmt.csv"));
  REQUIRE(parsed);
  const Result<int> created = importBankStatement(session, parsed.value());
  REQUIRE(created);
  REQUIRE(created.value() == 1);
  REQUIRE_FALSE(parsed.value().rows.front().contractId);
  const Result<std::vector<DocumentHeader>> list =
      session.documents().list(QDate(2026, 9, 19), QDate(2026, 9, 19), DocumentType::BankPayment);
  REQUIRE(list);
  const Result<Document> document = session.documents().findById(list.value().front().id);
  REQUIRE(document);
  REQUIRE_FALSE(document.value().contractId);
}

TEST_CASE("outgoing bank payments export to client-bank csv")
{
  LedgerSession session = openSession();
  CompanyProfile company = session.company().load().value();
  company.edrpou = QStringLiteral("12345678");
  company.iban = QStringLiteral("UA953220010000000000000001234");
  company.mfo = QStringLiteral("322001");
  REQUIRE(session.company().save(company));

  Counterparty withIban;
  withIban.name = QStringLiteral("ТОВ Постачальник");
  withIban.edrpou = QStringLiteral("87654321");
  withIban.iban = QStringLiteral("UA783003350000000000000000001");
  withIban.isSupplier = true;
  const Result<CounterpartyId> payeeId = session.counterparties().insert(withIban);
  REQUIRE(payeeId);

  Counterparty noIban;
  noIban.name = QStringLiteral("Без IBAN");
  noIban.edrpou = QStringLiteral("11223344");
  noIban.isSupplier = true;
  const Result<CounterpartyId> skipId = session.counterparties().insert(noIban);
  REQUIRE(skipId);

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document outOk =
      makeDoc(DocumentType::BankPayment, QDate(2026, 9, 15), QStringLiteral("РР-out-1"));
  outOk.counterpartyId = payeeId.value();
  outOk.comment = QStringLiteral("оплата рахунку 42");
  BankLine pay;
  pay.amount = Money::fromCents(250050);
  pay.description = QStringLiteral("оплата");
  Result<std::vector<DocumentLine>> outLines = composeBank(accounts.value(), false, {pay});
  REQUIRE(outLines);
  outOk.lines = outLines.value();
  REQUIRE(session.documents().insert(outOk));

  Document outSkip =
      makeDoc(DocumentType::BankPayment, QDate(2026, 9, 16), QStringLiteral("РР-skip"));
  outSkip.counterpartyId = skipId.value();
  BankLine paySkip;
  paySkip.amount = Money::fromCents(10000);
  paySkip.description = QStringLiteral("без iban");
  Result<std::vector<DocumentLine>> skipLines = composeBank(accounts.value(), false, {paySkip});
  REQUIRE(skipLines);
  outSkip.lines = skipLines.value();
  REQUIRE(session.documents().insert(outSkip));

  Document incoming =
      makeDoc(DocumentType::BankPayment, QDate(2026, 9, 17), QStringLiteral("РР-in"));
  incoming.counterpartyId = payeeId.value();
  BankLine inRow;
  inRow.amount = Money::fromCents(50000);
  inRow.description = QStringLiteral("надходження");
  Result<std::vector<DocumentLine>> inLines = composeBank(accounts.value(), true, {inRow});
  REQUIRE(inLines);
  incoming.lines = inLines.value();
  REQUIRE(session.documents().insert(incoming));

  Result<PaymentExportResult> exported =
      exportOutgoingPaymentsCsv(session, QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE(exported);
  REQUIRE(exported.value().exported == 1);
  REQUIRE(exported.value().skipped.size() == 1);
  REQUIRE(exported.value().csv.contains(QStringLiteral("DB_IBAN;CR_IBAN")));
  REQUIRE(exported.value().csv.contains(QStringLiteral("РР-out-1")));
  REQUIRE(exported.value().csv.contains(QStringLiteral("2500.50")));
  REQUIRE(exported.value().csv.contains(QStringLiteral("UA953220010000000000000001234")));
  REQUIRE(exported.value().csv.contains(QStringLiteral("UA783003350000000000000000001")));
  REQUIRE(exported.value().csv.contains(QStringLiteral("оплата рахунку 42")));
  REQUIRE_FALSE(exported.value().csv.contains(QStringLiteral("РР-in")));
  REQUIRE_FALSE(exported.value().csv.contains(QStringLiteral("РР-skip")));
}

TEST_CASE("payment export requires company iban")
{
  LedgerSession session = openSession();
  CompanyProfile company = session.company().load().value();
  company.iban.clear();
  company.mfo.clear();
  REQUIRE(session.company().save(company));
  Result<PaymentExportResult> exported =
      exportOutgoingPaymentsCsv(session, QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE_FALSE(exported);
  REQUIRE(exported.message().contains(QStringLiteral("IBAN")));
}
