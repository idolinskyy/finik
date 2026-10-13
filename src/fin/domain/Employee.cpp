#include "fin/domain/Employee.h"

#include "fin/core/Checked.h"
#include "fin/core/Sql.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"

#include <QHash>
#include <QList>

namespace fin {
namespace {

DocumentLine posting(AccountId debit, AccountId credit, Money amount, EmployeeId employee,
                     const QString & description)
{
  DocumentLine line;
  line.debitAccountId = debit;
  line.creditAccountId = credit;
  line.amount = amount;
  line.employeeId = employee;
  line.description = description;
  return line;
}

} // namespace

// HrOrderType

QString hrOrderTypeString(HrOrderType type)
{
  switch (type) {
  case HrOrderType::Hire:
    return QStringLiteral("hire");
  case HrOrderType::Transfer:
    return QStringLiteral("transfer");
  case HrOrderType::Dismiss:
    return QStringLiteral("dismiss");
  }
  return {};
}

QString hrOrderTypeLabelUk(HrOrderType type)
{
  switch (type) {
  case HrOrderType::Hire:
    return QStringLiteral("Прийом на роботу");
  case HrOrderType::Transfer:
    return QStringLiteral("Кадрове переведення");
  case HrOrderType::Dismiss:
    return QStringLiteral("Звільнення");
  }
  return {};
}

std::optional<HrOrderType> hrOrderTypeFromString(const QString & s)
{
  if (s == QLatin1String("hire"))
    return HrOrderType::Hire;
  if (s == QLatin1String("transfer"))
    return HrOrderType::Transfer;
  if (s == QLatin1String("dismiss"))
    return HrOrderType::Dismiss;
  return std::nullopt;
}

// Payroll helpers

bool usesPayrollForm(DocumentType type)
{
  // Лише нарахування зарплати. Відпускні / лікарняні мають окремі діалоги
  // (VacationDialog / SickLeaveDialog); див. openDocumentEditor у DocumentDialog.cpp.
  return type == DocumentType::Payroll;
}

bool isPayrollFamily(DocumentType type)
{
  return type == DocumentType::Payroll || type == DocumentType::VacationPayroll ||
         type == DocumentType::SickLeavePayroll;
}

Money payrollNetPayable(const std::vector<DocumentLine> & lines,
                        const std::vector<Account> & accounts)
{
  std::int64_t credit = 0;
  std::int64_t debit = 0;
  for (const DocumentLine & line : lines) {
    if (accountHasCode(accounts, line.creditAccountId, chart::kSalaryPayable)) {
      const Status add = addTo(credit, line.amount.cents(), QStringLiteral("661 кредит"));
      if (!add) {
        return Money::zero();
      }
    }
    if (accountHasCode(accounts, line.debitAccountId, chart::kSalaryPayable)) {
      const Status add = addTo(debit, line.amount.cents(), QStringLiteral("661 дебет"));
      if (!add) {
        return Money::zero();
      }
    }
  }
  if (credit <= debit) {
    return Money::zero();
  }
  return Money::fromCents(credit - debit);
}

// Основний розрахунок: ПДФО, ВЗ, ЄСВ (з підтримкою ПСП)

Result<PayrollNorms> payrollNormsForYear(int year)
{
  if (year == kPayrollNorms2026.year) {
    return Result<PayrollNorms>::ok(kPayrollNorms2026);
  }
  return Result<PayrollNorms>::fail(Error::validation(
      QStringLiteral("норми ПДФО/ЄСВ для %1 року не задані — нарахування заблоковано")
          .arg(year)));
}

Result<PayrollCharge> computePayroll(Money gross, bool disabledSsc, bool fullMonth,
                                     bool hasPitBenefit, QDate on)
{
  if (!on.isValid()) {
    return Result<PayrollCharge>::fail(
        Error::validation(QStringLiteral("потрібна дата нарахування для ставок ПДФО/ЄСВ")));
  }
  const Result<PayrollNorms> normsRes = payrollNormsForYear(on.year());
  if (!normsRes) {
    return Result<PayrollCharge>::fail(normsRes.error());
  }
  const PayrollNorms norms = normsRes.value();

  if (!gross.isPositive()) {
    return Result<PayrollCharge>::fail(
        Error::validation(QStringLiteral("оклад має бути більшим за нуль")));
  }

  // ПСП (п. 169.1 ПКУ): зменшує базу ПДФО, якщо gross ≤ порогу та є заява.
  // ПСП не зменшує базу ЄСВ та ВЗ.
  std::int64_t pitBase = gross.cents();
  std::int64_t pitBenefit = 0;
  if (hasPitBenefit && gross.cents() <= norms.pitBenefitThresholdCents) {
    pitBenefit = norms.pitBenefitCents;
    pitBase = gross.cents() - norms.pitBenefitCents;
    if (pitBase < 0) {
      pitBase = 0;
    }
  }

  const Result<std::int64_t> pitCents = mulDivRound(pitBase, norms.pitRateBp, 10000);
  if (!pitCents) {
    return Result<PayrollCharge>::fail(pitCents.error());
  }
  const Result<std::int64_t> militaryCents =
      mulDivRound(gross.cents(), norms.militaryRateBp, 10000);
  if (!militaryCents) {
    return Result<PayrollCharge>::fail(militaryCents.error());
  }

  // База ЄСВ: [мінзарплата (при повному місяці), 20 × МЗП]
  std::int64_t base = gross.cents();
  const std::int64_t maxBase = norms.minWageCents * norms.sscMaxBaseMonths;
  if (base > maxBase) {
    base = maxBase;
  }
  if (fullMonth && base > 0 && base < norms.minWageCents) {
    base = norms.minWageCents;
  }
  const int sscBp = disabledSsc ? norms.sscDisabledRateBp : norms.sscRateBp;
  const Result<std::int64_t> sscCents = mulDivRound(base, sscBp, 10000);
  if (!sscCents) {
    return Result<PayrollCharge>::fail(sscCents.error());
  }

  if (pitCents.value() + militaryCents.value() >= gross.cents()) {
    return Result<PayrollCharge>::fail(
        Error::validation(QStringLiteral("утримання ПДФО і військового збору перевищують оклад")));
  }

  PayrollCharge charge;
  charge.pit = Money::fromCents(pitCents.value());
  charge.military = Money::fromCents(militaryCents.value());
  charge.ssc = Money::fromCents(sscCents.value());
  charge.sscBase = Money::fromCents(base);
  charge.pitBase = Money::fromCents(pitBase);
  charge.pitBenefit = Money::fromCents(pitBenefit);
  charge.net = Money::fromCents(gross.cents() - pitCents.value() - militaryCents.value());
  return Result<PayrollCharge>::ok(charge);
}

// Норма робочих днів у місяці (спрощений алгоритм)

// Розраховує норму робочих днів для стандартного 5-денного тижня.
// Повні дні без урахування свят (для виробничого календаря зі святами потрібна
// окрема таблиця). Достатньо для розрахунку коефіцієнту часткової зайнятості.
int workingDaysInMonth(int year, int month)
{
  const QDate first(year, month, 1);
  if (!first.isValid()) {
    return 0;
  }
  const int days = first.daysInMonth();
  int count = 0;
  for (int d = 1; d <= days; ++d) {
    const QDate date(year, month, d);
    const Qt::DayOfWeek dow = static_cast<Qt::DayOfWeek>(date.dayOfWeek());
    if (dow != Qt::Saturday && dow != Qt::Sunday) {
      ++count;
    }
  }
  return count;
}

// Відсоток оплати лікарняних від страхового стажу

int sickPayPercentByExperience(int experienceYears)
{
  // ч. 4 ст. 19 Закону України «Про загальнообов'язкове державне
  // соціальне страхування» (зміни 2024 р.):
  // до 3 р. = 50%; від 3 до 5 р. = 60%; від 5 до 10 р. = 70%; ≥ 10 р. = 100%.
  if (experienceYears < 3)
    return 50;
  if (experienceYears < 5)
    return 60;
  if (experienceYears < 10)
    return 70;
  return 100;
}

// Середньоденний заробіток (КМУ № 100)

Result<Money> computeAvgDailyWage(QSqlDatabase db, EmployeeId id, QDate calcDate)
{
  if (!id) {
    return Result<Money>::fail(Error::invalidArgument(QStringLiteral("порожній id працівника")));
  }
  if (!calcDate.isValid()) {
    return Result<Money>::fail(Error::invalidArgument(QStringLiteral("невалідна дата розрахунку")));
  }

  // 12-місячний розрахунковий період (з першого дня позаминулого місяця назад)
  const QDate periodEnd = QDate(calcDate.year(), calcDate.month(), 1).addDays(-1);
  const QDate periodStart = periodEnd.addMonths(-11);
  const QDate periodFirst(periodStart.year(), periodStart.month(), 1);

  // Сума всіх нарахувань заробітної плати за 12 місяців:
  // беремо нарахування Дт 92 Кт 661 (рядки документів Payroll/VacationPayroll/SickLeavePayroll)
  sql::Query query(db);
  Status status = query.prepare(
      QStringLiteral("SELECT SUM(dl.amount_cents) "
                     "FROM document_lines dl "
                     "JOIN documents d ON d.id = dl.document_id "
                     "JOIN accounts da ON da.id = dl.debit_account_id "
                     "JOIN accounts ca ON ca.id = dl.credit_account_id "
                     "WHERE dl.employee_id = :eid "
                     "  AND d.posted = 1 "
                     "  AND d.date >= :from AND d.date <= :to "
                     "  AND da.code = '92' "
                     "  AND ca.code = '661' "
                     "  AND d.type IN ('payroll','vacation_payroll','sick_leave_payroll')"));
  if (!status) {
    return Result<Money>::fail(status.error());
  }
  query.bind(QStringLiteral(":eid"), static_cast<qlonglong>(id.value));
  query.bind(QStringLiteral(":from"), periodFirst.toString(Qt::ISODate));
  query.bind(QStringLiteral(":to"), periodEnd.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return Result<Money>::fail(status.error());
  }

  std::int64_t totalCents = 0;
  if (query.next() && !query.value(0).isNull()) {
    totalCents = query.value(0).toLongLong();
  }

  // Кількість календарних днів розрахункового 12-місячного діапазону
  const int calDays = static_cast<int>(periodFirst.daysTo(periodEnd)) + 1;
  if (calDays <= 0) {
    return Result<Money>::fail(
        Error::validation(QStringLiteral("некоректний розрахунковий період")));
  }

  // Середньоденний заробіток округлюємо до копійки (div + round)
  const std::int64_t avgCents = (totalCents + calDays / 2) / calDays;
  return Result<Money>::ok(Money::fromCents(avgCents));
}

// Розрахунок відпускних

Result<VacationCalc> computeVacation(QSqlDatabase db, const Employee & employee, QDate from,
                                     QDate to)
{
  if (!employee.id) {
    return Result<VacationCalc>::fail(
        Error::invalidArgument(QStringLiteral("порожній id працівника")));
  }
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<VacationCalc>::fail(
        Error::validation(QStringLiteral("невалідний діапазон відпустки")));
  }

  const int vacDays = static_cast<int>(from.daysTo(to)) + 1;
  const Result<Money> avgDaily = computeAvgDailyWage(db, employee.id, from);
  if (!avgDaily) {
    return Result<VacationCalc>::fail(avgDaily.error());
  }

  // Якщо середньоденного не було (новий працівник) — використовуємо оклад / 29.3
  // (умовна кількість calendar. днів на місяць за КМУ № 100 п. 3-б)
  const std::int64_t dailyCents = avgDaily.value().cents() > 0
                                      ? avgDaily.value().cents()
                                      : (employee.salary.cents() * 10 + 146) / 293; // ÷ 29.3

  const std::int64_t grossCents = dailyCents * vacDays;
  const Money gross = Money::fromCents(grossCents);

  // Відпускні оподатковуються як зарплата
  const Result<PayrollCharge> taxes =
      computePayroll(gross, employee.disabledSsc, true, employee.hasPitBenefit, from);
  if (!taxes) {
    return Result<VacationCalc>::fail(taxes.error());
  }

  VacationCalc calc;
  calc.employeeId = employee.id;
  calc.vacationFrom = from;
  calc.vacationTo = to;
  calc.vacationDays = vacDays;
  calc.avgDailyCents = Money::fromCents(dailyCents);
  calc.gross = gross;
  calc.taxes = taxes.value();
  return Result<VacationCalc>::ok(calc);
}

// Розрахунок лікарняних

Result<SickLeaveCalc> computeSickLeave(QSqlDatabase db, const Employee & employee, QDate from,
                                       QDate to, int experienceYears)
{
  if (!employee.id) {
    return Result<SickLeaveCalc>::fail(
        Error::invalidArgument(QStringLiteral("порожній id працівника")));
  }
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<SickLeaveCalc>::fail(
        Error::validation(QStringLiteral("невалідний діапазон лікарняного")));
  }
  if (experienceYears < 0) {
    return Result<SickLeaveCalc>::fail(
        Error::validation(QStringLiteral("стаж не може бути від'ємним")));
  }

  const int totalDays = static_cast<int>(from.daysTo(to)) + 1;
  const int employerDays = std::min(totalDays, kSickLeaveEmployerDays);
  const int pfuDays = totalDays - employerDays;

  const Result<Money> avgDaily = computeAvgDailyWage(db, employee.id, from);
  if (!avgDaily) {
    return Result<SickLeaveCalc>::fail(avgDaily.error());
  }

  const std::int64_t dailyCents = avgDaily.value().cents() > 0
                                      ? avgDaily.value().cents()
                                      : (employee.salary.cents() * 10 + 146) / 293;

  const int percent = sickPayPercentByExperience(experienceYears);
  // Нарахування роботодавця: employerDays × dailyCents × percent / 100
  const std::int64_t employerGrossCents = (dailyCents * employerDays * percent + 50) / 100;
  const Money employerGross = Money::fromCents(employerGrossCents);

  PayrollCharge taxes;
  if (employerGross.isPositive()) {
    const Result<PayrollCharge> t =
        computePayroll(employerGross, employee.disabledSsc, false, employee.hasPitBenefit, from);
    if (!t) {
      return Result<SickLeaveCalc>::fail(t.error());
    }
    taxes = t.value();
  }

  SickLeaveCalc calc;
  calc.employeeId = employee.id;
  calc.sickFrom = from;
  calc.sickTo = to;
  calc.totalDays = totalDays;
  calc.employerDays = employerDays;
  calc.pfuDays = pfuDays;
  calc.experienceYears = experienceYears;
  calc.avgDailyCents = Money::fromCents(dailyCents);
  calc.employerGross = employerGross;
  calc.taxes = taxes;
  return Result<SickLeaveCalc>::ok(calc);
}

// Зарплатний рядок

Result<PayrollLine> payrollLineFor(const Employee & employee, QDate monthEnd)
{
  if (!employee.id) {
    return Result<PayrollLine>::fail(Error::validation(QStringLiteral("порожній працівник")));
  }
  if (!employee.hiredOn.isValid() || employee.hiredOn > monthEnd) {
    return Result<PayrollLine>::fail(Error::validation(
        QStringLiteral("%1: дата прийому пізніше місяця нарахування").arg(employee.name)));
  }
  const QDate monthStart(monthEnd.year(), monthEnd.month(), 1);
  const bool fullMonth = employee.hiredOn <= monthStart;
  const Result<PayrollCharge> charge =
      computePayroll(employee.salary, employee.disabledSsc, fullMonth, employee.hasPitBenefit,
                     monthEnd);
  if (!charge) {
    return Result<PayrollLine>::fail(charge.error());
  }
  PayrollLine row;
  row.employeeId = employee.id;
  row.gross = employee.salary;
  row.pit = charge.value().pit;
  row.military = charge.value().military;
  row.ssc = charge.value().ssc;
  row.net = charge.value().net;
  row.name = employee.name;
  return Result<PayrollLine>::ok(row);
}

// Формування проводок

Result<std::vector<DocumentLine>> composePayroll(const std::vector<Account> & accounts,
                                                 const std::vector<PayrollLine> & rows)
{
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  const AccountId salary = accountIdByCode(accounts, chart::kSalaryPayable);
  const AccountId ssc = accountIdByCode(accounts, chart::kSscPayable);
  const AccountId pit = accountIdByCode(accounts, chart::kPitPayable);
  const AccountId military = accountIdByCode(accounts, chart::kMilitaryLevy);
  if (!expense || !salary || !ssc || !pit || !military) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 92, 661, 651, 642 або 645")));
  }
  if (rows.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б одного працівника")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const PayrollLine & row : rows) {
    ++rowNo;
    if (!row.employeeId || !row.gross.isPositive() || !row.net.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: некоректне нарахування").arg(rowNo)));
    }
    const QString who =
        row.name.trimmed().isEmpty() ? QStringLiteral("працівник") : row.name.trimmed();
    lines.push_back(posting(expense, salary, row.gross, row.employeeId,
                            QStringLiteral("Зарплата, %1").arg(who)));
    lines.push_back(
        posting(expense, ssc, row.ssc, row.employeeId, QStringLiteral("ЄСВ, %1").arg(who)));
    lines.push_back(
        posting(salary, pit, row.pit, row.employeeId, QStringLiteral("ПДФО, %1").arg(who)));
    lines.push_back(posting(salary, military, row.military, row.employeeId,
                            QStringLiteral("Військовий збір, %1").arg(who)));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

// Формує проводки нарахування відпускних для одного працівника:
//   Дт 92 — Кт 661 (нараховані відпускні)
//   Дт 92 — Кт 651 (ЄСВ на відпускні)
//   Дт 661 — Кт 642 (утриманий ПДФО)
//   Дт 661 — Кт 645 (утриманий Військовий збір)
Result<std::vector<DocumentLine>> composeVacationPayroll(const std::vector<Account> & accounts,
                                                         const VacationCalc & calc)
{
  if (!calc.employeeId || !calc.gross.isPositive()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("некоректний розрахунок відпускних")));
  }
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  const AccountId salary = accountIdByCode(accounts, chart::kSalaryPayable);
  const AccountId ssc = accountIdByCode(accounts, chart::kSscPayable);
  const AccountId pit = accountIdByCode(accounts, chart::kPitPayable);
  const AccountId military = accountIdByCode(accounts, chart::kMilitaryLevy);
  if (!expense || !salary || !ssc || !pit || !military) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 92, 661, 651, 642 або 645")));
  }
  const QString label = QStringLiteral("Відпускні %1–%2")
                            .arg(calc.vacationFrom.toString(Qt::ISODate))
                            .arg(calc.vacationTo.toString(Qt::ISODate));
  std::vector<DocumentLine> lines;
  lines.push_back(posting(expense, salary, calc.gross, calc.employeeId, label));
  if (calc.taxes.ssc.isPositive()) {
    lines.push_back(
        posting(expense, ssc, calc.taxes.ssc, calc.employeeId, QStringLiteral("ЄСВ на відпускні")));
  }
  if (calc.taxes.pit.isPositive()) {
    lines.push_back(
        posting(salary, pit, calc.taxes.pit, calc.employeeId, QStringLiteral("ПДФО з відпускних")));
  }
  if (calc.taxes.military.isPositive()) {
    lines.push_back(posting(salary, military, calc.taxes.military, calc.employeeId,
                            QStringLiteral("ВЗ з відпускних")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

// Формує проводки нарахування лікарняних (частини роботодавця):
//   Дт 92 — Кт 661, Дт 92 — Кт 651, Дт 661 — Кт 642, Дт 661 — Кт 645
Result<std::vector<DocumentLine>> composeSickLeavePayroll(const std::vector<Account> & accounts,
                                                          const SickLeaveCalc & calc)
{
  if (!calc.employeeId || !calc.employerGross.isPositive()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("некоректний розрахунок лікарняних")));
  }
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  const AccountId salary = accountIdByCode(accounts, chart::kSalaryPayable);
  const AccountId ssc = accountIdByCode(accounts, chart::kSscPayable);
  const AccountId pit = accountIdByCode(accounts, chart::kPitPayable);
  const AccountId military = accountIdByCode(accounts, chart::kMilitaryLevy);
  if (!expense || !salary || !ssc || !pit || !military) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 92, 661, 651, 642 або 645")));
  }
  const QString label = QStringLiteral("Лікарняний %1–%2 (%3 дн. роботодавець)")
                            .arg(calc.sickFrom.toString(Qt::ISODate))
                            .arg(calc.sickTo.toString(Qt::ISODate))
                            .arg(calc.employerDays);
  std::vector<DocumentLine> lines;
  lines.push_back(posting(expense, salary, calc.employerGross, calc.employeeId, label));
  if (calc.taxes.ssc.isPositive()) {
    lines.push_back(
        posting(expense, ssc, calc.taxes.ssc, calc.employeeId, QStringLiteral("ЄСВ на лікарняні")));
  }
  if (calc.taxes.pit.isPositive()) {
    lines.push_back(
        posting(salary, pit, calc.taxes.pit, calc.employeeId, QStringLiteral("ПДФО з лікарняних")));
  }
  if (calc.taxes.military.isPositive()) {
    lines.push_back(posting(salary, military, calc.taxes.military, calc.employeeId,
                            QStringLiteral("ВЗ з лікарняних")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

// Витягування рядків з проводок

std::vector<PayrollLine> extractPayrollLines(const std::vector<DocumentLine> & lines,
                                             const std::vector<Account> & accounts)
{
  QHash<qlonglong, PayrollLine> byEmployee;
  QList<qlonglong> order;
  for (const DocumentLine & line : lines) {
    if (!line.employeeId) {
      continue;
    }
    const qlonglong key = line.employeeId.value;
    if (!byEmployee.contains(key)) {
      PayrollLine row;
      row.employeeId = line.employeeId;
      byEmployee.insert(key, row);
      order.push_back(key);
    }
    PayrollLine & row = byEmployee[key];
    if (accountHasCode(accounts, line.debitAccountId, chart::kAdminExpense) &&
        accountHasCode(accounts, line.creditAccountId, chart::kSalaryPayable)) {
      row.gross = Money::fromCents(row.gross.cents() + line.amount.cents());
    } else if (accountHasCode(accounts, line.debitAccountId, chart::kAdminExpense) &&
               accountHasCode(accounts, line.creditAccountId, chart::kSscPayable)) {
      row.ssc = line.amount;
    } else if (accountHasCode(accounts, line.debitAccountId, chart::kSalaryPayable) &&
               accountHasCode(accounts, line.creditAccountId, chart::kPitPayable)) {
      row.pit = line.amount;
    } else if (accountHasCode(accounts, line.debitAccountId, chart::kSalaryPayable) &&
               accountHasCode(accounts, line.creditAccountId, chart::kMilitaryLevy)) {
      row.military = line.amount;
    }
  }
  std::vector<PayrollLine> rows;
  for (qlonglong key : order) {
    PayrollLine row = byEmployee.value(key);
    if (row.gross.cents() > row.pit.cents() + row.military.cents()) {
      row.net = Money::fromCents(row.gross.cents() - row.pit.cents() - row.military.cents());
    }
    rows.push_back(row);
  }
  return rows;
}

Result<bool> hasPostedPayrollPayment(QSqlDatabase db, DocumentId payrollId)
{
  if (!payrollId) {
    return Result<bool>::ok(false);
  }
  sql::Query query(db);
  Status status = query.prepare(
      QStringLiteral("SELECT id FROM documents WHERE posted = 1 AND based_on_id = :src "
                     "AND type IN ('bank_payment','cash_out') LIMIT 1"));
  if (!status) {
    return Result<bool>::fail(status.error());
  }
  query.bind(QStringLiteral(":src"), static_cast<qlonglong>(payrollId.value));
  status = query.exec();
  if (!status) {
    return Result<bool>::fail(status.error());
  }
  return Result<bool>::ok(query.next());
}

} // namespace fin
