#include "ui/NomenclaturePage.h"

#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/Nomenclature.h"

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

bool editItem(QWidget * parent, Nomenclature & row)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(row.id ? QStringLiteral("Номенклатура") : QStringLiteral("Нова позиція"));
  auto * form = new QFormLayout;
  auto * name = new QLineEdit(row.name);
  auto * sku = new QLineEdit(row.sku);
  auto * unit = new QLineEdit(row.unit);
  auto * active = new QCheckBox(QStringLiteral("Активна позиція"));
  active->setChecked(row.isActive);
  auto * notes = new QTextEdit;
  notes->setAcceptRichText(false);
  form->addRow(QStringLiteral("Назва"), name);
  form->addRow(QStringLiteral("Артикул / код"), sku);
  form->addRow(QStringLiteral("Од. виміру"), unit);
  form->addRow(QString(), active);

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  dialog.resize(400, 220);
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  row.name = name->text();
  row.sku = sku->text();
  row.unit = unit->text();
  row.isActive = active->isChecked();
  return true;
}

} // namespace

NomenclaturePage::NomenclaturePage(LedgerSession & session, QWidget * parent)
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

  model_ = new NomenclatureModel(this);
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

  connect(addBtn_, &QPushButton::clicked, this, &NomenclaturePage::add);
  connect(searchBar_, &QLineEdit::textChanged, this, &NomenclaturePage::applyFilter);
  connect(view_, &QTableView::doubleClicked, this, [this](const QModelIndex &) { edit(); });

  view_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(view_, &QWidget::customContextMenuRequested, this, [this](const QPoint & pos) {
    const NomenclatureId id = selectedId();
    if (!id)
      return;

    const Status writable = Access::requireEditDirectories(session_.handle());

    QMenu menu(this);
    QAction * openAction =
        menu.addAction(QIcon(QStringLiteral(":/help/icons/edit.svg")), QStringLiteral("Відкрити"));
    connect(openAction, &QAction::triggered, this, &NomenclaturePage::edit);

    QAction * copyAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/add.svg")),
                                          QStringLiteral("Копіювати (F9)"));
    copyAction->setEnabled(bool(writable));
    connect(copyAction, &QAction::triggered, this, &NomenclaturePage::clone);

    menu.addSeparator();

    QAction * removeAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/delete.svg")),
                                            QStringLiteral("Видалити"));
    removeAction->setEnabled(bool(writable));
    connect(removeAction, &QAction::triggered, this, &NomenclaturePage::removeSelected);

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
  connect(enter, &QShortcut::activated, this, &NomenclaturePage::edit);

  const Status writable = Access::requireEditDirectories(session_.handle());
  addBtn_->setEnabled(bool(writable));
  if (!writable) {
    auto * hint = new QLabel(writable.message());
    layout->addWidget(hint);
  }
  reload();
}

void NomenclaturePage::reload()
{
  const Result<std::vector<Nomenclature>> list = session_.nomenclature().listAll();
  if (!list) {
    ui::showError(this, list.error());
    return;
  }
  model_->setNomenclature(list.value());
}

NomenclatureId NomenclaturePage::selectedId() const
{
  if (!view_->selectionModel())
    return {};
  const QModelIndex current = view_->selectionModel()->currentIndex();
  if (!current.isValid())
    return {};
  const QModelIndex srcIndex = proxy_->mapToSource(current);
  return model_->idAt(srcIndex.row());
}

void NomenclaturePage::add()
{
  Nomenclature row;
  if (!editItem(this, row)) {
    return;
  }
  const Result<NomenclatureId> id = session_.nomenclature().insert(row);
  if (!id) {
    ui::showError(this, id.error());
    return;
  }
  reload();
}

void NomenclaturePage::clone()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const NomenclatureId id = selectedId();
  if (!id) {
    return;
  }
  Result<Nomenclature> rowRes = session_.nomenclature().findById(id);
  if (!rowRes) {
    ui::showError(this, rowRes.error());
    return;
  }
  Nomenclature row = rowRes.value();
  row.id = NomenclatureId{0};
  if (!editItem(this, row)) {
    return;
  }
  const Result<NomenclatureId> newId = session_.nomenclature().insert(row);
  if (!newId) {
    ui::showError(this, newId.error());
    return;
  }
  reload();
}

void NomenclaturePage::edit()
{
  if (!Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()))) {
    return;
  }
  const NomenclatureId id = selectedId();
  if (!id) {
    return;
  }
  Result<Nomenclature> row = session_.nomenclature().findById(id);
  if (!row) {
    ui::showError(this, row.error());
    return;
  }
  if (!editItem(this, row.value())) {
    return;
  }
  const Status status = session_.nomenclature().update(row.value());
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void NomenclaturePage::removeSelected()
{
  const NomenclatureId id = selectedId();
  if (!id) {
    return;
  }
  if (QMessageBox::question(this, QStringLiteral("Видалення"),
                            QStringLiteral("Видалити номенклатуру?")) != QMessageBox::Yes) {
    return;
  }
  const Status status = session_.nomenclature().remove(id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void NomenclaturePage::reveal(NomenclatureId id)
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

void NomenclaturePage::applyFilter(const QString & text)
{
  proxy_->setFilterFixedString(text.trimmed());
}

} // namespace fin
