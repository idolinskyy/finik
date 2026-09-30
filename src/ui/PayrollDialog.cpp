#include "ui/PayrollDialog.h"

#include "ui/HelpBrowser.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"
#include "fin/print/PrintForms.h"

#include <QAbstractItemView>
#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {

PayrollDialog::PayrollDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(DocumentType::Payroll));
  resize(860, 520);
  setWhatsThis(QStringLiteral("Нарахування зарплати: Дт 92 Кт 661, ЄСВ 92/651, ПДФО 661/642, "
                              "військовий збір 661/645. Виплата — «На підставі» на 661."));

  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
  }
  if (!document_.date.isValid()) {
    const QDate today = QDate::currentDate();
    document_.date = QDate(today.year(), today.month(), today.daysInMonth());
  }

  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date);
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Номер"), number_);
  form->addRow(QStringLiteral("Дата (місяць)"), date_);

  table_ = new QTableWidget;
  table_->setColumnCount(6);
  table_->setHorizontalHeaderLabels({QStringLiteral("Працівник"), QStringLiteral("Нараховано"),
                                     QStringLiteral("ПДФО"), QStringLiteral("ВЗ"),
                                     QStringLiteral("ЄСВ"), QStringLiteral("До виплати")});
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  fillTable();

  comment_ = new QTextEdit(document_.comment);
  comment_->setAcceptRichText(false);
  comment_->setMaximumHeight(70);
  reason_ = new QLabel;
  reason_->setWordWrap(true);
  auto * hint = new QLabel(
      QStringLiteral("Ставки 2026: ПДФО 18%, військовий збір 5%, ЄСВ 22% (8.41% для осіб з "
                     "інвалідністю). База ЄСВ — від мінімальної зарплати до 20 мінімальних. "
                     "ПСП застосовується, якщо у картці працівника увімкнено пільгу і "
                     "нараховано ≤ 3 540 грн. Один проведений документ на місяць."));
  hint->setWordWrap(true);

  fillBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/users.svg")),
                             QStringLiteral("Заповнити з довідника"));
  saveBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                             QStringLiteral("Зберегти"));
  postBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                             QStringLiteral("Провести"));
  unpostBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")),
                               QStringLiteral("Зняти проведення"));
  printBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                              QStringLiteral("Друк П-6"));
  printSlipsBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                                   QStringLiteral("Розрахункові листки"));
  auto * help = new QPushButton(QStringLiteral("Довідка"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  buttons->addWidget(fillBtn_);
  buttons->addWidget(saveBtn_);
  buttons->addWidget(postBtn_);
  buttons->addWidget(unpostBtn_);
  buttons->addWidget(printBtn_);
  buttons->addWidget(printSlipsBtn_);
  buttons->addWidget(help);
  buttons->addStretch();
  buttons->addWidget(close);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addWidget(table_, 1);
  layout->addWidget(new QLabel(QStringLiteral("Коментар")));
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(buttons);

  connect(fillBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = fillFromDirectory();
    if (!status) {
      ui::showError(this, status);
    }
    refreshGate();
  });
  connect(saveBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = this->save();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Збережено"));
    }
    refreshGate();
  });
  connect(postBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = saveAndPost();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Проведено"));
    }
    refreshGate();
  });
  connect(unpostBtn_, &QPushButton::clicked, this, &PayrollDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &PayrollDialog::printForm);
  connect(printSlipsBtn_, &QPushButton::clicked, this, &PayrollDialog::printPayslips);
  connect(help, &QPushButton::clicked, this, &PayrollDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &PayrollDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &PayrollDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &PayrollDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

void PayrollDialog::fillTable()
{
  table_->setRowCount(0);
  const std::vector<PayrollLine> rows = extractPayrollLines(document_.lines, accounts_);
  for (const PayrollLine & row : rows) {
    QString name = row.name;
    if (name.isEmpty() && row.employeeId) {
      const Result<Employee> employee = session_.employees().findById(row.employeeId);
      if (employee) {
        name = employee.value().name;
      }
    }
    const int i = table_->rowCount();
    table_->insertRow(i);
    table_->setItem(i, 0, new QTableWidgetItem(name));
    auto addAmt = [this, i](int col, Money amount) {
      auto * cell = new QTableWidgetItem(amount.toDisplay());
      cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      table_->setItem(i, col, cell);
    };
    addAmt(1, row.gross);
    addAmt(2, row.pit);
    addAmt(3, row.military);
    addAmt(4, row.ssc);
    addAmt(5, row.net);
  }
}

Status PayrollDialog::fillFromDirectory()
{
  if (document_.posted) {
    return Status::fail(Error::alreadyPosted(QStringLiteral("проведене нарахування не змінюють")));
  }
  const Result<std::vector<Employee>> staff = session_.employees().listActive();
  if (!staff) {
    return staff.asStatus();
  }
  const QDate monthEnd(date_->date().year(), date_->date().month(), date_->date().daysInMonth());
  std::vector<PayrollLine> rows;
  for (const Employee & employee : staff.value()) {
    if (employee.hiredOn > monthEnd) {
      continue;
    }
    const Result<PayrollLine> row = payrollLineFor(employee, monthEnd);
    if (!row) {
      return row.asStatus();
    }
    rows.push_back(row.value());
  }
  const Result<std::vector<DocumentLine>> lines = composePayroll(accounts_, rows);
  if (!lines) {
    return lines.asStatus();
  }
  document_.lines = lines.value();
  document_.date = monthEnd;
  date_->setDate(monthEnd);
  fillTable();
  return Status::ok();
}

Status PayrollDialog::collect(Document & out) const
{
  out = document_;
  out.type = DocumentType::Payroll;
  out.number = number_->text().trimmed();
  out.date = date_->date();
  out.comment = comment_->toPlainText().trimmed();
  out.counterpartyId = {};
  return Status::ok();
}

Status PayrollDialog::save()
{
  if (document_.lines.empty()) {
    const Status filled = fillFromDirectory();
    if (!filled) {
      return filled;
    }
  }
  Document collected;
  Status status = collect(collected);
  if (!status) {
    return status;
  }
  if (!collected.id) {
    Result<DocumentId> id = session_.documents().insert(collected);
    if (!id) {
      return id.asStatus();
    }
    document_ = collected;
    document_.id = id.value();
  } else {
    status = session_.documents().update(collected);
    if (!status) {
      return status;
    }
    document_ = collected;
  }
  persisted_ = true;
  updatePostedUi();
  return Status::ok();
}

Status PayrollDialog::saveAndPost()
{
  Status status = save();
  if (!status) {
    return status;
  }
  status = session_.posting().post(document_.id);
  if (!status) {
    return status;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  } else {
    document_.posted = true;
  }
  persisted_ = true;
  fillTable();
  updatePostedUi();
  return Status::ok();
}

void PayrollDialog::unpost()
{
  if (!document_.id) {
    return;
  }
  const Status status = session_.posting().unpost(document_.id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  } else {
    document_.posted = false;
  }
  persisted_ = true;
  fillTable();
  updatePostedUi();
  refreshGate();
}

void PayrollDialog::printForm()
{
  Status status = Status::ok();
  if (!document_.id) {
    status = save();
    if (!status) {
      ui::showError(this, status);
      return;
    }
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (!loaded) {
    ui::showError(this, loaded.error());
    return;
  }
  const Result<QString> html = PrintForms(session_.handle()).documentHtml(loaded.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  ui::printHtml(this, QStringLiteral("Відомість П-6 %1").arg(loaded.value().number), html.value());
}

void PayrollDialog::printPayslips()
{
  Status status = Status::ok();
  if (!document_.id) {
    status = save();
    if (!status) {
      ui::showError(this, status);
      return;
    }
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (!loaded) {
    ui::showError(this, loaded.error());
    return;
  }
  std::vector<PayrollLine> rows = extractPayrollLines(loaded.value().lines, accounts_);
  if (rows.empty()) {
    QMessageBox::information(this, QStringLiteral("Друк"),
                             QStringLiteral("Немає рядків для розрахункових листків."));
    return;
  }
  PrintForms forms(session_.handle());
  QString combined;
  for (PayrollLine & row : rows) {
    Result<Employee> emp = Result<Employee>::fail(Error::notFound(QStringLiteral("працівник")));
    if (row.employeeId) {
      emp = session_.employees().findById(row.employeeId);
    }
    Employee stub;
    if (emp) {
      stub = emp.value();
      if (row.name.isEmpty()) {
        row.name = stub.name;
      }
    } else if (!row.name.isEmpty()) {
      stub.name = row.name;
    }
    const Result<QString> html = forms.payslipHtml(row, stub, loaded.value().date);
    if (!html) {
      ui::showError(this, html.error());
      return;
    }
    if (!combined.isEmpty()) {
      combined += QStringLiteral("<div style=\"page-break-after: always;\"></div>");
    }
    combined += html.value();
  }
  ui::printHtml(this, QStringLiteral("Розрахункові листки %1").arg(loaded.value().number),
                combined);
}

void PayrollDialog::updatePostedUi()
{
  const bool posted = document_.posted;
  number_->setEnabled(!posted);
  date_->setEnabled(!posted);
  comment_->setEnabled(!posted);
  fillBtn_->setEnabled(!posted);
}

ui::FormButtons PayrollDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

void PayrollDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, document_.date,
                             [this](Document & out) { return collect(out); });
}

void PayrollDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("payroll.html"));
}

} // namespace fin
