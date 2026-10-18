#include "ui/SickLeaveDialog.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/print/PrintForms.h"

#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {

SickLeaveDialog::SickLeaveDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(QStringLiteral("Нарахування лікарняних"));
  resize(640, 580);

  auto * layout = new QVBoxLayout(this);
  auto * form = new QFormLayout;

  docNumber_ = new QLineEdit(document_.number);
  docDate_ = new QDateEdit(document_.date);
  docDate_->setCalendarPopup(true);
  docDate_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  form->addRow(QStringLiteral("Номер документа:"), docNumber_);
  form->addRow(QStringLiteral("Дата документа:"), docDate_);

  employeeCombo_ = new QComboBox;
  const Result<std::vector<Employee>> emps = session_.employees().listActive();
  if (emps) {
    employees_ = emps.value();
    for (const Employee & e : employees_) {
      employeeCombo_->addItem(e.name, QVariant::fromValue(static_cast<qlonglong>(e.id.value)));
    }
  }
  form->addRow(QStringLiteral("Працівник:"), employeeCombo_);

  fromDate_ = new QDateEdit(document_.date);
  fromDate_->setCalendarPopup(true);
  fromDate_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  toDate_ = new QDateEdit(document_.date.addDays(4));
  toDate_->setCalendarPopup(true);
  toDate_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  form->addRow(QStringLiteral("Лікарняний з:"), fromDate_);
  form->addRow(QStringLiteral("Лікарняний по:"), toDate_);

  experienceSpinBox_ = new QSpinBox;
  experienceSpinBox_->setRange(0, 50);
  experienceSpinBox_->setSuffix(QStringLiteral(" років"));
  form->addRow(QStringLiteral("Страховий стаж:"), experienceSpinBox_);

  layout->addLayout(form);

  hint_ = new QLabel(
      QStringLiteral("Період непрацездатності — з листка непрацездатності. "
                     "Табель (П-5) лише фіксує кількість днів «ТН» і не підставляє дати сюди. "
                     "У проводки потрапляють перші 5 календарних днів (роботодавець); решта — "
                     "довідково як ПФУ."));
  hint_->setWordWrap(true);
  layout->addWidget(hint_);

  timesheetHint_ = new QLabel;
  timesheetHint_->setWordWrap(true);
  layout->addWidget(timesheetHint_);

  resultText_ = new QTextEdit;
  resultText_->setReadOnly(true);
  layout->addWidget(resultText_, 1);

  auto * btns = new QHBoxLayout;
  computeBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                QStringLiteral("Розрахувати"));
  printBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                              QStringLiteral("Друк розрахунку"));
  unpostBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")),
                               QStringLiteral("Скасувати проведення"));
  saveBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                             QStringLiteral("Зберегти та провести"));
  saveBtn_->setDefault(true);
  auto * cancelBtn = new QPushButton(QStringLiteral("Закрити"));

  btns->addWidget(computeBtn_);
  btns->addWidget(printBtn_);
  btns->addWidget(unpostBtn_);
  btns->addStretch();
  btns->addWidget(saveBtn_);
  btns->addWidget(cancelBtn);
  layout->addLayout(btns);

  connect(computeBtn_, &QPushButton::clicked, this, &SickLeaveDialog::compute);
  connect(saveBtn_, &QPushButton::clicked, this, &SickLeaveDialog::saveAndPost);
  connect(unpostBtn_, &QPushButton::clicked, this, &SickLeaveDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &SickLeaveDialog::printForm);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  connect(employeeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &SickLeaveDialog::refreshTimesheetHint);
  connect(fromDate_, &QDateEdit::dateChanged, this, &SickLeaveDialog::refreshTimesheetHint);

  Result<std::vector<Account>> accs = session_.accounts().listPostable();
  if (accs) {
    accounts_ = accs.value();
  }

  if (document_.id && !document_.lines.empty()) {
    const DocumentLine & line = document_.lines.front();
    if (line.employeeId) {
      const int empIdx =
          employeeCombo_->findData(static_cast<qlonglong>(line.employeeId.value));
      if (empIdx >= 0) {
        employeeCombo_->setCurrentIndex(empIdx);
      }
    }
    resultText_->setText(
        QStringLiteral("Документ завантажено. Натисніть «Розрахувати», щоб оновити суми "
                       "і надрукувати розрахунок."));
  }

  refreshTimesheetHint();
  updateUi();
}

void SickLeaveDialog::refreshTimesheetHint()
{
  timesheetHint_->clear();
  if (employeeCombo_->currentIndex() < 0) {
    return;
  }
  const EmployeeId empId{employeeCombo_->currentData().toLongLong()};
  const int year = fromDate_->date().year();
  const int month = fromDate_->date().month();
  Result<Timesheet> ts = session_.timesheets().findOrCreate(year, month);
  if (!ts) {
    return;
  }
  for (const TimesheetRow & row : ts.value().rows) {
    if (row.employeeId == empId && row.sickDays > 0) {
      timesheetHint_->setText(
          QStringLiteral("У табелі за %1.%2 для цього працівника зазначено %3 дн. лікарняного "
                         "(довідково; період з листка вкажіть вище).")
              .arg(month, 2, 10, QLatin1Char('0'))
              .arg(year)
              .arg(row.sickDays));
      return;
    }
  }
  timesheetHint_->setText(
      QStringLiteral("У табелі за %1.%2 днів лікарняного для працівника не зазначено "
                     "(розділ «Табель»).")
          .arg(month, 2, 10, QLatin1Char('0'))
          .arg(year));
}

void SickLeaveDialog::updateUi()
{
  const bool posted = document_.posted;
  docNumber_->setEnabled(!posted);
  docDate_->setEnabled(!posted);
  employeeCombo_->setEnabled(!posted);
  fromDate_->setEnabled(!posted);
  toDate_->setEnabled(!posted);
  experienceSpinBox_->setEnabled(!posted);
  computeBtn_->setEnabled(!posted);
  saveBtn_->setEnabled(!posted);
  unpostBtn_->setVisible(posted);
  printBtn_->setEnabled(lastCalc_.has_value() && lastEmployee_.has_value());
}

void SickLeaveDialog::compute()
{
  if (employeeCombo_->currentIndex() < 0) {
    QMessageBox::warning(this, QStringLiteral("Лікарняні"),
                         QStringLiteral("Оберіть працівника."));
    return;
  }
  const EmployeeId empId{employeeCombo_->currentData().toLongLong()};
  Result<Employee> emp = session_.employees().findById(empId);
  if (!emp) {
    ui::showError(this, emp.error());
    return;
  }

  Result<SickLeaveCalc> calc = computeSickLeave(session_.handle(), emp.value(), fromDate_->date(),
                                                toDate_->date(), experienceSpinBox_->value());
  if (!calc) {
    ui::showError(this, calc.error());
    return;
  }

  Result<std::vector<DocumentLine>> lines = composeSickLeavePayroll(accounts_, calc.value());
  if (!lines) {
    ui::showError(this, lines.error());
    return;
  }
  document_.lines = lines.value();
  document_.type = DocumentType::SickLeavePayroll;
  lastCalc_ = calc.value();
  lastEmployee_ = emp.value();

  resultText_->setText(
      QStringLiteral(
          "Період: %1 — %2\n"
          "Днів усього: %3 (роботодавець: %4, ПФУ: %5)\n"
          "Страховий стаж: %6 р. → %7% оплати\n"
          "Середньоденний: %8\n"
          "Нараховано роботодавцем (брутто): %9\n"
          "ЄСВ: %10\n"
          "ПДФО: %11\n"
          "Військовий збір: %12\n"
          "До виплати (частина роботодавця): %13")
          .arg(fromDate_->date().toString(QStringLiteral("dd.MM.yyyy")),
               toDate_->date().toString(QStringLiteral("dd.MM.yyyy")),
               QString::number(calc.value().totalDays), QString::number(calc.value().employerDays),
               QString::number(calc.value().pfuDays), QString::number(calc.value().experienceYears),
               QString::number(sickPayPercentByExperience(calc.value().experienceYears)),
               calc.value().avgDailyCents.toDisplay(), calc.value().employerGross.toDisplay(),
               calc.value().taxes.ssc.toDisplay(), calc.value().taxes.pit.toDisplay(),
               calc.value().taxes.military.toDisplay(), calc.value().taxes.net.toDisplay()));
  updateUi();
}

void SickLeaveDialog::saveAndPost()
{
  if (document_.lines.empty()) {
    QMessageBox::warning(this, QStringLiteral("Лікарняні"),
                         QStringLiteral("Спочатку натисніть «Розрахувати»."));
    return;
  }
  document_.number = docNumber_->text().trimmed();
  document_.date = docDate_->date();
  document_.type = DocumentType::SickLeavePayroll;

  if (!document_.id) {
    Result<DocumentId> docId = session_.documents().insert(document_);
    if (!docId) {
      ui::showError(this, docId.error());
      return;
    }
    document_.id = docId.value();
  } else {
    Status status = session_.documents().update(document_);
    if (!status) {
      ui::showError(this, status);
      return;
    }
  }

  Status status = session_.posting().post(document_.id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  document_.posted = true;
  persisted_ = true;
  updateUi();
  ui::showToast(this, QStringLiteral("Документ проведено."));
}

void SickLeaveDialog::unpost()
{
  Status status = session_.posting().unpost(document_.id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  document_.posted = false;
  persisted_ = true;
  updateUi();
}

void SickLeaveDialog::printForm()
{
  if (!lastCalc_ || !lastEmployee_) {
    QMessageBox::warning(this, QStringLiteral("Друк"),
                         QStringLiteral("Спочатку натисніть «Розрахувати»."));
    return;
  }
  Result<QString> html =
      PrintForms(session_.handle()).sickLeaveCalcHtml(lastCalc_.value(), lastEmployee_.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  ui::printHtml(this, QStringLiteral("Розрахунок лікарняних"), html.value());
}

} // namespace fin
