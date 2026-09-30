#include "ui/WarehousePage.h"

#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/Warehouse.h"

#include <QAbstractItemView>
#include <QCheckBox>
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
#include <QSettings>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {
namespace {

bool editWarehouse(QWidget * parent, Warehouse & row)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(row.id ? QStringLiteral("Склад") : QStringLiteral("Новий склад"));
  auto * code = new QLineEdit(row.code);
  auto * name = new QLineEdit(row.name);
  auto * address = new QLineEdit(row.address);
  auto * active = new QCheckBox(QStringLiteral("Активний склад"));
  active->setChecked(row.isActive);

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Код складу"), code);
  form->addRow(QStringLiteral("Найменування"), name);
  form->addRow(QStringLiteral("Адреса"), address);
  form->addRow(QString(), active);

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  dialog.resize(400, 200);

  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  row.code = code->text().trimmed();
  row.name = name->text().trimmed();
  row.address = address->text().trimmed();
  row.isActive = active->isChecked();
  return true;
}

} // namespace

WarehousePage::WarehousePage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * toolbar = new QHBoxLayout;
  addBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                            QStringLiteral("Додати"));
  toolbar->addWidget(addBtn_);
  toolbar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(4);
  table_->setHorizontalHeaderLabels({QStringLiteral("Код"), QStringLiteral("Найменування"),
                                     QStringLiteral("Адреса"), QStringLiteral("Статус")});
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);

  layout->addLayout(toolbar);
  layout->addWidget(table_);

  connect(addBtn_, &QPushButton::clicked, this, &WarehousePage::add);
  connect(table_, &QTableWidget::doubleClicked, this, [this](const QModelIndex &) { edit(); });

  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint & pos) {
    const WarehouseId id = selectedId();
    if (!id)
      return;

    const Status writable = Access::requireEditDirectories(session_.handle());

    QMenu menu(this);
    QAction * openAction =
        menu.addAction(QIcon(QStringLiteral(":/help/icons/edit.svg")), QStringLiteral("Відкрити"));
    connect(openAction, &QAction::triggered, this, &WarehousePage::edit);

    QAction * copyAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/add.svg")),
                                          QStringLiteral("Копіювати (F9)"));
    copyAction->setEnabled(bool(writable));
    connect(copyAction, &QAction::triggered, this, &WarehousePage::clone);

    menu.addSeparator();

    QAction * removeAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/delete.svg")),
                                            QStringLiteral("Видалити"));
    removeAction->setEnabled(bool(writable));
    connect(removeAction, &QAction::triggered, this, &WarehousePage::removeSelected);

    menu.exec(table_->viewport()->mapToGlobal(pos));
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

  const Status writable = Access::requireEditDirectories(session_.handle());
  addBtn_->setEnabled(bool(writable));
  if (!writable) {
    auto * hint = new QLabel(writable.message());
    layout->addWidget(hint);
  }
  reload();
}

void WarehousePage::reload()
{
  const Result<std::vector<Warehouse>> list = session_.warehouses().listAll();
  if (!list) {
    ui::showError(this, list.error());
    return;
  }
  table_->setRowCount(static_cast<int>(list.value().size()));
  int row = 0;
  for (const Warehouse & item : list.value()) {
    auto * code = new QTableWidgetItem(item.code);
    code->setData(Qt::UserRole, static_cast<qlonglong>(item.id.value));
    table_->setItem(row, 0, code);
    table_->setItem(row, 1, new QTableWidgetItem(item.name));
    table_->setItem(row, 2, new QTableWidgetItem(item.address));
    auto * statusItem = new QTableWidgetItem(item.isActive ? QStringLiteral("Активний")
                                                           : QStringLiteral("Неактивний"));
    if (!item.isActive) {
      statusItem->setForeground(Qt::gray);
    }
    table_->setItem(row, 3, statusItem);
    ++row;
  }
}

WarehouseId WarehousePage::selectedId() const
{
  const int row = table_->currentRow();
  if (row < 0) {
    return {};
  }
  QTableWidgetItem * cell = table_->item(row, 0);
  if (cell == nullptr) {
    return {};
  }
  return WarehouseId{cell->data(Qt::UserRole).toLongLong()};
}

void WarehousePage::clone()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const WarehouseId id = selectedId();
  if (!id) {
    return;
  }
  Result<Warehouse> rowRes = session_.warehouses().findById(id);
  if (!rowRes) {
    ui::showError(this, rowRes.error());
    return;
  }
  Warehouse row = rowRes.value();
  row.id = WarehouseId{0};
  if (!editWarehouse(this, row)) {
    return;
  }
  if (row.code.isEmpty() || row.name.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("Помилка"),
                         QStringLiteral("Код та назва складу обов'язкові для заповнення"));
    return;
  }
  const Result<WarehouseId> newId = session_.warehouses().insert(row);
  if (!newId) {
    ui::showError(this, newId.error());
    return;
  }
  reload();
}

void WarehousePage::add()
{
  Warehouse row;
  if (!editWarehouse(this, row)) {
    return;
  }
  if (row.code.isEmpty() || row.name.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("Помилка"),
                         QStringLiteral("Код та назва складу обов'язкові для заповнення"));
    return;
  }
  const Result<WarehouseId> id = session_.warehouses().insert(row);
  if (!id) {
    ui::showError(this, id.error());
    return;
  }
  reload();
}

void WarehousePage::edit()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const WarehouseId id = selectedId();
  if (!id) {
    return;
  }
  Result<Warehouse> row = session_.warehouses().findById(id);
  if (!row) {
    ui::showError(this, row.error());
    return;
  }
  Warehouse item = row.value();
  if (!editWarehouse(this, item)) {
    return;
  }
  if (item.code.isEmpty() || item.name.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("Помилка"),
                         QStringLiteral("Код та назва складу обов'язкові для заповнення"));
    return;
  }
  const Status status = session_.warehouses().update(item);
  if (!status) {
    ui::showError(this, status.error());
    return;
  }
  reload();
}

void WarehousePage::removeSelected()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const WarehouseId id = selectedId();
  if (!id) {
    return;
  }
  if (id.value == 1) {
    QMessageBox::warning(this, QStringLiteral("Попередження"),
                         QStringLiteral("Неможливо видалити основний склад системи"));
    return;
  }
  if (QMessageBox::question(this, QStringLiteral("Видалення"),
                            QStringLiteral("Видалити обраний склад? Якщо склад використовується "
                                           "у документах, його краще деактивувати.")) !=
      QMessageBox::Yes) {
    return;
  }
  const Status status = session_.warehouses().remove(id);
  if (!status) {
    ui::showError(this, status.error());
    return;
  }
  reload();
}

void WarehousePage::applyFilter(const QString & text)
{
  const QString query = text.trimmed().toLower();
  for (int r = 0; r < table_->rowCount(); ++r) {
    bool match = query.isEmpty();
    if (!match) {
      for (int c = 0; c < table_->columnCount(); ++c) {
        if (table_->item(r, c) && table_->item(r, c)->text().toLower().contains(query)) {
          match = true;
          break;
        }
      }
    }
    table_->setRowHidden(r, !match);
  }
}

} // namespace fin
