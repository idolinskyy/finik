#include "ui/CounterpartyPage.h"

#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/Counterparty.h"

#include <QAbstractItemView>
#include <QAction>
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
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {
namespace {

bool editCounterparty(QWidget * parent, Counterparty & row)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(row.id ? QStringLiteral("Контрагент") : QStringLiteral("Новий контрагент"));
  auto * form = new QFormLayout;
  auto * name = new QLineEdit(row.name);
  auto * edrpou = new QLineEdit(row.edrpou);
  auto * iban = new QLineEdit(row.iban);
  auto * bank = new QLineEdit(row.bankName);
  auto * customer = new QCheckBox(QStringLiteral("Покупець"));
  customer->setChecked(row.isCustomer);
  auto * supplier = new QCheckBox(QStringLiteral("Постачальник"));
  supplier->setChecked(row.isSupplier);
  auto * notes = new QTextEdit(row.notes);
  notes->setAcceptRichText(false);
  form->addRow(QStringLiteral("Назва"), name);
  form->addRow(QStringLiteral("ЄДРПОУ/ІПН"), edrpou);
  form->addRow(QStringLiteral("IBAN"), iban);
  form->addRow(QStringLiteral("Банк"), bank);
  form->addRow(QString(), customer);
  form->addRow(QString(), supplier);
  form->addRow(QStringLiteral("Примітка"), notes);

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  dialog.resize(420, 320);
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  row.name = name->text();
  row.edrpou = edrpou->text();
  row.iban = iban->text();
  row.bankName = bank->text();
  row.isCustomer = customer->isChecked();
  row.isSupplier = supplier->isChecked();
  row.notes = notes->toPlainText();
  return true;
}

} // namespace

CounterpartyPage::CounterpartyPage(LedgerSession & session, QWidget * parent)
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

  model_ = new CounterpartyModel(this);
  proxy_ = new QSortFilterProxyModel(this);
  proxy_->setSourceModel(model_);
  proxy_->setFilterKeyColumn(-1);
  proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);

  view_ = new QTableView(this);
  view_->setModel(proxy_);
  view_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->setSelectionMode(QAbstractItemView::SingleSelection);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->verticalHeader()->setVisible(false);

  layout->addLayout(toolbar);
  layout->addWidget(view_);

  connect(addBtn_, &QPushButton::clicked, this, &CounterpartyPage::add);
  connect(searchBar_, &QLineEdit::textChanged, this, &CounterpartyPage::applyFilter);
  connect(view_, &QTableView::doubleClicked, this, [this](const QModelIndex &) { edit(); });

  view_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(view_, &QWidget::customContextMenuRequested, this, [this](const QPoint & pos) {
    const CounterpartyId id = selectedId();
    if (!id)
      return;

    const Status writable = Access::requireEditDirectories(session_.handle());

    QMenu menu(this);
    QAction * openAction =
        menu.addAction(QIcon(QStringLiteral(":/help/icons/edit.svg")), QStringLiteral("Відкрити"));
    connect(openAction, &QAction::triggered, this, &CounterpartyPage::edit);

    QAction * copyAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/add.svg")),
                                          QStringLiteral("Копіювати (F9)"));
    copyAction->setEnabled(bool(writable));
    connect(copyAction, &QAction::triggered, this, &CounterpartyPage::clone);

    menu.addSeparator();

    QAction * removeAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/delete.svg")),
                                            QStringLiteral("Видалити"));
    removeAction->setEnabled(bool(writable));
    connect(removeAction, &QAction::triggered, this, &CounterpartyPage::removeSelected);

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
  connect(enter, &QShortcut::activated, this, &CounterpartyPage::edit);

  const Status writable = Access::requireEditDirectories(session_.handle());
  addBtn_->setEnabled(bool(writable));
  if (!writable) {
    auto * hint = new QLabel(writable.message());
    layout->addWidget(hint);
  }
  reload();
}

void CounterpartyPage::reload()
{
  const Result<std::vector<Counterparty>> list = session_.counterparties().listAll();
  if (!list) {
    ui::showError(this, list.error());
    return;
  }
  model_->setCounterparties(list.value());
}

CounterpartyId CounterpartyPage::selectedId() const
{
  if (!view_->selectionModel())
    return {};
  const QModelIndex current = view_->selectionModel()->currentIndex();
  if (!current.isValid())
    return {};
  const QModelIndex srcIndex = proxy_->mapToSource(current);
  return model_->idAt(srcIndex.row());
}

void CounterpartyPage::add()
{
  Counterparty row;
  if (!editCounterparty(this, row)) {
    return;
  }
  const Result<CounterpartyId> id = session_.counterparties().insert(row);
  if (!id) {
    ui::showError(this, id.error());
    return;
  }
  reload();
}

void CounterpartyPage::clone()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const CounterpartyId id = selectedId();
  if (!id) {
    return;
  }
  Result<Counterparty> rowRes = session_.counterparties().findById(id);
  if (!rowRes) {
    ui::showError(this, rowRes.error());
    return;
  }
  Counterparty row = rowRes.value();
  row.id = CounterpartyId{0};
  if (!editCounterparty(this, row)) {
    return;
  }
  const Result<CounterpartyId> newId = session_.counterparties().insert(row);
  if (!newId) {
    ui::showError(this, newId.error());
    return;
  }
  reload();
}

void CounterpartyPage::edit()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const CounterpartyId id = selectedId();
  if (!id) {
    return;
  }
  Result<Counterparty> row = session_.counterparties().findById(id);
  if (!row) {
    ui::showError(this, row.error());
    return;
  }
  if (!editCounterparty(this, row.value())) {
    return;
  }
  const Status status = session_.counterparties().update(row.value());
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void CounterpartyPage::removeSelected()
{
  const CounterpartyId id = selectedId();
  if (!id) {
    return;
  }
  if (QMessageBox::question(this, QStringLiteral("Видалення"),
                            QStringLiteral("Видалити контрагента?")) != QMessageBox::Yes) {
    return;
  }
  const Status status = session_.counterparties().remove(id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void CounterpartyPage::reveal(CounterpartyId id)
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

void CounterpartyPage::applyFilter(const QString & text)
{
  proxy_->setFilterFixedString(text.trimmed());
}

} // namespace fin
