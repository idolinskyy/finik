#include "support/Session.h"

#include "fin/auth/Access.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"
#include "fin/domain/MonthClose.h"
#include "fin/print/PrintForms.h"

using namespace fin;
using namespace fin::test;

namespace {

EmployeeId hire(LedgerSession & session, const QString & taxId, const QString & name, QDate hiredOn,
                std::int64_t salaryCents, bool disabled = false)
{
  Employee row;
  row.taxId = taxId;
  row.name = name;
  row.hiredOn = hiredOn;
  row.salary = Money::fromCents(salaryCents);
  row.disabledSsc = disabled;
  row.isActive = true;
  Result<EmployeeId> id = session.employees().insert(row);
  REQUIRE(id);
  return id.value();
}

Document payrollDoc(LedgerSession & session, QDate date, const QString & number,
                    const std::vector<Employee> & staff)
{
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  std::vector<PayrollLine> rows;
  for (const Employee & employee : staff) {
    const Result<PayrollLine> row = payrollLineFor(employee, date);
    REQUIRE(row);
    rows.push_back(row.value());
  }
  Document document = makeDoc(DocumentType::Payroll, date, number);
  const Result<std::vector<DocumentLine>> lines = composePayroll(accounts.value(), rows);
  REQUIRE(lines);
  document.lines = lines.value();
  return document;
}

} // namespace

TEST_CASE("payroll rates 2026 are integer kopiyky")
{
  const QDate on(2026, 3, 31);
  const Result<PayrollCharge> charge =
      computePayroll(Money::fromCents(1000000), false, true, false, on);
  REQUIRE(charge);
  REQUIRE(charge.value().pit == Money::fromCents(180000));
  REQUIRE(charge.value().military == Money::fromCents(50000));
  REQUIRE(charge.value().net == Money::fromCents(770000));
  REQUIRE(charge.value().ssc == Money::fromCents(220000));
  REQUIRE(charge.value().sscBase == Money::fromCents(1000000));

  const Result<PayrollCharge> floor =
      computePayroll(Money::fromCents(500000), false, true, false, on);
  REQUIRE(floor);
  REQUIRE(floor.value().sscBase == Money::fromCents(kMinWageCents2026));
  REQUIRE(floor.value().ssc == Money::fromCents(190234));

  const Result<PayrollCharge> cap =
      computePayroll(Money::fromCents(200000000), false, true, false, on);
  REQUIRE(cap);
  REQUIRE(cap.value().sscBase == Money::fromCents(kMinWageCents2026 * kSscMaxBaseMonths));
  REQUIRE(cap.value().ssc == Money::fromCents(3804680));

  const Result<PayrollCharge> disabled =
      computePayroll(Money::fromCents(1000000), true, true, false, on);
  REQUIRE(disabled);
  REQUIRE(disabled.value().ssc == Money::fromCents(84100));
}

TEST_CASE("payroll rejects years without published norms")
{
  const Result<PayrollCharge> y2027 =
      computePayroll(Money::fromCents(1000000), false, true, false, QDate(2027, 1, 31));
  REQUIRE_FALSE(y2027);
  REQUIRE(y2027.error().message.contains(QStringLiteral("2027")));
}

TEST_CASE("payroll posts 92/661/651/642/645 and journal total is gross plus ssc")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().findByCode(QString::fromLatin1(chart::kSalaryPayable)));
  REQUIRE(session.accounts().findByCode(QString::fromLatin1(chart::kSscPayable)));
  REQUIRE(session.accounts().findByCode(QString::fromLatin1(chart::kPitPayable)));
  REQUIRE(session.accounts().findByCode(QString::fromLatin1(chart::kMilitaryLevy)));

  const EmployeeId id = hire(session, QStringLiteral("1234567890"), QStringLiteral("Іваненко"),
                             QDate(2026, 1, 10), 1000000);
  Result<Employee> employee = session.employees().findById(id);
  REQUIRE(employee);
  Document document =
      payrollDoc(session, QDate(2026, 3, 31), QStringLiteral("ЗП-1"), {employee.value()});
  const DocumentId docId = saveAndPost(session, document);
  Result<Document> posted = session.documents().findById(docId);
  REQUIRE(posted);
  REQUIRE(posted.value().lines.size() == 4);
  REQUIRE(posted.value().lines[0].debitAccountId == account(session, chart::kAdminExpense));
  REQUIRE(posted.value().lines[0].creditAccountId == account(session, chart::kSalaryPayable));
  REQUIRE(posted.value().lines[1].creditAccountId == account(session, chart::kSscPayable));
  REQUIRE(posted.value().lines[2].creditAccountId == account(session, chart::kPitPayable));
  REQUIRE(posted.value().lines[3].creditAccountId == account(session, chart::kMilitaryLevy));
  REQUIRE(posted.value().lines[2].creditAccountId != account(session, chart::kVatBudget));

  const Result<std::vector<DocumentHeader>> list =
      session.documents().list(QDate(2026, 3, 1), QDate(2026, 3, 31), DocumentType::Payroll);
  REQUIRE(list);
  REQUIRE(list.value().size() == 1);
  REQUIRE(list.value()[0].total == Money::fromCents(1220000));

  const Result<QString> html = PrintForms(session.handle()).documentHtml(posted.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("661")));
  REQUIRE(html.value().contains(QStringLiteral("ПДФО")));
  REQUIRE(session.verify());
}

TEST_CASE("one posted payroll per month; payment based on 661; leftover 661 close")
{
  LedgerSession session = openSession();
  const EmployeeId id = hire(session, QStringLiteral("1234567891"), QStringLiteral("Петренко"),
                             QDate(2025, 12, 1), 1000000);
  Result<Employee> employee = session.employees().findById(id);
  REQUIRE(employee);
  Document first =
      payrollDoc(session, QDate(2026, 3, 31), QStringLiteral("ЗП-a"), {employee.value()});
  const DocumentId payrollId = saveAndPost(session, first);

  Document second =
      payrollDoc(session, QDate(2026, 3, 15), QStringLiteral("ЗП-b"), {employee.value()});
  Result<DocumentId> secondId = session.documents().insert(second);
  REQUIRE(secondId);
  REQUIRE_FALSE(session.posting().post(secondId.value()));
  REQUIRE(session.documents().remove(secondId.value()));

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Result<Document> posted = session.documents().findById(payrollId);
  REQUIRE(posted);
  Result<Document> pay = buildBasedOn(posted.value(), DocumentType::BankPayment, accounts.value());
  REQUIRE(pay);
  REQUIRE_FALSE(pay.value().counterpartyId);
  REQUIRE(pay.value().lines.size() == 1);
  REQUIRE(pay.value().lines[0].debitAccountId == account(session, chart::kSalaryPayable));
  REQUIRE(pay.value().lines[0].creditAccountId == account(session, chart::kBank));
  REQUIRE(pay.value().lines[0].amount == Money::fromCents(770000));
  pay.value().number = QStringLiteral("БП-зп");
  const DocumentId payId = saveAndPost(session, pay.value());
  REQUIRE_FALSE(session.posting().unpost(payrollId));
  REQUIRE(session.posting().unpost(payId));
  REQUIRE(session.posting().unpost(payrollId));
  REQUIRE(session.posting().post(payrollId));

  REQUIRE_FALSE(session.employees().remove(id));

  Result<Document> result = buildPeriodResultDocument(session.handle(), 2026, 3);
  REQUIRE(result);
  const Result<DocumentId> resultId = session.documents().insert(result.value());
  REQUIRE(resultId);
  REQUIRE(session.posting().post(resultId.value()));
  REQUIRE(session.period().closeThroughMonth(2026, 3));
  REQUIRE(session.verify());
}

TEST_CASE("cashier cannot write payroll; prefix ЗП")
{
  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Cashier, DocumentType::Payroll));
  REQUIRE(Access::canWriteDocument(UserRole::Accountant, DocumentType::Payroll));
  REQUIRE(documentTypePrefix(DocumentType::Payroll) == QStringLiteral("ЗП"));
  REQUIRE(usesPayrollForm(DocumentType::Payroll));
  REQUIRE_FALSE(usesPayrollForm(DocumentType::VacationPayroll));
  REQUIRE_FALSE(usesPayrollForm(DocumentType::SickLeavePayroll));
  REQUIRE(isPayrollFamily(DocumentType::VacationPayroll));
  REQUIRE(isPayrollFamily(DocumentType::SickLeavePayroll));
}
