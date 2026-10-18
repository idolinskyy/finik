#include "ui/LowValuePage.h"

#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/LowValueItem.h"

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

bool editItem(QWidget * parent, LowValueItem & row)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(row.id ? QStringLiteral("МШП") : QStringLiteral("Нове МШП"));
  auto * inventory = new QLineEdit(row.inventoryNo);
  auto * name = new QLineEdit(row.name);
  auto * unit = new QLineEdit(row.unit);
  auto * active = new QCheckBox(QStringLiteral("Активне"));
  active->setChecked(row.isActive);
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Інв. номер"), inventory);
  form->addRow(QStringLiteral("Назва"), name);
  form->addRow(QStringLiteral("Од. виміру"), unit);
  form->addRow(QString(), active);
  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  row.inventoryNo = inventory->text();
  row.name = name->text();
  row.unit = unit->text();
  row.isActive = active->isChecked();
  return true;
}

} // namespace

LowValuePage::LowValuePage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * toolbar = new QHBoxLayout;
  addBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                            QStringLiteral("Додати"));
  toolbar->addWidget(addBtn_);
  toolbar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(5);
  table_->setHorizontalHeaderLabels({QStringLiteral("Інв. №"), QStringLiteral("Назва"),
                                     QStringLiteral("Од."), QStringLiteral("К-сть"),
                                     QStringLiteral("Сума")});
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  layout->addLayout(toolbar);
  layout->addWidget(table_);

  connect(addBtn_, &QPushButton::clicked, this, &LowValuePage::add);
  connect(table_, &QTableWidget::doubleClicked, this, [this](const QModelIndex &) { edit(); });

  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint & pos) {
    const LowValueId id = selectedId();
    if (!id)
      return;

    const Status writable = Access::requireEditDirectories(session_.handle());

    QMenu menu(this);
    QAction * openAction =
        menu.addAction(QIcon(QStringLiteral(":/help/icons/edit.svg")), QStringLiteral("Відкрити"));
    connect(openAction, &QAction::triggered, this, &LowValuePage::edit);

    QAction * copyAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/add.svg")),
                                          QStringLiteral("Копіювати (F9)"));
    copyAction->setEnabled(bool(writable));
    connect(copyAction, &QAction::triggered, this, &LowValuePage::clone);

    menu.addSeparator();

    QAction * removeAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/delete.svg")),
                                            QStringLiteral("Видалити"));
    removeAction->setEnabled(bool(writable));
    connect(removeAction, &QAction::triggered, this, &LowValuePage::removeSelected);

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

void LowValuePage::reload()
{
  const Result<std::vector<LowValueItem>> list = session_.lowValue().listAll();
  if (!list) {
    ui::showError(this, list.error());
    return;
  }
  table_->setRowCount(static_cast<int>(list.value().size()));
  int row = 0;
  for (const LowValueItem & item : list.value()) {
    auto * inv = new QTableWidgetItem(item.inventoryNo);
    inv->setData(Qt::UserRole, static_cast<qlonglong>(item.id.value));
    table_->setItem(row, 0, inv);
    table_->setItem(row, 1, new QTableWidgetItem(item.name));
    table_->setItem(row, 2, new QTableWidgetItem(item.unit));
    auto * qty = new QTableWidgetItem(item.quantity.toDisplay());
    qty->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 3, qty);
    auto * amt = new QTableWidgetItem(item.amount.toDisplay());
    amt->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 4, amt);
    ++row;
  }
}

LowValueId LowValuePage::selectedId() const
{
  const int row = table_->currentRow();
  if (row < 0) {
    return {};
  }
  QTableWidgetItem * cell = table_->item(row, 0);
  if (cell == nullptr) {
    return {};
  }
  return LowValueId{cell->data(Qt::UserRole).toLongLong()};
}

void LowValuePage::add()
{
  LowValueItem row;
  row.unit = QStringLiteral("шт");
  row.isActive = true;
  if (!editItem(this, row)) {
    return;
  }
  const Result<LowValueId> id = session_.lowValue().insert(row);
  if (!id) {
    ui::showError(this, id.error());
    return;
  }
  reload();
}

void LowValuePage::clone()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const LowValueId id = selectedId();
  if (!id) {
    return;
  }
  Result<LowValueItem> rowRes = session_.lowValue().findById(id);
  if (!rowRes) {
    ui::showError(this, rowRes.error());
    return;
  }
  LowValueItem row = rowRes.value();
  row.id = LowValueId{0};
  if (!editItem(this, row)) {
    return;
  }
  const Result<LowValueId> newId = session_.lowValue().insert(row);
  if (!newId) {
    ui::showError(this, newId.error());
    return;
  }
  reload();
}

void LowValuePage::edit()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const LowValueId id = selectedId();
  if (!id) {
    return;
  }
  Result<LowValueItem> row = session_.lowValue().findById(id);
  if (!row) {
    ui::showError(this, row.error());
    return;
  }
  if (!editItem(this, row.value())) {
    return;
  }
  const Status status = session_.lowValue().update(row.value());
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void LowValuePage::removeSelected()
{
  const LowValueId id = selectedId();
  if (!id) {
    return;
  }
  if (QMessageBox::question(this, QStringLiteral("Видалення"),
                            QStringLiteral("Видалити картку МШП?")) != QMessageBox::Yes) {
    return;
  }
  const Status status = session_.lowValue().remove(id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void LowValuePage::reveal(LowValueId id)
{
  reload();
  ui::selectTableRowById(table_, id.value);
}

void LowValuePage::applyFilter(const QString & text)
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
