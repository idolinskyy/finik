#pragma once

// Кадри та зарплата: накази, табель, відпускні, лікарняні.
// Норми (2026): мінЗП 864700 коп.; ПДФО 18%; ВЗ 5%; ЄСВ 22% / 8.41%;
// ПСП 134000 коп. (поріг 354000); лікарняні: 5 днів роботодавець, далі ПФУ;
// стаж: <3р 50%, 3–5 60%, 5–10 70%, ≥10 100%. Середній заробіток — КМУ № 100.

#include "fin/core/Ids.h"
#include "fin/core/Money.h"
#include "fin/core/Result.h"
#include "fin/domain/Account.h"
#include "fin/domain/Document.h"

#include <optional>
#include <vector>

#include <QDate>
#include <QSqlDatabase>
#include <QString>

namespace fin {

inline constexpr std::int64_t kMinWageCents2026 = 864700; // мінзарплата 2026 р.
inline constexpr int kSscMaxBaseMonths = 20; // максимальна база ЄСВ (20 МЗП)
inline constexpr int kPitRateBp = 1800;      // ПДФО 18%
inline constexpr int kMilitaryRateBp = 500;  // Військовий збір 5%
inline constexpr int kSscRateBp = 2200;      // ЄСВ 22%
inline constexpr int kSscDisabledRateBp = 841; // ЄСВ для осіб з інвалідністю 8.41%
inline constexpr std::int64_t kPitBenefitCents2026 = 134000;          // ПСП 1 340 грн
inline constexpr std::int64_t kPitBenefitThresholdCents2026 = 354000; // поріг ПСП 3 540 грн

// Максимальна кількість календарних днів лікарняного за рахунок роботодавця
inline constexpr int kSickLeaveEmployerDays = 5;

struct Employee
{
  EmployeeId id;
  QString taxId;                // РНОКПП (10 цифр)
  QString name;                 // ПІБ працівника
  QDate hiredOn;                // дата прийняття на роботу
  Money salary = Money::zero(); // посадовий оклад
  bool disabledSsc = false; // ознака особи з інвалідністю (пільгова ставка ЄСВ 8.41%)
  bool isActive = true; // статус зайнятості (true = працює, false = звільнений)
  QString position;   // посада (текстова назва)
  QString department; // підрозділ (текстова назва)
  QDate firedOn; // дата звільнення (невалідна = не звільнений)
  bool hasPitBenefit = false; // право на ПСП (п. 169.1 ПКУ): зменшує базу ПДФО
};

// Тип кадрового наказу
enum class HrOrderType
{
  Hire,     // прийом на роботу
  Transfer, // кадрове переведення (зміна посади / підрозділу / окладу)
  Dismiss,  // звільнення
};
QString hrOrderTypeString(HrOrderType type); // 'hire'/'transfer'/'dismiss'
QString hrOrderTypeLabelUk(HrOrderType type); // «Прийом»/«Переведення»/«Звільнення»
std::optional<HrOrderType> hrOrderTypeFromString(const QString & s);

// Кадровий наказ; пов'язаний з документом типу Manual (службовим).
// Зберігається у таблиці hr_orders, документ у documents використовується
// як контейнер дати, номера та стану підпису.
struct HrOrder
{
  HrOrderId id;
  DocumentId documentId;
  EmployeeId employeeId;
  HrOrderType orderType = HrOrderType::Hire;
  QDate effectiveDate;             // дата набрання чинності наказу
  QString newPosition;             // нова посада (Transfer)
  QString newDepartment;           // новий підрозділ (Transfer)
  Money newSalary = Money::zero(); // новий оклад (Transfer; zero = без змін)
  QString dismissReason;           // підстава звільнення (Transfer/Dismiss)
};

// Рядок табеля обліку робочого часу (форма П-5) за один місяць
struct TimesheetRow
{
  EmployeeId employeeId;
  QString employeeName;
  int daysWorked = 0;   // фактично відпрацьовано днів
  int hoursWorked = 0;  // фактично відпрацьовано годин
  int vacationDays = 0; // календарних днів відпустки
  int sickDays = 0;     // календарних днів лікарняного
  int absenceDays = 0;  // інші неявки (прогули, без збереження)
  int workingDaysNorm = 0; // норма робочих днів у місяці (виробничий календар)
};

// Табель (форма П-5) за місяць
struct Timesheet
{
  int year = 0;
  int month = 0;
  std::vector<TimesheetRow> rows;
};

// Результат розрахунку податків та внесків на одну суму нарахування
struct PayrollCharge
{
  Money pit = Money::zero(); // ПДФО (18% від бази після вирахування ПСП)
  Money military = Money::zero(); // Військовий збір (5%)
  Money ssc = Money::zero();      // ЄСВ (22% або 8.41%)
  Money net = Money::zero();      // до виплати (gross – pit – military)
  Money sscBase = Money::zero();  // фактична база ЄСВ (після обмежень)
  Money pitBase = Money::zero(); // фактична база ПДФО (після вирахування ПСП)
  Money pitBenefit = Money::zero(); // застосована сума ПСП
};

// Розрахунковий рядок зарплатної відомості по співробітнику
struct PayrollLine
{
  EmployeeId employeeId;
  Money gross = Money::zero();    // нарахований оклад (брутто)
  Money pit = Money::zero();      // утриманий ПДФО
  Money military = Money::zero(); // утриманий Військовий збір
  Money ssc = Money::zero();      // нарахований ЄСВ
  Money net = Money::zero();      // сума до виплати на руки
  QString name;
};

// Результат розрахунку відпускних (постанова КМУ № 100)
struct VacationCalc
{
  EmployeeId employeeId;
  QDate vacationFrom;
  QDate vacationTo;
  int vacationDays = 0;
  Money avgDailyCents = Money::zero(); // середньоденний заробіток за 12 міс.
  Money gross = Money::zero();         // = vacationDays * avgDailyCents
  PayrollCharge taxes;
};

// Результат розрахунку лікарняних
struct SickLeaveCalc
{
  EmployeeId employeeId;
  QDate sickFrom;
  QDate sickTo;
  int totalDays = 0;
  int employerDays = 0; // перші 5 — роботодавець
  int pfuDays = 0; // решта — ПФУ (не відображається у проводках)
  int experienceYears = 0;
  Money avgDailyCents = Money::zero();
  Money employerGross = Money::zero(); // нарахована підприємством частина
  PayrollCharge taxes;
};

bool usesPayrollForm(DocumentType type);
// Payroll + відпускні + лікарняні (друк, довідка, родина документів).
bool isPayrollFamily(DocumentType type);
Money payrollNetPayable(const std::vector<DocumentLine> & lines,
                        const std::vector<Account> & accounts);

// Ключові розрахункові функції

// Розраховує податки/внески за ставками з урахуванням:
//   – мінімальної/максимальної бази ЄСВ;
//   – ПСП (якщо gross ≤ kPitBenefitThresholdCents2026 та hasPitBenefit = true).
Result<PayrollCharge> computePayroll(Money gross, bool disabledSsc, bool fullMonth,
                                     bool hasPitBenefit = false);

// Норма робочих днів у місяці (стандартний 5-денний тиждень, спрощений алгоритм).
// Рахується без урахування державних свят (для повноцінного виробничого
// календаря потрібна окрема таблиця свят).
int workingDaysInMonth(int year, int month);

// Відсоток оплати лікарняних від страхового стажу (ч. 4 ст. 19 Закону):
//   < 3 р.  → 50%; 3–5 р. → 60%; 5–10 р. → 70%; ≥ 10 р. → 100%.
int sickPayPercentByExperience(int experienceYears);

// Середньоденний заробіток за останні 12 місяців (КМУ № 100):
//   Береться сума всіх нарахувань Payroll/VacationPayroll/SickLeavePayroll
//   за 12 місяців до calcDate, ділена на кількість calendar днів цього
//   12-місячного діапазону мінус дні без збереження заробітної плати.
Result<Money> computeAvgDailyWage(QSqlDatabase db, EmployeeId id, QDate calcDate);

// Розраховує відпускні для працівника за заданий діапазон дат.
// Обчислює кількість календарних днів відпустки, avgDailyWage та суму нарахувань.
Result<VacationCalc> computeVacation(QSqlDatabase db, const Employee & employee, QDate from,
                                     QDate to);

// Розраховує лікарняні: частину роботодавця та (інформаційно) частину ПФУ.
Result<SickLeaveCalc> computeSickLeave(QSqlDatabase db, const Employee & employee, QDate from,
                                       QDate to, int experienceYears);

// Генерує зарплатний рядок для працівника на кінець звітного місяця
Result<PayrollLine> payrollLineFor(const Employee & employee, QDate monthEnd);

// Трансформує зарплатні рядки у бухгалтерські проводки:
//   Дт 92 Кт 661 (оклад), Дт 92 Кт 651 (ЄСВ),
//   Дт 661 Кт 642 (ПДФО), Дт 661 Кт 645 (ВЗ)
Result<std::vector<DocumentLine>> composePayroll(const std::vector<Account> & accounts,
                                                 const std::vector<PayrollLine> & rows);

// Формує проводки для нарахування відпускних:
//   Дт 92 Кт 661 (нарахування відпускних),
//   Дт 92 Кт 651 (ЄСВ), Дт 661 Кт 642 (ПДФО), Дт 661 Кт 645 (ВЗ)
Result<std::vector<DocumentLine>> composeVacationPayroll(const std::vector<Account> & accounts,
                                                         const VacationCalc & calc);

// Формує проводки для нарахування лікарняних (частини роботодавця):
//   Дт 92 Кт 661, Дт 92 Кт 651, Дт 661 Кт 642, Дт 661 Кт 645
Result<std::vector<DocumentLine>> composeSickLeavePayroll(const std::vector<Account> & accounts,
                                                          const SickLeaveCalc & calc);

// Витягує структуровані зарплатні рядки з проводок документа Payroll
std::vector<PayrollLine> extractPayrollLines(const std::vector<DocumentLine> & lines,
                                             const std::vector<Account> & accounts);

// Перевіряє наявність проведених виплат, створених «на підставі» цієї відомості
Result<bool> hasPostedPayrollPayment(QSqlDatabase db, DocumentId payrollId);

} // namespace fin
