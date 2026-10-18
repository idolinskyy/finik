#include "support/Session.h"

#include "fin/print/PrintForms.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/MonthClose.h"

using namespace fin;
using namespace fin::test;

namespace {

TrialBalanceRow rowByCode(const TrialBalance & report, const char * code)
{
  const QString needle = QString::fromLatin1(code);
  for (const TrialBalanceRow & row : report.rows) {
    if (row.code == needle) {
      return row;
    }
  }
  FAIL("account missing from trial balance: " + needle.toStdString());
  return {};
}

} // namespace

TEST_CASE("core circuit: opening, sale, cash receipt, OSV matches account card")
{
  LedgerSession session = openSession();
  const AccountId bank = account(session, chart::kBank);
  const AccountId cash = account(session, chart::kCash);
  const AccountId customers = account(session, chart::kCustomers);
  const AccountId revenue = account(session, chart::kRevenue);
  const AccountId aux = account(session, chart::kAuxiliary);
  const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Покупець"));

  Document opening =
      makeDoc(DocumentType::OpeningBalance, QDate(2026, 1, 1), QStringLiteral("ЗС-1"));
  opening.lines.push_back(line(bank, aux, 10000000, QStringLiteral("залишок р/р")));
  saveAndPost(session, opening);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 1, 15), QStringLiteral("РТ-1"));
  sale.counterpartyId = buyer;
  sale.lines.push_back(line(customers, revenue, 1000000));
  saveAndPost(session, sale);

  Document cashIn = makeDoc(DocumentType::CashIn, QDate(2026, 1, 16), QStringLiteral("ПКО-1"));
  cashIn.counterpartyId = buyer;
  cashIn.lines.push_back(line(cash, customers, 1000000));
  saveAndPost(session, cashIn);

  const QDate from(2026, 1, 1);
  const QDate to(2026, 1, 31);
  const Result<TrialBalance> osv = session.trialBalance().build(from, to);
  REQUIRE(osv);

  const TrialBalanceRow bankRow = rowByCode(osv.value(), chart::kBank);
  REQUIRE(bankRow.closingDebit.cents() == 10000000);
  REQUIRE(bankRow.closingCredit.isZero());

  const TrialBalanceRow cashRow = rowByCode(osv.value(), chart::kCash);
  REQUIRE(cashRow.closingDebit.cents() == 1000000);

  const TrialBalanceRow custRow = rowByCode(osv.value(), chart::kCustomers);
  REQUIRE(custRow.closingDebit.isZero());
  REQUIRE(custRow.closingCredit.isZero());

  const TrialBalanceRow revRow = rowByCode(osv.value(), chart::kRevenue);
  REQUIRE(revRow.closingCredit.cents() == 1000000);

  REQUIRE(osv.value().totals.openingDebit == osv.value().totals.openingCredit);
  REQUIRE(osv.value().totals.turnoverDebit == osv.value().totals.turnoverCredit);
  REQUIRE(osv.value().totals.closingDebit == osv.value().totals.closingCredit);

  const Result<AccountCard> card = session.accountCard().build(customers, from, to);
  REQUIRE(card);
  REQUIRE(card.value().openingDebit.isZero());
  REQUIRE(card.value().openingCredit.isZero());
  REQUIRE(card.value().closingDebit == custRow.closingDebit);
  REQUIRE(card.value().closingCredit == custRow.closingCredit);
  REQUIRE(card.value().turnoverDebit == custRow.turnoverDebit);
  REQUIRE(card.value().turnoverCredit == custRow.turnoverCredit);
}

TEST_CASE("purchase, sale with COGS and bank payment")
{
  LedgerSession session = openSession();
  const CounterpartyId supplier = customer(session, QStringLiteral("ТОВ Постачальник"));
  const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Клієнт"));

  const NomenclatureId flour = item(session, QStringLiteral("Борошно"));
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 2, 1), QStringLiteral("НП-1"));
  purchase.counterpartyId = supplier;
  purchase.lines.push_back(stockLine(account(session, chart::kGoods),
                                     account(session, chart::kSuppliers), flour, 10000, 40000));
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 2, 2), QStringLiteral("РТ-2"));
  sale.counterpartyId = buyer;
  sale.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 700000));
  sale.lines.push_back(stockLine(account(session, chart::kCogs), account(session, chart::kGoods),
                                 flour, 10000, 40000));
  saveAndPost(session, sale);

  Document payIn = makeDoc(DocumentType::BankPayment, QDate(2026, 2, 3), QStringLiteral("РР-in"));
  payIn.counterpartyId = buyer;
  payIn.lines.push_back(
      line(account(session, chart::kBank), account(session, chart::kCustomers), 700000));
  saveAndPost(session, payIn);

  Document payOut = makeDoc(DocumentType::BankPayment, QDate(2026, 2, 4), QStringLiteral("РР-out"));
  payOut.counterpartyId = supplier;
  payOut.lines.push_back(
      line(account(session, chart::kSuppliers), account(session, chart::kBank), 400000));
  saveAndPost(session, payOut);

  const Result<TrialBalance> osv =
      session.trialBalance().build(QDate(2026, 2, 1), QDate(2026, 2, 28));
  REQUIRE(osv);
  REQUIRE(rowByCode(osv.value(), chart::kGoods).closingDebit.isZero());
  REQUIRE(rowByCode(osv.value(), chart::kCustomers).closingDebit.isZero());
  REQUIRE(rowByCode(osv.value(), chart::kSuppliers).closingCredit.isZero());
  REQUIRE(rowByCode(osv.value(), chart::kBank).closingDebit.cents() == 300000);
  REQUIRE(rowByCode(osv.value(), chart::kRevenue).closingCredit.cents() == 700000);
  REQUIRE(rowByCode(osv.value(), chart::kCogs).closingDebit.cents() == 400000);
  REQUIRE(osv.value().totals.closingDebit == osv.value().totals.closingCredit);
}

TEST_CASE("unpost removes ledger rows from reports")
{
  LedgerSession session = openSession();
  Document document = makeDoc(DocumentType::Manual, QDate(2026, 3, 1), QStringLiteral("БО-u"));
  document.lines.push_back(
      line(account(session, chart::kAdminExpense), account(session, chart::kCash), 2500));
  const DocumentId id = saveAndPost(session, document);
  REQUIRE(session.posting().unpost(id));

  const Result<TrialBalance> osv =
      session.trialBalance().build(QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(osv);
  REQUIRE(rowByCode(osv.value(), chart::kCash).turnoverCredit.isZero());
}

TEST_CASE("cash book matches account 301 card for the same period")
{
  LedgerSession session = openSession();
  const AccountId cash = account(session, chart::kCash);
  const AccountId revenue = account(session, chart::kRevenue);
  const AccountId expense = account(session, chart::kAdminExpense);

  Document opening = makeDoc(DocumentType::CashIn, QDate(2025, 12, 20), QStringLiteral("ПКО-open"));
  opening.lines.push_back(line(cash, revenue, 50000));
  saveAndPost(session, opening);

  Document incoming = makeDoc(DocumentType::CashIn, QDate(2026, 1, 10), QStringLiteral("ПКО-jan"));
  incoming.lines.push_back(line(cash, revenue, 100000));
  saveAndPost(session, incoming);

  Document outgoing = makeDoc(DocumentType::CashOut, QDate(2026, 1, 20), QStringLiteral("ВКО-jan"));
  outgoing.lines.push_back(line(expense, cash, 30000));
  saveAndPost(session, outgoing);

  const QDate from(2026, 1, 1);
  const QDate to(2026, 1, 31);
  const Result<CashBook> book = session.cashBook().build(from, to);
  REQUIRE(book);
  REQUIRE(book.value().opening.cents() == 50000);
  REQUIRE(book.value().incoming.cents() == 100000);
  REQUIRE(book.value().outgoing.cents() == 30000);
  REQUIRE(book.value().closing.cents() == 120000);
  REQUIRE(book.value().rows.size() == 4);
  REQUIRE(book.value().rows[0].number == QStringLiteral("ПКО-jan"));
  REQUIRE(book.value().rows[1].dayTotal);
  REQUIRE(book.value().rows[2].number == QStringLiteral("ВКО-jan"));
  REQUIRE(book.value().rows[3].dayTotal);
  REQUIRE(book.value().rows[3].balance.cents() == 120000);

  const Result<AccountCard> card = session.accountCard().build(cash, from, to);
  REQUIRE(card);
  REQUIRE(card.value().openingDebit.cents() == 50000);
  REQUIRE(card.value().turnoverDebit.cents() == 100000);
  REQUIRE(card.value().turnoverCredit.cents() == 30000);
  REQUIRE(card.value().closingDebit.cents() == 120000);
  const Result<Money> net = card.value().closingDebit.minus(card.value().closingCredit);
  REQUIRE(net);
  REQUIRE(book.value().closing == net.value());

  PrintForms forms(session.handle());
  const Result<QString> html = forms.cashBookHtml(book.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("Касова книга")));
  REQUIRE(html.value().contains(QStringLiteral("ПКО-jan")));
  REQUIRE(html.value().contains(QStringLiteral("ВКО-jan")));
  REQUIRE(html.value().contains(QStringLiteral("content=\"landscape\"")));
  REQUIRE(html.value().contains(QStringLiteral("КО-4")));
  REQUIRE(html.value().contains(QStringLiteral("Разом за день")));
  REQUIRE(html.value().contains(QStringLiteral("width=\"100%\"")));

  const Result<CashJournal> journal = CashJournalReport(session.handle()).build(from, to);
  REQUIRE(journal);
  REQUIRE(journal.value().rows.size() == 2);
  PrintForms ko3(session.handle());
  const Result<QString> ko3Html = ko3.cashJournalHtml(journal.value());
  REQUIRE(ko3Html);
  REQUIRE(ko3Html.value().contains(QStringLiteral("КО-3")));
}

TEST_CASE("bank statement matches account 311 card")
{
  LedgerSession session = openSession();
  const AccountId bank = account(session, chart::kBank);
  const AccountId customers = account(session, chart::kCustomers);
  const AccountId suppliers = account(session, chart::kSuppliers);
  const CounterpartyId party = customer(session, QStringLiteral("Банк-тест"));

  Document opening =
      makeDoc(DocumentType::BankPayment, QDate(2025, 12, 20), QStringLiteral("РР-open"));
  opening.counterpartyId = party;
  opening.lines.push_back(line(bank, customers, 80000));
  saveAndPost(session, opening);

  Document incoming =
      makeDoc(DocumentType::BankPayment, QDate(2026, 1, 10), QStringLiteral("РР-in"));
  incoming.counterpartyId = party;
  incoming.lines.push_back(line(bank, customers, 20000));
  saveAndPost(session, incoming);

  Document outgoing =
      makeDoc(DocumentType::BankPayment, QDate(2026, 1, 20), QStringLiteral("РР-out"));
  outgoing.counterpartyId = party;
  outgoing.lines.push_back(line(suppliers, bank, 30000));
  saveAndPost(session, outgoing);

  const QDate from(2026, 1, 1);
  const QDate to(2026, 1, 31);
  const Result<CashBook> book = session.cashBook().buildFor(chart::kBank, from, to);
  REQUIRE(book);
  REQUIRE(book.value().opening.cents() == 80000);
  REQUIRE(book.value().incoming.cents() == 20000);
  REQUIRE(book.value().outgoing.cents() == 30000);
  REQUIRE(book.value().closing.cents() == 70000);

  const Result<AccountCard> card = session.accountCard().build(bank, from, to);
  REQUIRE(card);
  const Result<Money> net = card.value().closingDebit.minus(card.value().closingCredit);
  REQUIRE(net);
  REQUIRE(book.value().closing == net.value());
}

TEST_CASE("income statement uses period turnovers excluding financial result")
{
  LedgerSession session = openSession();
  Document cash = makeDoc(DocumentType::Manual, QDate(2026, 3, 2), QStringLiteral("БО-pl"));
  cash.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 80000));
  saveAndPost(session, cash);
  Document service = makeDoc(DocumentType::Manual, QDate(2026, 3, 3), QStringLiteral("БО-svc"));
  service.counterpartyId = customer(session, QStringLiteral("ТОВ Послуги"));
  service.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kServiceRevenue), 20000));
  saveAndPost(session, service);

  FinancialStatementReport statements(session.handle());
  const Result<IncomeStatement> open = statements.income(QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(open);
  REQUIRE(open.value().goodsRevenue.cents() == 80000);
  REQUIRE(open.value().serviceRevenue.cents() == 20000);
  REQUIRE(open.value().profit.cents() == 100000);

  const Result<BalanceSheet> openSheet = statements.balance(QDate(2026, 3, 31));
  REQUIRE(openSheet);
  bool sawOpenResult = false;
  for (const StatementLine & line : openSheet.value().equity) {
    if (line.code == QString::fromLatin1(chart::kFinancialResult)) {
      sawOpenResult = true;
      REQUIRE(line.amount.cents() == 100000);
      REQUIRE(line.name.contains(QStringLiteral("Незакритий")));
    }
  }
  REQUIRE(sawOpenResult);
  REQUIRE(openSheet.value().assetsTotal.cents() ==
          openSheet.value().equityTotal.cents() + openSheet.value().liabilitiesTotal.cents());

  Result<Document> result = buildPeriodResultDocument(session.handle(), 2026, 3);
  REQUIRE(result);
  saveAndPost(session, result.value());
  const Result<IncomeStatement> after = statements.income(QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(after);
  REQUIRE(after.value().goodsRevenue.cents() == 80000);
  REQUIRE(after.value().profit.cents() == 100000);

  const Result<BalanceSheet> sheet = statements.balance(QDate(2026, 3, 31));
  REQUIRE(sheet);
  REQUIRE(sheet.value().assetsTotal.cents() ==
          sheet.value().equityTotal.cents() + sheet.value().liabilitiesTotal.cents());
}

TEST_CASE("income book and single tax from 701 and 703")
{
  LedgerSession session = openSession();
  CompanyProfile profile;
  profile.name = QStringLiteral("ФОП Тест");
  profile.taxSystem = TaxSystem::Simplified;
  profile.singleTaxRateBp = 500;
  REQUIRE(session.company().save(profile));
  const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Покупець"));
  Document sale = makeDoc(DocumentType::Manual, QDate(2026, 4, 1), QStringLiteral("БО-inc"));
  sale.counterpartyId = buyer;
  sale.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 200000));
  saveAndPost(session, sale);
  const Result<IncomeBook> book =
      IncomeBookReport(session.handle()).build(QDate(2026, 4, 1), QDate(2026, 4, 30));
  REQUIRE(book);
  REQUIRE(book.value().income.cents() == 200000);
  REQUIRE(book.value().tax.cents() == 10000);

  Document service = makeDoc(DocumentType::Manual, QDate(2026, 4, 5), QStringLiteral("БО-svc-inc"));
  service.counterpartyId = buyer;
  service.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kServiceRevenue), 50000));
  saveAndPost(session, service);
  const Result<IncomeBook> withService =
      IncomeBookReport(session.handle()).build(QDate(2026, 4, 1), QDate(2026, 4, 30));
  REQUIRE(withService);
  REQUIRE(withService.value().income.cents() == 250000);
  REQUIRE(withService.value().tax.cents() == 12500);

  Document reversing =
      makeDoc(DocumentType::Manual, QDate(2026, 4, 10), QStringLiteral("БО-ret-inc"));
  reversing.counterpartyId = buyer;
  reversing.lines.push_back(
      line(account(session, chart::kRevenue), account(session, chart::kCustomers), 40000));
  saveAndPost(session, reversing);
  const Result<IncomeBook> net =
      IncomeBookReport(session.handle()).build(QDate(2026, 4, 1), QDate(2026, 4, 30));
  REQUIRE(net);
  REQUIRE(net.value().income.cents() == 210000);
  REQUIRE(net.value().tax.cents() == 10500);

  profile.taxSystem = TaxSystem::General;
  REQUIRE(session.company().save(profile));
  const Result<IncomeBook> general =
      IncomeBookReport(session.handle()).build(QDate(2026, 4, 1), QDate(2026, 4, 30));
  REQUIRE(general);
  REQUIRE(general.value().income.cents() == 210000);
  REQUIRE(general.value().tax.isZero());
}

TEST_CASE("cash flow 301+311 and profitability margin")
{
  LedgerSession session = openSession();
  const AccountId cash = account(session, chart::kCash);
  const AccountId bank = account(session, chart::kBank);
  const AccountId revenue = account(session, chart::kRevenue);
  const AccountId customers = account(session, chart::kCustomers);
  const AccountId expense = account(session, chart::kAdminExpense);

  Document openCash =
      makeDoc(DocumentType::CashIn, QDate(2026, 2, 28), QStringLiteral("ПКО-cf-open"));
  openCash.lines.push_back(line(cash, revenue, 50000));
  saveAndPost(session, openCash);

  Document saleCash = makeDoc(DocumentType::CashIn, QDate(2026, 3, 5), QStringLiteral("ПКО-cf-in"));
  saleCash.lines.push_back(line(cash, revenue, 200000));
  saveAndPost(session, saleCash);

  Document transfer = makeDoc(DocumentType::Manual, QDate(2026, 3, 8), QStringLiteral("БО-cf-tr"));
  transfer.lines.push_back(line(bank, cash, 40000));
  saveAndPost(session, transfer);

  Document payBank = makeDoc(DocumentType::Manual, QDate(2026, 3, 10), QStringLiteral("БО-cf-out"));
  payBank.lines.push_back(line(expense, bank, 30000));
  saveAndPost(session, payBank);

  CashFlowReport report(session.handle());
  const Result<CashFlowStatement> cf = report.build(QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(cf);
  REQUIRE(cf.value().openingCash.cents() == 50000);
  REQUIRE(cf.value().openingBank.cents() == 0);
  REQUIRE(cf.value().openingTotal.cents() == 50000);
  REQUIRE(cf.value().incoming.cents() == 200000);
  REQUIRE(cf.value().outgoing.cents() == 30000);
  REQUIRE(cf.value().closingCash.cents() == 210000);  // 50+200-40
  REQUIRE(cf.value().closingBank.cents() == 10000);   // 40-30
  REQUIRE(cf.value().closingTotal.cents() == 220000); // 50+200-30

  bool sawRevenue = false;
  bool sawExpense = false;
  for (const CashFlowCorrRow & row : cf.value().byCorrespondence) {
    if (row.accountCode == QString::fromLatin1(chart::kRevenue)) {
      sawRevenue = true;
      REQUIRE(row.incoming.cents() == 200000);
    }
    if (row.accountCode == QString::fromLatin1(chart::kAdminExpense)) {
      sawExpense = true;
      REQUIRE(row.outgoing.cents() == 30000);
    }
  }
  REQUIRE(sawRevenue);
  REQUIRE(sawExpense);

  Document accrual =
      makeDoc(DocumentType::Manual, QDate(2026, 3, 15), QStringLiteral("БО-cf-accr"));
  accrual.counterpartyId = customer(session, QStringLiteral("ТОВ Нарахування"));
  accrual.lines.push_back(line(customers, revenue, 50000));
  saveAndPost(session, accrual);

  const Result<Profitability> profit = report.profitability(QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(profit);
  // 200k (каса) + 50k (нарахування) − 30k (92)
  REQUIRE(profit.value().revenue.cents() == 250000);
  REQUIRE(profit.value().costs.cents() == 30000);
  REQUIRE(profit.value().profit.cents() == 220000);
  REQUIRE(profit.value().marginBp == 8800); // 220/250 = 88%
}

TEST_CASE("cash flow includes foreign currency cash 302 and bank 312")
{
  LedgerSession session = openSession();
  const AccountId fxCash = account(session, chart::kForeignCash);
  const AccountId fxBank = account(session, chart::kForeignBank);
  const AccountId revenue = account(session, chart::kRevenue);
  const AccountId expense = account(session, chart::kAdminExpense);

  Document opening = makeDoc(DocumentType::Manual, QDate(2026, 2, 27), QStringLiteral("БО-fx-op"));
  opening.lines.push_back(line(fxCash, revenue, 90000));
  saveAndPost(session, opening);

  Document income = makeDoc(DocumentType::Manual, QDate(2026, 3, 4), QStringLiteral("БО-fx-in"));
  income.lines.push_back(line(fxBank, revenue, 120000));
  saveAndPost(session, income);

  Document payment = makeDoc(DocumentType::Manual, QDate(2026, 3, 6), QStringLiteral("БО-fx-out"));
  payment.lines.push_back(line(expense, fxCash, 20000));
  saveAndPost(session, payment);

  // Переміщення між валютною касою та валютним рахунком — не чистий рух коштів
  Document transfer = makeDoc(DocumentType::Manual, QDate(2026, 3, 8), QStringLiteral("БО-fx-tr"));
  transfer.lines.push_back(line(fxBank, fxCash, 10000));
  saveAndPost(session, transfer);

  const Result<CashFlowStatement> cf =
      CashFlowReport(session.handle()).build(QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(cf);
  REQUIRE(cf.value().openingCash.cents() == 90000);
  REQUIRE(cf.value().openingBank.cents() == 0);
  REQUIRE(cf.value().incoming.cents() == 120000);
  REQUIRE(cf.value().outgoing.cents() == 20000);
  REQUIRE(cf.value().closingCash.cents() == 60000);   // 90 − 20 − 10
  REQUIRE(cf.value().closingBank.cents() == 130000);  // 120 + 10
  REQUIRE(cf.value().closingTotal.cents() == 190000); // 90 + 120 − 20

  bool sawRevenue = false;
  bool sawExpense = false;
  for (const CashFlowCorrRow & row : cf.value().byCorrespondence) {
    if (row.accountCode == QString::fromLatin1(chart::kRevenue)) {
      sawRevenue = true;
      REQUIRE(row.incoming.cents() == 120000);
    }
    if (row.accountCode == QString::fromLatin1(chart::kAdminExpense)) {
      sawExpense = true;
      REQUIRE(row.outgoing.cents() == 20000);
    }
  }
  REQUIRE(sawRevenue);
  REQUIRE(sawExpense);
}

TEST_CASE("income statement includes exchange gain 714 and loss 944")
{
  LedgerSession session = openSession();
  Document gain = makeDoc(DocumentType::Manual, QDate(2026, 6, 5), QStringLiteral("БО-714"));
  gain.lines.push_back(
      line(account(session, chart::kBank), account(session, chart::kExchangeGain), 50000));
  saveAndPost(session, gain);
  Document loss = makeDoc(DocumentType::Manual, QDate(2026, 6, 6), QStringLiteral("БО-944"));
  loss.lines.push_back(
      line(account(session, chart::kExchangeLoss), account(session, chart::kBank), 20000));
  saveAndPost(session, loss);

  FinancialStatementReport statements(session.handle());
  const Result<IncomeStatement> pl = statements.income(QDate(2026, 6, 1), QDate(2026, 6, 30));
  REQUIRE(pl);
  REQUIRE(pl.value().otherIncome.cents() == 50000);
  REQUIRE(pl.value().admin.cents() == 20000);
  REQUIRE(pl.value().profit.cents() == 30000);

  const Result<BalanceSheet> sheet = statements.balance(QDate(2026, 6, 30));
  REQUIRE(sheet);
  // Незакриті 714/944 входять у openResult, не в активи/зобов'язання
  for (const StatementLine & row : sheet.value().assets) {
    REQUIRE(row.code != QString::fromLatin1(chart::kExchangeLoss));
  }
  for (const StatementLine & row : sheet.value().liabilities) {
    REQUIRE(row.code != QString::fromLatin1(chart::kExchangeGain));
  }
}
