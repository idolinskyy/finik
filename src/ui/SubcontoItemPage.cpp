#include "ui/SubcontoItemPage.h"

#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/Subconto.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
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

bool editItem(QWidget * parent, SubcontoItem & row)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(row.id ? QStringLiteral("Стаття субконто")
                               : QStringLiteral("Нова стаття субконто"));

  auto * code = new QLineEdit(row.code);
  auto * name = new QLineEdit(row.name);
  auto * active = new QCheckBox(QStringLiteral("Діє"));
  active->setChecked(row.isActive);

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Код*"), code);
  form->addRow(QStringLiteral("Назва*"), name);
  form->addRow(QString(), active);

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  dialog.resize(420, 180);

  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  row.code = code->text().trimmed();
  row.name = name->text().trimmed();
  row.isActive = active->isChecked();
  return true;
}

bool editKind(QWidget * parent, QString & id, QString & nameUa)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("Новий вид субконто"));
  auto * slug = new QLineEdit(id);
  auto * name = new QLineEdit(nameUa);
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Ідентифікатор*"), slug);
  form->addRow(QStringLiteral("Назва*"), name);
  form->addRow(new QLabel(QStringLiteral("Латиниця, цифри й підкреслення. Далі додайте статті "
                                        "в цьому довіднику і прив’яжіть вид у плані рахунків.")));
  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  dialog.resize(460, 200);
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  id = slug->text().trimmed();
  nameUa = name->text().trimmed();
  return true;
}

} // namespace

SubcontoItemPage::SubcontoItemPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * toolbar = new QHBoxLayout;
  kind_ = new QComboBox;
  addBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                            QStringLiteral("Додати"));
  addKindBtn_ = new QPushButton(QStringLiteral("Новий вид…"));
  searchBar_ = new QLineEdit;
  searchBar_->setPlaceholderText(QStringLiteral("Пошук за кодом або назвою…"));
  searchBar_->setClearButtonEnabled(true);
  toolbar->addWidget(new QLabel(QStringLiteral("Вид")));
  toolbar->addWidget(kind_);
  toolbar->addWidget(addBtn_);
  toolbar->addWidget(addKindBtn_);
  toolbar->addWidget(searchBar_, 1);

  table_ = new QTableWidget;
  table_->setColumnCount(3);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Код"), QStringLiteral("Назва"), QStringLiteral("Статус")});
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);

  layout->addLayout(toolbar);
  layout->addWidget(table_);

  connect(addBtn_, &QPushButton::clicked, this, &SubcontoItemPage::add);
  connect(addKindBtn_, &QPushButton::clicked, this, &SubcontoItemPage::addKind);
  connect(searchBar_, &QLineEdit::textChanged, this, &SubcontoItemPage::applyFilter);
  connect(kind_, &QComboBox::currentIndexChanged, this, [this]() { reload(); });
  connect(table_, &QTableWidget::doubleClicked, this, [this](const QModelIndex &) { edit(); });

  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint & pos) {
    if (selectedId() <= 0) {
      return;
    }
    const Status writable = Access::requireEditDirectories(session_.handle());
    QMenu menu(this);
    QAction * openAction =
        menu.addAction(QIcon(QStringLiteral(":/help/icons/edit.svg")), QStringLiteral("Відкрити"));
    connect(openAction, &QAction::triggered, this, &SubcontoItemPage::edit);
    menu.addSeparator();
    QAction * removeAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/delete.svg")),
                                            QStringLiteral("Видалити"));
    removeAction->setEnabled(bool(writable));
    connect(removeAction, &QAction::triggered, this, &SubcontoItemPage::removeSelected);
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
  addKindBtn_->setEnabled(bool(writable));
  if (!writable) {
    layout->addWidget(new QLabel(writable.message()));
  }
  reload();
}

QString SubcontoItemPage::currentKindId() const
{
  return kind_->currentData().toString();
}

void SubcontoItemPage::reload()
{
  const QString previous = currentKindId().isEmpty() ? subconto_kinds::ExpenseArticle
                                                     : currentKindId();
  kind_->blockSignals(true);
  kind_->clear();
  const Result<std::vector<SubcontoKind>> kinds = session_.subconto().listKinds();
  if (!kinds) {
    kind_->blockSignals(false);
    ui::showError(this, kinds.error());
    return;
  }
  int kindIndex = 0;
  for (const SubcontoKind & k : kinds.value()) {
    if (!isCustomSubcontoSource(k.sourceTable)) {
      continue;
    }
    kind_->addItem(k.nameUa, k.id);
    if (k.id == previous) {
      kindIndex = kind_->count() - 1;
    }
  }
  if (kind_->count() > 0) {
    kind_->setCurrentIndex(kindIndex);
  }
  kind_->blockSignals(false);

  const QString kindId = currentKindId();
  if (kindId.isEmpty()) {
    table_->setRowCount(0);
    return;
  }
  const Result<std::vector<SubcontoItem>> list = session_.subconto().listItems(kindId, false);
  if (!list) {
    ui::showError(this, list.error());
    return;
  }
  table_->setRowCount(static_cast<int>(list.value().size()));
  int row = 0;
  for (const SubcontoItem & item : list.value()) {
    auto * code = new QTableWidgetItem(item.code);
    code->setData(Qt::UserRole, static_cast<qlonglong>(item.id));
    table_->setItem(row, 0, code);
    table_->setItem(row, 1, new QTableWidgetItem(item.name));
    table_->setItem(row, 2,
                    new QTableWidgetItem(item.isActive ? QStringLiteral("діє")
                                                       : QStringLiteral("не діє")));
    ++row;
  }
  applyFilter(searchBar_->text());
}

void SubcontoItemPage::applyFilter(const QString & text)
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

std::int64_t SubcontoItemPage::selectedId() const
{
  const int row = table_->currentRow();
  if (row < 0) {
    return 0;
  }
  const QTableWidgetItem * cell = table_->item(row, 0);
  return cell ? cell->data(Qt::UserRole).toLongLong() : 0;
}

void SubcontoItemPage::add()
{
  SubcontoItem row;
  row.kindId = currentKindId();
  if (row.kindId.isEmpty()) {
    return;
  }
  if (!editItem(this, row)) {
    return;
  }
  const Result<std::int64_t> id = session_.subconto().insertItem(row);
  if (!id) {
    ui::showError(this, id.error());
    return;
  }
  reload();
}

void SubcontoItemPage::edit()
{
  const std::int64_t id = selectedId();
  if (id <= 0) {
    return;
  }
  Result<SubcontoItem> found = session_.subconto().findItem(id);
  if (!found) {
    ui::showError(this, found.error());
    return;
  }
  SubcontoItem row = found.value();
  if (!editItem(this, row)) {
    return;
  }
  const Status status = session_.subconto().updateItem(row);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void SubcontoItemPage::removeSelected()
{
  const std::int64_t id = selectedId();
  if (id <= 0) {
    return;
  }
  if (QMessageBox::question(this, QStringLiteral("Видалити статтю?"),
                            QStringLiteral("Статтю буде видалено з довідника.")) !=
      QMessageBox::Yes) {
    return;
  }
  const Status status = session_.subconto().removeItem(id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void SubcontoItemPage::addKind()
{
  QString id;
  QString name;
  if (!editKind(this, id, name)) {
    return;
  }
  const Status status = session_.subconto().insertCustomKind(id, name);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
  const int idx = kind_->findData(id.trimmed().toLower());
  if (idx >= 0) {
    kind_->setCurrentIndex(idx);
  }
}

} // namespace fin
