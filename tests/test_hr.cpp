#include "support/Session.h"

#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"

using namespace fin;
using namespace fin::test;

namespace {

EmployeeId insertEmployee(LedgerSession & session, const QString & taxId, const QString & name,
                          QDate hiredOn, std::int64_t salaryCents, bool disabled = false)
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

} // namespace

TEST_CASE("HrOrder: Hire, Transfer, Dismiss updates employee state")
{
  LedgerSession session = openSession();
  const EmployeeId id = insertEmployee(session, QStringLiteral("1234567890"),
                                       QStringLiteral("Тест"), QDate(2026, 1, 1), 1000000);

  // 1. Hire
  Document hrDoc1 = makeDoc(DocumentType::Manual, QDate(2026, 1, 1), QStringLiteral("Н-1"));
  const DocumentId docId1 = session.documents().insert(hrDoc1).value();
  HrOrder hire;
  hire.documentId = docId1;
  hire.employeeId = id;
  hire.orderType = HrOrderType::Hire;
  hire.effectiveDate = QDate(2026, 1, 1);
  hire.newPosition = QStringLiteral("Менеджер");
  hire.newDepartment = QStringLiteral("Продажі");

  Result<HrOrderId> hireId = session.hrOrders().insert(hire);
  REQUIRE(hireId);

  Result<Employee> emp1 = session.employees().findById(id);
  REQUIRE(emp1);
  REQUIRE(emp1.value().position == QStringLiteral("Менеджер"));
  REQUIRE(emp1.value().department == QStringLiteral("Продажі"));

  // 2. Transfer
  Document hrDoc2 = makeDoc(DocumentType::Manual, QDate(2026, 6, 1), QStringLiteral("Н-2"));
  const DocumentId docId2 = session.documents().insert(hrDoc2).value();
  HrOrder transfer;
  transfer.documentId = docId2;
  transfer.employeeId = id;
  transfer.orderType = HrOrderType::Transfer;
  transfer.effectiveDate = QDate(2026, 6, 1);
  transfer.newPosition = QStringLiteral("Директор");
  transfer.newSalary = Money::fromCents(2000000);

  Result<HrOrderId> transferId = session.hrOrders().insert(transfer);
  REQUIRE(transferId);

  Result<Employee> emp2 = session.employees().findById(id);
  REQUIRE(emp2);
  REQUIRE(emp2.value().position == QStringLiteral("Директор"));
  REQUIRE(emp2.value().department == QStringLiteral("Продажі")); // залишилось старе
  REQUIRE(emp2.value().salary == Money::fromCents(2000000));

  // 3. Dismiss
  Document hrDoc3 = makeDoc(DocumentType::Manual, QDate(2026, 12, 31), QStringLiteral("Н-3"));
  const DocumentId docId3 = session.documents().insert(hrDoc3).value();
  HrOrder dismiss;
  dismiss.documentId = docId3;
  dismiss.employeeId = id;
  dismiss.orderType = HrOrderType::Dismiss;
  dismiss.effectiveDate = QDate(2026, 12, 31);
  dismiss.dismissReason = QStringLiteral("За власним бажанням");

  Result<HrOrderId> dismissId = session.hrOrders().insert(dismiss);
  REQUIRE(dismissId);

  Result<Employee> emp3 = session.employees().findById(id);
  REQUIRE(emp3);
  REQUIRE_FALSE(emp3.value().isActive);
  REQUIRE(emp3.value().firedOn == QDate(2026, 12, 31));
}

TEST_CASE("Timesheet: findOrCreate and upsertRow")
{
  LedgerSession session = openSession();
  const EmployeeId id1 = insertEmployee(session, QStringLiteral("1111111111"), QStringLiteral("А"),
                                        QDate(2026, 1, 1), 1000000);
  const EmployeeId id2 = insertEmployee(session, QStringLiteral("2222222222"), QStringLiteral("Б"),
                                        QDate(2026, 1, 1), 1000000);
  (void)id1;
  (void)id2;

  Result<Timesheet> ts = session.timesheets().findOrCreate(2026, 1);
  REQUIRE(ts);
  REQUIRE(ts.value().year == 2026);
  REQUIRE(ts.value().month == 1);
  REQUIRE(ts.value().rows.size() == 2);
  REQUIRE(ts.value().rows[0].workingDaysNorm == workingDaysInMonth(2026, 1));
  REQUIRE(ts.value().rows[0].daysWorked == 0); // ще не заповнено

  TimesheetRow upd = ts.value().rows[0];
  upd.daysWorked = 20;
  upd.hoursWorked = 160;
  REQUIRE(session.timesheets().upsertRow(2026, 1, upd));

  Result<Timesheet> ts2 = session.timesheets().findOrCreate(2026, 1);
  REQUIRE(ts2);
  REQUIRE(ts2.value().rows[0].daysWorked == 20);
}

TEST_CASE("Vacation: computeVacation and Document")
{
  LedgerSession session = openSession();
  const EmployeeId id = insertEmployee(session, QStringLiteral("1111111111"), QStringLiteral("В"),
                                       QDate(2025, 1, 1), 1000000);
  Result<Employee> emp = session.employees().findById(id);
  REQUIRE(emp);

  // Для відпускних потрібні виплати за 12 місяців, але без історії буде 0
  Result<VacationCalc> calc =
      computeVacation(session.handle(), emp.value(), QDate(2026, 7, 1), QDate(2026, 7, 14));
  REQUIRE(calc);
  REQUIRE(calc.value().vacationDays == 14);

  // Перевіримо створення документа
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Document doc = makeDoc(DocumentType::VacationPayroll, QDate(2026, 7, 1), QStringLiteral("В-1"));
  Result<std::vector<DocumentLine>> lines = composeVacationPayroll(accounts.value(), calc.value());
  REQUIRE(lines);
  doc.lines = lines.value();

  const DocumentId docId = saveAndPost(session, doc);
  REQUIRE(docId);
  Result<Document> posted = session.documents().findById(docId);
  REQUIRE(posted);
  REQUIRE(posted.value().lines.size() >= 4); // 92-661, 92-651, 661-642, 661-645

  REQUIRE(canPrintDocument(DocumentType::VacationPayroll));
  const auto targets = basedOnTargets(DocumentType::VacationPayroll);
  REQUIRE(targets.size() == 2);
  Result<Document> pay =
      buildBasedOn(posted.value(), DocumentType::BankPayment, accounts.value());
  REQUIRE(pay);
  CHECK(pay.value().type == DocumentType::BankPayment);
  CHECK(pay.value().basedOnId == docId);
  REQUIRE_FALSE(pay.value().lines.empty());
}

TEST_CASE("Cannot unpost vacation payroll while payment based_on is posted")
{
  LedgerSession session = openSession();
  const EmployeeId id = insertEmployee(session, QStringLiteral("2222222222"), QStringLiteral("В2"),
                                       QDate(2025, 1, 1), 1000000);
  Result<Employee> emp = session.employees().findById(id);
  REQUIRE(emp);

  Result<VacationCalc> calc =
      computeVacation(session.handle(), emp.value(), QDate(2026, 8, 1), QDate(2026, 8, 14));
  REQUIRE(calc);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document doc = makeDoc(DocumentType::VacationPayroll, QDate(2026, 8, 1), QStringLiteral("В-2"));
  Result<std::vector<DocumentLine>> lines = composeVacationPayroll(accounts.value(), calc.value());
  REQUIRE(lines);
  doc.lines = lines.value();
  const DocumentId docId = saveAndPost(session, doc);

  Document pay = makeDoc(DocumentType::BankPayment, QDate(2026, 8, 15), QStringLiteral("ЗП-В-1"));
  pay.basedOnId = docId;
  pay.lines.push_back(
      line(account(session, chart::kSalaryPayable), account(session, chart::kBank), 100));
  saveAndPost(session, pay);

  REQUIRE_FALSE(session.posting().unpost(docId));
}
