#include "ui/ContractPage.h"

#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/Contract.h"
#include "fin/domain/Counterparty.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
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
#include <QKeySequence>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {
namespace {

bool editContract(QWidget * parent, LedgerSession & session, Contract & row)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(row.id ? QStringLiteral("Договір") : QStringLiteral("Новий договір"));

  auto * counterparty = new QComboBox;
  ui::fillCounterpartyCombo(counterparty, session, row.counterpartyId, false);
  auto * number = new QLineEdit(row.number);
  auto * name = new QLineEdit(row.name);
  auto * date = new QDateEdit(row.date.isValid() ? row.date : QDate::currentDate());
  date->setCalendarPopup(true);
  date->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  auto * type = new QComboBox;
  type->addItem(contractTypeLabelUk(ContractType::WithCustomer),
                contractTypeToString(ContractType::WithCustomer));
  type->addItem(contractTypeLabelUk(ContractType::WithSupplier),
                contractTypeToString(ContractType::WithSupplier));
  type->addItem(contractTypeLabelUk(ContractType::Other),
                contractTypeToString(ContractType::Other));
  for (int i = 0; i < type->count(); ++i) {
    if (type->itemData(i).toString() == contractTypeToString(row.contractType)) {
      type->setCurrentIndex(i);
      break;
    }
  }
  auto * currency = new QLineEdit(row.currency.isEmpty() ? QStringLiteral("UAH") : row.currency);
  auto * active = new QCheckBox(QStringLiteral("Діє"));
  active->setChecked(row.isActive);

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Контрагент*"), counterparty);
  form->addRow(QStringLiteral("Номер*"), number);
  form->addRow(QStringLiteral("Назва"), name);
  form->addRow(QStringLiteral("Дата"), date);
  form->addRow(QStringLiteral("Тип"), type);
  form->addRow(QStringLiteral("Валюта"), currency);
  form->addRow(QString(), active);

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  dialog.resize(460, 280);

  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  row.counterpartyId = ui::selectedCounterparty(counterparty);
  row.number = number->text().trimmed();
  row.name = name->text().trimmed();
  row.date = date->date();
  row.contractType = contractTypeFromString(type->currentData().toString());
  row.currency = currency->text().trimmed().toUpper();
  if (row.currency.isEmpty()) {
    row.currency = QStringLiteral("UAH");
  }
  row.isActive = active->isChecked();
  return true;
}

} // namespace

ContractPage::ContractPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * toolbar = new QHBoxLayout;
  addBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                            QStringLiteral("Додати"));
  searchBar_ = new QLineEdit;
  searchBar_->setPlaceholderText(QStringLiteral("Пошук за номером, назвою, контрагентом…"));
  searchBar_->setClearButtonEnabled(true);
  toolbar->addWidget(addBtn_);
  toolbar->addWidget(searchBar_, 1);

  table_ = new QTableWidget;
  table_->setColumnCount(6);
  table_->setHorizontalHeaderLabels({QStringLiteral("Номер"), QStringLiteral("Назва"),
                                     QStringLiteral("Контрагент"), QStringLiteral("Дата"),
                                     QStringLiteral("Тип"), QStringLiteral("Статус")});
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);

  layout->addLayout(toolbar);
  layout->addWidget(table_);

  connect(addBtn_, &QPushButton::clicked, this, &ContractPage::add);
  connect(searchBar_, &QLineEdit::textChanged, this, &ContractPage::applyFilter);
  connect(table_, &QTableWidget::doubleClicked, this, [this](const QModelIndex &) { edit(); });

  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint & pos) {
    if (!selectedId()) {
      return;
    }
    const Status writable = Access::requireEditDirectories(session_.handle());
    QMenu menu(this);
    QAction * openAction =
        menu.addAction(QIcon(QStringLiteral(":/help/icons/edit.svg")), QStringLiteral("Відкрити"));
    connect(openAction, &QAction::triggered, this, &ContractPage::edit);
    menu.addSeparator();
    QAction * removeAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/delete.svg")),
                                            QStringLiteral("Видалити"));
    removeAction->setEnabled(bool(writable));
    connect(removeAction, &QAction::triggered, this, &ContractPage::removeSelected);
    menu.exec(table_->viewport()->mapToGlobal(pos));
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
    layout->addWidget(new QLabel(writable.message()));
  }
  reload();
}

void ContractPage::reload()
{
  const Result<std::vector<Contract>> list = session_.contracts().listAll();
  if (!list) {
    ui::showError(this, list.error());
    return;
  }
  table_->setRowCount(static_cast<int>(list.value().size()));
  int row = 0;
  for (const Contract & item : list.value()) {
    QString partyName = QStringLiteral("—");
    if (item.counterpartyId) {
      const Result<Counterparty> party = session_.counterparties().findById(item.counterpartyId);
      if (party) {
        partyName = party.value().name;
      }
    }
    auto * number = new QTableWidgetItem(item.number);
    number->setData(Qt::UserRole, static_cast<qlonglong>(item.id.value));
    table_->setItem(row, 0, number);
    table_->setItem(row, 1, new QTableWidgetItem(item.name));
    table_->setItem(row, 2, new QTableWidgetItem(partyName));
    table_->setItem(row, 3,
                    new QTableWidgetItem(item.date.toString(QStringLiteral("dd.MM.yyyy"))));
    table_->setItem(row, 4, new QTableWidgetItem(contractTypeLabelUk(item.contractType)));
    auto * statusItem = new QTableWidgetItem(item.isActive ? QStringLiteral("Діє")
                                                           : QStringLiteral("Не діє"));
    if (!item.isActive) {
      statusItem->setForeground(Qt::gray);
    }
    table_->setItem(row, 5, statusItem);
    ++row;
  }
  applyFilter(searchBar_ ? searchBar_->text() : QString());
}

void ContractPage::applyFilter(const QString & text)
{
  const QString needle = text.trimmed();
  for (int row = 0; row < table_->rowCount(); ++row) {
    bool match = needle.isEmpty();
    if (!match) {
      for (int col = 0; col < table_->columnCount(); ++col) {
        const QTableWidgetItem * cell = table_->item(row, col);
        if (cell != nullptr && cell->text().contains(needle, Qt::CaseInsensitive)) {
          match = true;
          break;
        }
      }
    }
    table_->setRowHidden(row, !match);
  }
}

ContractId ContractPage::selectedId() const
{
  const int row = table_->currentRow();
  if (row < 0) {
    return {};
  }
  QTableWidgetItem * cell = table_->item(row, 0);
  if (cell == nullptr) {
    return {};
  }
  return ContractId{cell->data(Qt::UserRole).toLongLong()};
}

void ContractPage::reveal(ContractId id)
{
  if (!id) {
    return;
  }
  ui::selectTableRowById(table_, id.value);
}

void ContractPage::add()
{
  Contract row;
  row.date = QDate::currentDate();
  row.isActive = true;
  if (!editContract(this, session_, row)) {
    return;
  }
  const Result<ContractId> id = session_.contracts().insert(row);
  if (!id) {
    ui::showError(this, id.error());
    return;
  }
  reload();
  reveal(id.value());
}

void ContractPage::edit()
{
  const ContractId id = selectedId();
  if (!id) {
    return;
  }
  Result<Contract> loaded = session_.contracts().findById(id);
  if (!loaded) {
    ui::showError(this, loaded.error());
    return;
  }
  Contract row = loaded.value();
  if (!editContract(this, session_, row)) {
    return;
  }
  const Status status = session_.contracts().update(row);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
  reveal(id);
}

void ContractPage::removeSelected()
{
  const ContractId id = selectedId();
  if (!id) {
    return;
  }
  if (QMessageBox::question(this, QStringLiteral("Видалення"),
                            QStringLiteral("Видалити вибраний договір?")) != QMessageBox::Yes) {
    return;
  }
  const Status status = session_.contracts().remove(id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

} // namespace fin
