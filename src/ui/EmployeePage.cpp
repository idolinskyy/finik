#include "ui/EmployeePage.h"

#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/Employee.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {
namespace {

bool editEmployee(QWidget * parent, Employee & row)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(row.id ? QStringLiteral("Картка працівника")
                               : QStringLiteral("Новий працівник"));
  auto * form = new QFormLayout;
  auto * name = new QLineEdit(row.name);
  auto * taxId = new QLineEdit(row.taxId);
  auto * pos = new QLineEdit(row.position);
  auto * salary =
      new QLineEdit(row.salary.isZero() ? QStringLiteral("0.00") : row.salary.toDisplay());
  auto * hired = new QDateEdit(row.hiredOn.isValid() ? row.hiredOn : QDate::currentDate());
  hired->setCalendarPopup(true);
  hired->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  auto * disabled = new QCheckBox(QStringLiteral("Особа з інвалідністю (ЄСВ 8.41%)"));
  disabled->setChecked(row.disabledSsc);
  auto * pitBenefit = new QCheckBox(QStringLiteral("ПСП (податкова соціальна пільга, п. 169.1 ПКУ)"));
  pitBenefit->setChecked(row.hasPitBenefit);
  pitBenefit->setToolTip(
      QStringLiteral("Зменшує базу ПДФО на 1 340 грн, якщо нараховано ≤ 3 540 грн (ставки 2026)."));
  auto * department = new QLineEdit(row.department);
  auto * active = new QCheckBox(QStringLiteral("Працює"));
  active->setChecked(row.isActive);

  form->addRow(QStringLiteral("ПІБ"), name);
  form->addRow(QStringLiteral("РНОКПП"), taxId);
  form->addRow(QStringLiteral("Посада"), pos);
  form->addRow(QStringLiteral("Підрозділ"), department);
  form->addRow(QStringLiteral("Оклад (грн)"), salary);
  form->addRow(QStringLiteral("Прийнято"), hired);
  form->addRow(QString(), disabled);
  form->addRow(QString(), pitBenefit);
  form->addRow(QString(), active);

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  dialog.resize(420, 260);
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  row.name = name->text().trimmed();
  row.taxId = taxId->text().trimmed();
  row.position = pos->text().trimmed();
  row.department = department->text().trimmed();
  Result<Money> m = Money::fromDisplayString(salary->text());
  if (m) {
    row.salary = m.value();
  }
  row.hiredOn = hired->date();
  row.disabledSsc = disabled->isChecked();
  row.hasPitBenefit = pitBenefit->isChecked();
  row.isActive = active->isChecked();
  return true;
}

} // namespace

EmployeePage::EmployeePage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * toolbar = new QHBoxLayout;
  addBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                            QStringLiteral("Додати"));
  searchBar_ = new QLineEdit;
  searchBar_->setPlaceholderText(QStringLiteral("Швидкий пошук..."));
  searchBar_->setClearButtonEnabled(true);

  toolbar->addWidget(addBtn_);
  toolbar->addWidget(searchBar_, 1);

  model_ = new EmployeeModel(this);
  proxy_ = new QSortFilterProxyModel(this);
  proxy_->setSourceModel(model_);
  proxy_->setFilterKeyColumn(-1);
  proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);

  view_ = new QTableView(this);
  view_->setModel(proxy_);
  view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->setSelectionMode(QAbstractItemView::SingleSelection);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->verticalHeader()->setVisible(false);

  layout->addLayout(toolbar);
  layout->addWidget(view_);

  connect(addBtn_, &QPushButton::clicked, this, &EmployeePage::add);
  connect(searchBar_, &QLineEdit::textChanged, this, &EmployeePage::applyFilter);
  connect(view_, &QTableView::doubleClicked, this, [this](const QModelIndex &) { edit(); });

  view_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(view_, &QWidget::customContextMenuRequested, this, [this](const QPoint & pos) {
    const EmployeeId id = selectedId();
    if (!id)
      return;

    const Status writable = Access::requireEditDirectories(session_.handle());

    QMenu menu(this);
    QAction * openAction =
        menu.addAction(QIcon(QStringLiteral(":/help/icons/edit.svg")), QStringLiteral("Відкрити"));
    connect(openAction, &QAction::triggered, this, &EmployeePage::edit);

    QAction * copyAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/add.svg")),
                                          QStringLiteral("Копіювати (F9)"));
    copyAction->setEnabled(bool(writable));
    connect(copyAction, &QAction::triggered, this, &EmployeePage::clone);

    menu.addSeparator();

    QAction * removeAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/delete.svg")),
                                            QStringLiteral("Видалити"));
    removeAction->setEnabled(bool(writable));
    connect(removeAction, &QAction::triggered, this, &EmployeePage::removeSelected);

    menu.exec(view_->viewport()->mapToGlobal(pos));
  });

  auto * f9 = new QShortcut(Qt::Key_F9, this);
  connect(f9, &QShortcut::activated, this, [this]() {
    if (Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
      clone();
    }
  });

  auto * del = new QShortcut(QKeySequence::Delete, this);
  connect(del, &QShortcut::activated, this, [this]() {
    if (Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
      removeSelected();
    }
  });

  auto * enter = new QShortcut(Qt::Key_Return, view_);
  enter->setContext(Qt::WidgetShortcut);
  connect(enter, &QShortcut::activated, this, &EmployeePage::edit);

  const Status writable = Access::requireEditDirectories(session_.handle());
  addBtn_->setEnabled(bool(writable));
  if (!writable) {
    auto * hint = new QLabel(writable.message());
    layout->addWidget(hint);
  }
  reload();
}

void EmployeePage::reload()
{
  const Result<std::vector<Employee>> list = session_.employees().listAll();
  if (!list) {
    ui::showError(this, list.error());
    return;
  }
  model_->setEmployees(list.value());
}

EmployeeId EmployeePage::selectedId() const
{
  if (!view_->selectionModel())
    return {};
  const QModelIndex current = view_->selectionModel()->currentIndex();
  if (!current.isValid())
    return {};
  const QModelIndex srcIndex = proxy_->mapToSource(current);
  return model_->idAt(srcIndex.row());
}

void EmployeePage::add()
{
  Employee row;
  row.isActive = true;
  if (!editEmployee(this, row)) {
    return;
  }
  const Result<EmployeeId> id = session_.employees().insert(row);
  if (!id) {
    ui::showError(this, id.error());
    return;
  }
  reload();
}

void EmployeePage::clone()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const EmployeeId id = selectedId();
  if (!id) {
    return;
  }
  Result<Employee> rowRes = session_.employees().findById(id);
  if (!rowRes) {
    ui::showError(this, rowRes.error());
    return;
  }
  Employee row = rowRes.value();
  row.id = EmployeeId{0};
  if (!editEmployee(this, row)) {
    return;
  }
  const Result<EmployeeId> newId = session_.employees().insert(row);
  if (!newId) {
    ui::showError(this, newId.error());
    return;
  }
  reload();
}

void EmployeePage::edit()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const EmployeeId id = selectedId();
  if (!id) {
    return;
  }
  Result<Employee> row = session_.employees().findById(id);
  if (!row) {
    ui::showError(this, row.error());
    return;
  }
  if (!editEmployee(this, row.value())) {
    return;
  }
  const Status status = session_.employees().update(row.value());
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void EmployeePage::removeSelected()
{
  const EmployeeId id = selectedId();
  if (!id) {
    return;
  }
  if (QMessageBox::question(this, QStringLiteral("Видалення"),
                            QStringLiteral("Видалити працівника?")) != QMessageBox::Yes) {
    return;
  }
  const Status status = session_.employees().remove(id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void EmployeePage::reveal(EmployeeId id)
{
  reload();
  for (int r = 0; r < model_->rowCount(); ++r) {
    if (model_->idAt(r) == id) {
      const QModelIndex srcIdx = model_->index(r, 0);
      const QModelIndex prxIdx = proxy_->mapFromSource(srcIdx);
      if (prxIdx.isValid()) {
        view_->setCurrentIndex(prxIdx);
        view_->scrollTo(prxIdx);
      }
      break;
    }
  }
}

void EmployeePage::applyFilter(const QString & text)
{
  proxy_->setFilterFixedString(text.trimmed());
}

} // namespace fin
