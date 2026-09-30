#include "ui/VacationDialog.h"

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
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {

VacationDialog::VacationDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(QStringLiteral("Нарахування відпускних"));
  resize(640, 560);

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
  toDate_ = new QDateEdit(document_.date.addDays(13));
  toDate_->setCalendarPopup(true);
  toDate_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  form->addRow(QStringLiteral("Відпустка з:"), fromDate_);
  form->addRow(QStringLiteral("Відпустка по:"), toDate_);

  layout->addLayout(form);

  hint_ = new QLabel(
      QStringLiteral("Період відпустки вказується з наказу / заяви (календарні дні). "
                     "Табель (П-5) — окремий облік явок; він не підставляє дати сюди. "
                     "Середньоденний заробіток рахується за КМУ № 100 з уже проведених нарахувань "
                     "за 12 місяців."));
  hint_->setWordWrap(true);
  hint_->setObjectName(QStringLiteral("muted"));
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
                              QStringLiteral("Друк наказу"));
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

  connect(computeBtn_, &QPushButton::clicked, this, &VacationDialog::compute);
  connect(saveBtn_, &QPushButton::clicked, this, &VacationDialog::saveAndPost);
  connect(unpostBtn_, &QPushButton::clicked, this, &VacationDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &VacationDialog::printForm);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  connect(employeeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &VacationDialog::refreshTimesheetHint);
  connect(fromDate_, &QDateEdit::dateChanged, this, &VacationDialog::refreshTimesheetHint);

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
                       "і надрукувати наказ."));
  }

  refreshTimesheetHint();
  updateUi();
}

void VacationDialog::refreshTimesheetHint()
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
    if (row.employeeId == empId && row.vacationDays > 0) {
      timesheetHint_->setText(
          QStringLiteral("У табелі за %1.%2 для цього працівника зазначено %3 дн. відпустки "
                         "(довідково; дати відпустки все одно вкажіть вище).")
              .arg(month, 2, 10, QLatin1Char('0'))
              .arg(year)
              .arg(row.vacationDays));
      return;
    }
  }
  timesheetHint_->setText(
      QStringLiteral("У табелі за %1.%2 днів відпустки для працівника не зазначено "
                     "(розділ «Табель»).")
          .arg(month, 2, 10, QLatin1Char('0'))
          .arg(year));
}

void VacationDialog::updateUi()
{
  const bool posted = document_.posted;
  docNumber_->setEnabled(!posted);
  docDate_->setEnabled(!posted);
  employeeCombo_->setEnabled(!posted);
  fromDate_->setEnabled(!posted);
  toDate_->setEnabled(!posted);
  computeBtn_->setEnabled(!posted);
  saveBtn_->setEnabled(!posted);
  unpostBtn_->setVisible(posted);
  printBtn_->setEnabled(lastCalc_.has_value() && lastEmployee_.has_value());
}

void VacationDialog::compute()
{
  if (employeeCombo_->currentIndex() < 0) {
    QMessageBox::warning(this, QStringLiteral("Відпускні"),
                         QStringLiteral("Оберіть працівника."));
    return;
  }
  const EmployeeId empId{employeeCombo_->currentData().toLongLong()};
  Result<Employee> emp = session_.employees().findById(empId);
  if (!emp) {
    ui::showError(this, emp.error());
    return;
  }

  Result<VacationCalc> calc =
      computeVacation(session_.handle(), emp.value(), fromDate_->date(), toDate_->date());
  if (!calc) {
    ui::showError(this, calc.error());
    return;
  }

  Result<std::vector<DocumentLine>> lines = composeVacationPayroll(accounts_, calc.value());
  if (!lines) {
    ui::showError(this, lines.error());
    return;
  }
  document_.lines = lines.value();
  document_.type = DocumentType::VacationPayroll;
  lastCalc_ = calc.value();
  lastEmployee_ = emp.value();

  const Money net = calc.value().taxes.net;
  resultText_->setText(
      QStringLiteral(
          "Період: %1 — %2 (%3 кал. дн.)\n"
          "Середньоденний (12 міс., КМУ № 100): %4\n"
          "Нараховано брутто: %5\n"
          "ЄСВ: %6\n"
          "ПДФО: %7\n"
          "Військовий збір: %8\n"
          "До виплати: %9")
          .arg(fromDate_->date().toString(QStringLiteral("dd.MM.yyyy")),
               toDate_->date().toString(QStringLiteral("dd.MM.yyyy")),
               QString::number(calc.value().vacationDays), calc.value().avgDailyCents.toDisplay(),
               calc.value().gross.toDisplay(), calc.value().taxes.ssc.toDisplay(),
               calc.value().taxes.pit.toDisplay(), calc.value().taxes.military.toDisplay(),
               net.toDisplay()));
  updateUi();
}

void VacationDialog::saveAndPost()
{
  if (document_.lines.empty()) {
    QMessageBox::warning(this, QStringLiteral("Відпускні"),
                         QStringLiteral("Спочатку натисніть «Розрахувати»."));
    return;
  }
  document_.number = docNumber_->text().trimmed();
  document_.date = docDate_->date();
  document_.type = DocumentType::VacationPayroll;

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

void VacationDialog::unpost()
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

void VacationDialog::printForm()
{
  if (!lastCalc_ || !lastEmployee_) {
    QMessageBox::warning(this, QStringLiteral("Друк"),
                         QStringLiteral("Спочатку натисніть «Розрахувати»."));
    return;
  }
  Result<QString> html =
      PrintForms(session_.handle()).vacationOrderHtml(lastCalc_.value(), lastEmployee_.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  ui::printHtml(this, QStringLiteral("Наказ про відпустку"), html.value());
}

} // namespace fin
