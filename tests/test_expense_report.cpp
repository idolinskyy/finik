#include "support/Session.h"

#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"
#include "fin/print/PrintForms.h"

using namespace fin;
using namespace fin::test;

namespace {

EmployeeId hireEmployee(LedgerSession & session, const QString & taxId, const QString & name)
{
  Employee row;
  row.taxId = taxId;
  row.name = name;
  row.hiredOn = QDate(2026, 1, 1);
  row.salary = Money::fromCents(1500000);
  row.isActive = true;
  Result<EmployeeId> id = session.employees().insert(row);
  REQUIRE(id);
  return id.value();
}

} // namespace

TEST_CASE(
    "expense report: full lifecycle with advance cash out, expense report, and return cash in")
{
  LedgerSession session = openSession();

  const EmployeeId empId = hireEmployee(session, QStringLiteral("2938475610"),
                                        QStringLiteral("Петренко Петро Петрович"));
  const NomenclatureId sugarId = item(session, QStringLiteral("Цукор-пісок"));

  const AccountId acc301 = account(session, chart::kCash);
  const AccountId acc3721 = account(session, chart::kAccountablePersonsUah);
  const AccountId acc92 = account(session, chart::kAdminExpense);
  const AccountId acc201 = account(session, chart::kRawMaterials);
  const AccountId acc6442 = account(session, chart::kTaxCreditCheques);

  REQUIRE(acc301.isValid());
  REQUIRE(acc3721.isValid());
  REQUIRE(acc92.isValid());
  REQUIRE(acc201.isValid());
  REQUIRE(acc6442.isValid());

  // 1. Початковий залишок у касі 10 000 грн
  {
    Document capDoc =
        makeDoc(DocumentType::OpeningBalance, QDate(2026, 1, 1), QStringLiteral("ПОЧ-1"));
    DocumentLine capLine;
    capLine.debitAccountId = acc301;
    capLine.creditAccountId = account(session, chart::kAuxiliary);
    capLine.amount = Money::fromCents(1000000); // 10 000 грн
    capDoc.lines.push_back(capLine);
    saveAndPost(session, capDoc);
  }

  // 2. Видача авансу з каси на відрядження / госппотреби: Дт 3721 - Кт 301, 2500.00 грн
  Document cashOut = makeDoc(DocumentType::CashOut, QDate(2026, 3, 1), QStringLiteral("ВКО-1"));
  cashOut.employeeId = empId;
  DocumentLine outLine;
  outLine.debitAccountId = acc3721;
  outLine.creditAccountId = acc301;
  outLine.amount = Money::fromCents(250000); // 2 500 грн
  outLine.employeeId = empId;
  outLine.description = QStringLiteral("Аванс на відрядження та закупівлю матеріалів");
  cashOut.lines.push_back(outLine);

  const DocumentId cashOutId = saveAndPost(session, cashOut);
  REQUIRE(cashOutId.isValid());

  // 3. Складання авансового звіту:
  // - Канцтовари: Дт 92 - Кт 3721 (800 грн) + ПДВ Дт 6442 - Кт 3721 (160 грн)
  // - Матеріали (Цукор): Дт 201 - Кт 3721 (1000 грн, 25 кг) + ПДВ Дт 6442 - Кт 3721 (200 грн)
  // - Добові: Дт 92 - Кт 3721 (300 грн)
  // Разом витрачено: 800 + 160 + 1000 + 200 + 300 = 2 460.00 грн
  std::vector<ExpenseLine> expenseLines;

  // Рядок 1: Канцтовари
  ExpenseLine l1;
  l1.expenseAccountId = acc92;
  l1.description = QStringLiteral("Канцелярські товари за чеком №11");
  l1.documentRef = QStringLiteral("Чек № 11");
  l1.amount = Money::fromCents(80000);
  l1.vatRateBp = 2000;
  l1.vatCents = 16000;
  expenseLines.push_back(l1);

  // Рядок 2: Матеріали
  ExpenseLine l2;
  l2.expenseAccountId = acc201;
  l2.nomenclatureId = sugarId;
  l2.quantity = Quantity::fromMilli(25000); // 25.000 кг
  l2.unitPrice = Money::fromCents(4000);    // 40.00 грн/кг
  l2.amount = Money::fromCents(100000);
  l2.vatRateBp = 2000;
  l2.vatCents = 20000;
  l2.documentRef = QStringLiteral("Чек № 12");
  l2.description = QStringLiteral("Цукор 25 кг");
  expenseLines.push_back(l2);

  // Рядок 3: Добові (без ПДВ)
  ExpenseLine l3;
  l3.expenseAccountId = acc92;
  l3.description = QStringLiteral("Добові за 1 день відрядження");
  l3.documentRef = QStringLiteral("Наказ № 5-В");
  l3.amount = Money::fromCents(30000);
  l3.vatRateBp = 0;
  l3.vatCents = 0;
  expenseLines.push_back(l3);

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  const Result<std::vector<DocumentLine>> composed =
      composeExpenseReport(accounts.value(), expenseLines, acc3721);
  REQUIRE(composed);
  REQUIRE(composed.value().size() == 5); // 92, 6442, 201, 6442, 92

  Document expDoc = makeDoc(DocumentType::ExpenseReport, QDate(2026, 3, 5), QStringLiteral("АЗ-1"));
  expDoc.employeeId = empId;
  expDoc.lines = composed.value();

  const DocumentId expDocId = saveAndPost(session, expDoc);
  REQUIRE(expDocId.isValid());

  // 4. Повернення невикористаного залишку в касу: 2500 - 2460 = 40.00 грн
  // Дт 301 - Кт 3721
  Document cashIn = makeDoc(DocumentType::CashIn, QDate(2026, 3, 6), QStringLiteral("ПКО-1"));
  cashIn.employeeId = empId;
  DocumentLine inLine;
  inLine.debitAccountId = acc301;
  inLine.creditAccountId = acc3721;
  inLine.amount = Money::fromCents(4000); // 40.00 грн
  inLine.employeeId = empId;
  inLine.description = QStringLiteral("Повернення невикористаного авансу за АЗ-1");
  cashIn.lines.push_back(inLine);

  const DocumentId cashInId = saveAndPost(session, cashIn);
  REQUIRE(cashInId.isValid());

  // 5. Перевірка сальдо по рахунку 3721:
  // Дебет: видача 2500.00 грн
  // Кредит: звіт 2460.00 грн + повернення 40.00 грн = 2500.00 грн
  // Кінцеве сальдо: 0
  const Result<TrialBalance> tb =
      session.trialBalance().build(QDate(2026, 1, 1), QDate(2026, 3, 31));
  REQUIRE(tb);
  bool found3721 = false;
  for (const auto & row : tb.value().rows) {
    if (row.code == QString::fromLatin1(chart::kAccountablePersonsUah)) {
      found3721 = true;
      REQUIRE(row.turnoverDebit == Money::fromCents(250000));
      REQUIRE(row.turnoverCredit == Money::fromCents(250000));
      REQUIRE(row.closingDebit.isZero());
      REQUIRE(row.closingCredit.isZero());
    }
  }
  REQUIRE(found3721);
}

TEST_CASE("expense report: validation rules")
{
  LedgerSession session = openSession();
  const EmployeeId empId =
      hireEmployee(session, QStringLiteral("1122334455"), QStringLiteral("Сидоренко С.С."));

  const AccountId acc3721 = account(session, chart::kAccountablePersonsUah);
  const AccountId acc92 = account(session, chart::kAdminExpense);

  // Валідація: відсутній employeeId
  Document docWithoutEmp =
      makeDoc(DocumentType::ExpenseReport, QDate(2026, 3, 1), QStringLiteral("АЗ-ERR1"));
  DocumentLine line1;
  line1.debitAccountId = acc92;
  line1.creditAccountId = acc3721;
  line1.amount = Money::fromCents(10000);
  docWithoutEmp.lines.push_back(line1);

  const Result<DocumentId> res1 = session.documents().insert(docWithoutEmp);
  REQUIRE(res1);
  const Status postRes1 = session.posting().post(res1.value());
  REQUIRE_FALSE(postRes1); // Має відхилити проведення через відсутність підзвітної особи

  // Валідація: Кредит має бути рахунком підзвітних осіб (372/3721/3722)
  Document docWrongCredit =
      makeDoc(DocumentType::ExpenseReport, QDate(2026, 3, 1), QStringLiteral("АЗ-ERR2"));
  docWrongCredit.employeeId = empId;
  DocumentLine line2;
  line2.debitAccountId = acc92;
  line2.creditAccountId = account(session, chart::kCash); // помилковий кредит 301 замість 3721
  line2.amount = Money::fromCents(10000);
  docWrongCredit.lines.push_back(line2);

  const Result<DocumentId> res2 = session.documents().insert(docWrongCredit);
  REQUIRE(res2);
  const Status postRes2 = session.posting().post(res2.value());
  REQUIRE_FALSE(postRes2);

  // Валідація: сума має бути додатною (insert відхиляє нульову суму через CHECK constraint)
  Document docZeroAmount =
      makeDoc(DocumentType::ExpenseReport, QDate(2026, 3, 1), QStringLiteral("АЗ-ERR3"));
  docZeroAmount.employeeId = empId;
  DocumentLine line3;
  line3.debitAccountId = acc92;
  line3.creditAccountId = acc3721;
  line3.amount = Money::zero();
  docZeroAmount.lines.push_back(line3);

  const Result<DocumentId> res3 = session.documents().insert(docZeroAmount);
  REQUIRE_FALSE(res3);
}

TEST_CASE("expense report: print form generates valid HTML")
{
  LedgerSession session = openSession();
  const EmployeeId empId =
      hireEmployee(session, QStringLiteral("3344556677"), QStringLiteral("Гончаренко О.В."));

  const AccountId acc3721 = account(session, chart::kAccountablePersonsUah);
  const AccountId acc92 = account(session, chart::kAdminExpense);

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  ExpenseLine el;
  el.expenseAccountId = acc92;
  el.amount = Money::fromCents(50000);
  el.vatRateBp = 2000;
  el.vatCents = 10000;
  el.documentRef = QStringLiteral("Чек № 55");
  el.description = QStringLiteral("Придбання паперу А4");

  const Result<std::vector<DocumentLine>> lines =
      composeExpenseReport(accounts.value(), {el}, acc3721);
  REQUIRE(lines);

  Document doc = makeDoc(DocumentType::ExpenseReport, QDate(2026, 3, 10), QStringLiteral("АЗ-10"));
  doc.employeeId = empId;
  doc.lines = lines.value();

  const DocumentId docId = saveAndPost(session, doc);
  REQUIRE(docId.isValid());

  const Result<Document> loaded = session.documents().findById(docId);
  REQUIRE(loaded);

  PrintForms forms(session.handle());
  const Result<QString> html = forms.expenseReportHtml(loaded.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("Звіт про використання коштів")));
  REQUIRE(html.value().contains(QStringLiteral("№ 841")));
  REQUIRE(html.value().contains(QStringLiteral("Гончаренко О.В.")));
  REQUIRE(html.value().contains(QStringLiteral("3344556677")));
  REQUIRE(html.value().contains(QStringLiteral("600.00"))); // 500 + 100 = 600 грн
}

TEST_CASE("expense report: based-on generation")
{
  LedgerSession session = openSession();
  const EmployeeId empId =
      hireEmployee(session, QStringLiteral("9988776655"), QStringLiteral("Коваленко К.К."));

  Document doc = makeDoc(DocumentType::ExpenseReport, QDate(2026, 3, 15), QStringLiteral("АЗ-77"));
  doc.employeeId = empId;

  DocumentLine l1;
  l1.debitAccountId = account(session, chart::kAdminExpense);
  l1.creditAccountId = account(session, chart::kAccountablePersonsUah);
  l1.amount = Money::fromCents(75000);
  l1.description = QStringLiteral("Послуги таксі");
  doc.lines.push_back(l1);

  const DocumentId docId = saveAndPost(session, doc);
  REQUIRE(docId.isValid());

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  // Створення ПКО на підставі Авансового звіту (повернення залишку або розрахунок)
  const Result<Document> basedCashIn = buildBasedOn(doc, DocumentType::CashIn, accounts.value());
  REQUIRE(basedCashIn);
  REQUIRE(basedCashIn.value().type == DocumentType::CashIn);
  REQUIRE(basedCashIn.value().employeeId == empId);
  REQUIRE(basedCashIn.value().lines.size() == 1);
  REQUIRE(basedCashIn.value().lines[0].creditAccountId ==
          account(session, chart::kAccountablePersonsUah));
  REQUIRE(basedCashIn.value().lines[0].debitAccountId == account(session, chart::kCash));
  REQUIRE(basedCashIn.value().lines[0].amount == Money::fromCents(75000));
}
