#include "ui/IntangiblePage.h"

#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/IntangibleAsset.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {
namespace {

std::vector<Account> filterAssetAccounts(const std::vector<Account> & accounts)
{
  std::vector<Account> rows;
  for (const Account & account : accounts) {
    if (isIntangibleAccountCode(account.code)) {
      rows.push_back(account);
    }
  }
  return rows;
}

bool editAsset(QWidget * parent, LedgerSession & session, IntangibleAsset & row)
{
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  if (!accounts) {
    ui::showError(parent, accounts.error());
    return false;
  }
  QDialog dialog(parent);
  dialog.setWindowTitle(row.id ? QStringLiteral("Нематеріальний актив")
                               : QStringLiteral("Новий нематеріальний актив"));
  auto * inventory = new QLineEdit(row.inventoryNo);
  auto * name = new QLineEdit(row.name);
  auto * assetAccount = new QComboBox;
  ui::fillAccountCombo(assetAccount, filterAssetAccounts(accounts.value()), row.assetAccountId);
  auto * expense = new QComboBox;
  AccountId expenseId = row.expenseAccountId;
  if (!expenseId) {
    for (const Account & account : accounts.value()) {
      if (account.code.compare(QLatin1String(chart::kAdminExpense), Qt::CaseInsensitive) == 0) {
        expenseId = account.id;
        break;
      }
    }
  }
  ui::fillAccountCombo(expense, accounts.value(), expenseId);
  auto * original = new QLineEdit(row.original.isZero() ? QString() : row.original.toDisplay());
  original->setAlignment(Qt::AlignRight);
  auto * salvage = new QLineEdit(row.salvage.isZero() ? QString() : row.salvage.toDisplay());
  salvage->setAlignment(Qt::AlignRight);
  auto * opening = new QLineEdit(
      row.openingAccumulated.isZero() ? QString() : row.openingAccumulated.toDisplay());
  opening->setAlignment(Qt::AlignRight);
  auto * life = new QSpinBox;
  life->setRange(1, 600);
  life->setValue(row.usefulLifeMonths > 0 ? row.usefulLifeMonths : 60);
  life->setSuffix(QStringLiteral(" міс."));
  auto * active = new QCheckBox(QStringLiteral("Активний"));
  active->setChecked(row.isActive);
  const bool locked = row.status != IntangibleStatus::Draft;
  original->setReadOnly(locked);
  salvage->setReadOnly(locked);
  opening->setReadOnly(locked);
  life->setReadOnly(locked);
  assetAccount->setEnabled(!locked);

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Інв. номер"), inventory);
  form->addRow(QStringLiteral("Назва"), name);
  form->addRow(QStringLiteral("Рахунок обліку"), assetAccount);
  form->addRow(QStringLiteral("Витрати амортизації"), expense);
  form->addRow(QStringLiteral("Первісна вартість"), original);
  form->addRow(QStringLiteral("Ліквідаційна"), salvage);
  form->addRow(QStringLiteral("Знос на початок"), opening);
  form->addRow(QStringLiteral("Строк"), life);
  form->addRow(QString(), active);
  auto * hint =
      new QLabel(locked ? QStringLiteral("В експлуатації. Вартість і строк не змінюються.")
                        : QStringLiteral("Знос на початок — лише якщо вводите залишком через 00."));
  hint->setWordWrap(true);
  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addWidget(buttons);
  dialog.resize(480, 420);
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  row.inventoryNo = inventory->text();
  row.name = name->text();
  row.assetAccountId = ui::selectedAccount(assetAccount);
  row.expenseAccountId = ui::selectedAccount(expense);
  const Result<Money> orig = Money::fromDisplayString(original->text());
  if (!orig) {
    ui::showError(parent, orig.error());
    return false;
  }
  row.original = orig.value();
  const Result<Money> salv = salvage->text().trimmed().isEmpty()
                                 ? Result<Money>::ok(Money::zero())
                                 : Money::fromDisplayString(salvage->text());
  if (!salv) {
    ui::showError(parent, salv.error());
    return false;
  }
  row.salvage = salv.value();
  const Result<Money> open = opening->text().trimmed().isEmpty()
                                 ? Result<Money>::ok(Money::zero())
                                 : Money::fromDisplayString(opening->text());
  if (!open) {
    ui::showError(parent, open.error());
    return false;
  }
  row.openingAccumulated = open.value();
  row.usefulLifeMonths = life->value();
  row.isActive = active->isChecked();
  return true;
}

} // namespace

IntangiblePage::IntangiblePage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * toolbar = new QHBoxLayout;
  addBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                            QStringLiteral("Додати"));
  toolbar->addWidget(addBtn_);
  toolbar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(8);
  table_->setHorizontalHeaderLabels({QStringLiteral("Інв. №"), QStringLiteral("Назва"),
                                     QStringLiteral("Рахунок"), QStringLiteral("Первісна"),
                                     QStringLiteral("Знос"), QStringLiteral("Залишкова"),
                                     QStringLiteral("Строк"), QStringLiteral("Стан")});
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  layout->addLayout(toolbar);
  layout->addWidget(table_);

  connect(addBtn_, &QPushButton::clicked, this, &IntangiblePage::add);
  connect(table_, &QTableWidget::doubleClicked, this, [this](const QModelIndex &) { edit(); });

  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint & pos) {
    const IntangibleId id = selectedId();
    if (!id)
      return;

    const Status writable = Access::requireEditDirectories(session_.handle());

    QMenu menu(this);
    QAction * openAction =
        menu.addAction(QIcon(QStringLiteral(":/help/icons/edit.svg")), QStringLiteral("Відкрити"));
    connect(openAction, &QAction::triggered, this, &IntangiblePage::edit);

    QAction * copyAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/add.svg")),
                                          QStringLiteral("Копіювати (F9)"));
    copyAction->setEnabled(bool(writable));
    connect(copyAction, &QAction::triggered, this, &IntangiblePage::clone);

    menu.addSeparator();

    QAction * removeAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/delete.svg")),
                                            QStringLiteral("Видалити"));
    removeAction->setEnabled(bool(writable));
    connect(removeAction, &QAction::triggered, this, &IntangiblePage::removeSelected);

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

void IntangiblePage::reload()
{
  const Result<std::vector<IntangibleAsset>> list = session_.intangibles().listAll();
  if (!list) {
    ui::showError(this, list.error());
    return;
  }
  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  table_->setRowCount(static_cast<int>(list.value().size()));
  int row = 0;
  for (const IntangibleAsset & asset : list.value()) {
    auto * inv = new QTableWidgetItem(asset.inventoryNo);
    inv->setData(Qt::UserRole, static_cast<qlonglong>(asset.id.value));
    table_->setItem(row, 0, inv);
    table_->setItem(row, 1, new QTableWidgetItem(asset.name));
    QString code;
    if (accounts) {
      for (const Account & account : accounts.value()) {
        if (account.id == asset.assetAccountId) {
          code = account.code;
          break;
        }
      }
    }
    table_->setItem(row, 2, new QTableWidgetItem(code));
    auto * orig = new QTableWidgetItem(asset.original.toDisplay());
    orig->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 3, orig);
    auto * acc = new QTableWidgetItem(asset.accumulated.toDisplay());
    acc->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 4, acc);
    auto * res = new QTableWidgetItem(asset.residual().toDisplay());
    res->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 5, res);
    table_->setItem(row, 6, new QTableWidgetItem(QString::number(asset.usefulLifeMonths)));
    table_->setItem(row, 7, new QTableWidgetItem(intangibleStatusLabelUk(asset.status)));
    ++row;
  }
}

IntangibleId IntangiblePage::selectedId() const
{
  const int row = table_->currentRow();
  if (row < 0) {
    return {};
  }
  QTableWidgetItem * cell = table_->item(row, 0);
  if (cell == nullptr) {
    return {};
  }
  return IntangibleId{cell->data(Qt::UserRole).toLongLong()};
}

void IntangiblePage::clone()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const IntangibleId id = selectedId();
  if (!id) {
    return;
  }
  Result<IntangibleAsset> rowRes = session_.intangibles().findById(id);
  if (!rowRes) {
    ui::showError(this, rowRes.error());
    return;
  }
  IntangibleAsset row = rowRes.value();
  row.id = IntangibleId{0};
  if (!editAsset(this, session_, row)) {
    return;
  }
  if (row.name.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("Помилка"),
                         QStringLiteral("Назва НМА обов'язкова для заповнення"));
    return;
  }
  const Result<IntangibleId> newId = session_.intangibles().insert(row);
  if (!newId) {
    ui::showError(this, newId.error());
    return;
  }
  reload();
}

void IntangiblePage::add()
{
  IntangibleAsset row;
  row.isActive = true;
  row.usefulLifeMonths = 60;
  if (!editAsset(this, session_, row)) {
    return;
  }
  const Result<IntangibleId> id = session_.intangibles().insert(row);
  if (!id) {
    ui::showError(this, id.error());
    return;
  }
  reload();
}

void IntangiblePage::edit()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const IntangibleId id = selectedId();
  if (!id) {
    return;
  }
  Result<IntangibleAsset> row = session_.intangibles().findById(id);
  if (!row) {
    ui::showError(this, row.error());
    return;
  }
  if (!editAsset(this, session_, row.value())) {
    return;
  }
  const Status status = session_.intangibles().update(row.value());
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void IntangiblePage::removeSelected()
{
  const IntangibleId id = selectedId();
  if (!id) {
    return;
  }
  if (QMessageBox::question(this, QStringLiteral("Видалення"),
                            QStringLiteral("Видалити картку нематеріального активу?")) !=
      QMessageBox::Yes) {
    return;
  }
  const Status status = session_.intangibles().remove(id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void IntangiblePage::reveal(IntangibleId id)
{
  reload();
  ui::selectTableRowById(table_, id.value);
}

void IntangiblePage::applyFilter(const QString & text)
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
