#include "ui/TimesheetPage.h"

#include "fin/auth/Access.h"
#include "fin/db/TimesheetRepository.h"
#include "fin/domain/Employee.h"
#include "fin/print/PrintForms.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QItemDelegate>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {
namespace {

enum Columns
{
  kColId = 0,
  kColName,
  kColDaysWorked,
  kColHoursWorked,
  kColVacationDays,
  kColSickDays,
  kColCount
};

class IntegerDelegate : public QItemDelegate
{
 public:
  QWidget * createEditor(QWidget * parent, const QStyleOptionViewItem & /*option*/,
                         const QModelIndex & /*index*/) const override
  {
    QLineEdit * editor = new QLineEdit(parent);
    editor->setValidator(new QIntValidator(0, 31, editor));
    return editor;
  }
};

} // namespace

TimesheetPage::TimesheetPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);

  auto * top = new QHBoxLayout;
  top->addWidget(new QLabel(QStringLiteral("Місяць:")));
  monthCombo_ = new QComboBox;
  const QStringList months = {
      QStringLiteral("Січень"),  QStringLiteral("Лютий"),    QStringLiteral("Березень"),
      QStringLiteral("Квітень"), QStringLiteral("Травень"),  QStringLiteral("Червень"),
      QStringLiteral("Липень"),  QStringLiteral("Серпень"),  QStringLiteral("Вересень"),
      QStringLiteral("Жовтень"), QStringLiteral("Листопад"), QStringLiteral("Грудень")};
  monthCombo_->addItems(months);
  const QDate today = QDate::currentDate();
  monthCombo_->setCurrentIndex(today.month() - 1);
  top->addWidget(monthCombo_);

  top->addWidget(new QLabel(QStringLiteral("Рік:")));
  yearSpinBox_ = new QSpinBox;
  yearSpinBox_->setRange(2020, 2100);
  yearSpinBox_->setValue(today.year());
  top->addWidget(yearSpinBox_);

  printBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                              QStringLiteral("Друк (П-5)"));
  top->addStretch();
  top->addWidget(printBtn_);
  layout->addLayout(top);

  table_ = new QTableWidget(0, kColCount);
  table_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Працівник"),
                                     QStringLiteral("Робочі дні"), QStringLiteral("Робочі години"),
                                     QStringLiteral("Відпустка (дні)"),
                                     QStringLiteral("Лікарняні (дні)")});
  table_->hideColumn(kColId);
  table_->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
  table_->setItemDelegateForColumn(kColDaysWorked, new IntegerDelegate);
  table_->setItemDelegateForColumn(kColHoursWorked, new IntegerDelegate);
  table_->setItemDelegateForColumn(kColVacationDays, new IntegerDelegate);
  table_->setItemDelegateForColumn(kColSickDays, new IntegerDelegate);

  layout->addWidget(table_);

  connect(monthCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &TimesheetPage::reload);
  connect(yearSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged), this, &TimesheetPage::reload);
  connect(printBtn_, &QPushButton::clicked, this, &TimesheetPage::printTimesheet);
  connect(table_, &QTableWidget::cellChanged, this, [this](int row, int col) {
    if (col >= kColDaysWorked && col <= kColSickDays) {
      saveRow(row);
    }
  });
  reload();
}

void TimesheetPage::reload()
{
  table_->blockSignals(true);
  table_->setRowCount(0);

  const int year = yearSpinBox_->value();
  const int month = monthCombo_->currentIndex() + 1;
  const Status writable = Access::requireEditDirectories(session_.handle());

  Result<Timesheet> ts = session_.timesheets().findOrCreate(year, month);
  if (!ts) {
    QMessageBox::critical(this, QStringLiteral("Помилка"), ts.error().message);
    table_->blockSignals(false);
    return;
  }

  for (const TimesheetRow & row : ts.value().rows) {
    Result<Employee> emp = session_.employees().findById(row.employeeId);
    if (!emp)
      continue;

    const int r = table_->rowCount();
    table_->insertRow(r);

    QTableWidgetItem * idItem = new QTableWidgetItem(QString::number(row.employeeId.value));
    table_->setItem(r, kColId, idItem);

    QTableWidgetItem * nameItem = new QTableWidgetItem(emp.value().name);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    table_->setItem(r, kColName, nameItem);

    auto * days = new QTableWidgetItem(QString::number(row.daysWorked));
    auto * hours = new QTableWidgetItem(QString::number(row.hoursWorked));
    auto * vacation = new QTableWidgetItem(QString::number(row.vacationDays));
    auto * sick = new QTableWidgetItem(QString::number(row.sickDays));
    if (!writable) {
      const auto ro = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
      days->setFlags(ro);
      hours->setFlags(ro);
      vacation->setFlags(ro);
      sick->setFlags(ro);
    }
    table_->setItem(r, kColDaysWorked, days);
    table_->setItem(r, kColHoursWorked, hours);
    table_->setItem(r, kColVacationDays, vacation);
    table_->setItem(r, kColSickDays, sick);
  }

  table_->setEditTriggers(writable ? QAbstractItemView::DoubleClicked |
                                         QAbstractItemView::EditKeyPressed |
                                         QAbstractItemView::AnyKeyPressed
                                   : QAbstractItemView::NoEditTriggers);
  table_->setToolTip(writable ? QString() : writable.message());

  table_->blockSignals(false);
}

void TimesheetPage::saveRow(int row)
{
  const int year = yearSpinBox_->value();
  const int month = monthCombo_->currentIndex() + 1;

  TimesheetRow r;
  r.employeeId = EmployeeId{table_->item(row, kColId)->text().toLongLong()};
  r.daysWorked = table_->item(row, kColDaysWorked)->text().toInt();
  r.hoursWorked = table_->item(row, kColHoursWorked)->text().toInt();
  r.vacationDays = table_->item(row, kColVacationDays)->text().toInt();
  r.sickDays = table_->item(row, kColSickDays)->text().toInt();

  Status status = session_.timesheets().upsertRow(year, month, r);
  if (!status) {
    QMessageBox::critical(this, QStringLiteral("Помилка"), status.message());
  }
}

void TimesheetPage::printTimesheet()
{
  const int year = yearSpinBox_->value();
  const int month = monthCombo_->currentIndex() + 1;

  Result<Timesheet> ts = session_.timesheets().findOrCreate(year, month);
  if (!ts) {
    ui::showError(this, ts.error());
    return;
  }

  const Result<QString> html = PrintForms(session_.handle()).timesheetHtml(ts.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }

  ui::printHtml(
      this,
      QStringLiteral("Табель обліку робочого часу (П-5) %1.%2")
          .arg(QString::number(month).rightJustified(2, QLatin1Char('0')), QString::number(year)),
      html.value());
}

} // namespace fin
