#include "ui/DocumentJournalPage.h"
#include "ui/DocumentJournalModel.h"

#include "ui/DocumentDialog.h"
#include "ui/HrOrderDialog.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/print/PrintForms.h"
#include "fin/prro/CheckboxClient.h"

#include <optional>

#include <QSignalBlocker>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

namespace fin {
namespace {

constexpr DocumentType kTypes[] = {
    DocumentType::OpeningBalance, DocumentType::Manual,          DocumentType::CashIn,
    DocumentType::CashOut,        DocumentType::Purchase,        DocumentType::Sale,
    DocumentType::StockTransfer,  DocumentType::KitAssembly,     DocumentType::KitDisassembly,
    DocumentType::CustomerOrder,   DocumentType::SupplierOrder,
    DocumentType::MaterialIssue,  DocumentType::ProductionReport, DocumentType::ServiceSale,
    DocumentType::ServicePurchase, DocumentType::CustomerInvoice, DocumentType::BankPayment,
    DocumentType::SaleReturn,     DocumentType::PurchaseReturn,  DocumentType::WriteOff,
    DocumentType::Stocktake,      DocumentType::PeriodResult,    DocumentType::ExpenseReport,
    DocumentType::FaCommission,   DocumentType::FaDepreciation,  DocumentType::FaWriteOff,
    DocumentType::IaCommission,   DocumentType::IaDepreciation,  DocumentType::IaWriteOff,
    DocumentType::MsReceipt,      DocumentType::MsIssue,         DocumentType::TaxInvoice,
    DocumentType::Payroll,        DocumentType::VacationPayroll, DocumentType::SickLeavePayroll};

} // namespace

DocumentJournalPage::DocumentJournalPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * bar = new QHBoxLayout;
  from_ = new QDateEdit(QDate(QDate::currentDate().year(), 1, 1));
  to_ = new QDateEdit(QDate::currentDate());
  from_->setCalendarPopup(true);
  to_->setCalendarPopup(true);
  from_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  to_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  typeFilter_ = new QComboBox;
  typeFilter_->addItem(QStringLiteral("Усі типи"), -1);
  for (DocumentType type : kTypes) {
    typeFilter_->addItem(documentTypeLabelUk(type), static_cast<int>(type));
  }
  counterpartyFilter_ = new QComboBox;
  ui::fillCounterpartyCombo(counterpartyFilter_, session_, {}, true);
  counterpartyFilter_->setItemText(0, QStringLiteral("Усі контрагенти"));
  auto * refresh = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                   QStringLiteral("Оновити"));
  createBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                               QStringLiteral("Створити"));
  createHrBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/users.svg")),
                                 QStringLiteral("Кадровий наказ"));
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(typeFilter_, 1);
  bar->addWidget(counterpartyFilter_, 1);
  bar->addWidget(refresh);

  searchBar_ = new QLineEdit;
  searchBar_->setPlaceholderText(QStringLiteral("Пошук..."));
  searchBar_->setClearButtonEnabled(true);
  bar->addWidget(searchBar_);

  bar->addStretch();
  bar->addWidget(createBtn_);
  bar->addWidget(createHrBtn_);

  model_ = new DocumentJournalModel(this);
  proxy_ = new QSortFilterProxyModel(this);
  proxy_->setSourceModel(model_);
  proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  proxy_->setFilterKeyColumn(-1); // search all columns

  table_ = new QTableView;
  table_->setModel(proxy_);
  table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  reason_ = new QLabel;
  reason_->setWordWrap(true);

  layout->addLayout(bar);
  layout->addWidget(table_);
  layout->addWidget(reason_);

  QSettings settings;
  from_->setDate(
      settings.value(QStringLiteral("journal/from"), QDate(QDate::currentDate().year(), 1, 1))
          .toDate());
  to_->setDate(settings.value(QStringLiteral("journal/to"), QDate::currentDate()).toDate());
  if (settings.contains(QStringLiteral("journal/headerState"))) {
    table_->horizontalHeader()->restoreState(
        settings.value(QStringLiteral("journal/headerState")).toByteArray());
  }

  connect(refresh, &QPushButton::clicked, this, &DocumentJournalPage::reload);
  connect(searchBar_, &QLineEdit::textChanged, this, &DocumentJournalPage::applyFilter);
  connect(from_, &QDateEdit::dateChanged, this, [this](const QDate & d) {
    QSettings s;
    s.setValue(QStringLiteral("journal/from"), d);
  });
  connect(to_, &QDateEdit::dateChanged, this, [this](const QDate & d) {
    QSettings s;
    s.setValue(QStringLiteral("journal/to"), d);
  });
  connect(table_->horizontalHeader(), &QHeaderView::sectionResized, this, [this]() {
    QSettings s;
    s.setValue(QStringLiteral("journal/headerState"), table_->horizontalHeader()->saveState());
  });
  connect(table_->horizontalHeader(), &QHeaderView::sectionMoved, this, [this]() {
    QSettings s;
    s.setValue(QStringLiteral("journal/headerState"), table_->horizontalHeader()->saveState());
  });
  connect(from_, &QDateEdit::dateChanged, this, &DocumentJournalPage::reload);
  connect(to_, &QDateEdit::dateChanged, this, &DocumentJournalPage::reload);
  connect(typeFilter_, &QComboBox::currentIndexChanged, this, &DocumentJournalPage::reload);
  connect(counterpartyFilter_, &QComboBox::currentIndexChanged, this, &DocumentJournalPage::reload);
  connect(createBtn_, &QPushButton::clicked, this, &DocumentJournalPage::createOfType);
  connect(createHrBtn_, &QPushButton::clicked, this, &DocumentJournalPage::createHrOrder);
  connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
          [this](const QModelIndex &, const QModelIndex &) { refreshActions(); });
  connect(table_, &QTableView::doubleClicked, this, &DocumentJournalPage::openSelected);

  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint & pos) {
    const DocumentId id = selectedId();
    if (!id)
      return;
    const bool posted = selectedPosted();
    const DocumentType type = selectedType();
    const UserRole role = Access::currentRoleOrViewer(session_.handle());
    const bool canWrite = Access::canWriteDocument(role, type);
    bool canCreateAny = false;
    for (DocumentType candidate : kTypes) {
      if (Access::canWriteDocument(role, candidate)) {
        canCreateAny = true;
        break;
      }
    }

    QMenu menu(this);
    QAction * openAction =
        menu.addAction(QIcon(QStringLiteral(":/help/icons/edit.svg")), QStringLiteral("Відкрити"));
    connect(openAction, &QAction::triggered, this, &DocumentJournalPage::openSelected);

    QAction * copyAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/add.svg")),
                                          QStringLiteral("Копіювати (F9)"));
    copyAction->setEnabled(canWrite);
    connect(copyAction, &QAction::triggered, this, &DocumentJournalPage::copySelected);

    if (!basedOnTargets(type).empty()) {
      QAction * basedOnAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/add.svg")),
                                               QStringLiteral("На підставі"));
      basedOnAction->setEnabled(canCreateAny);
      connect(basedOnAction, &QAction::triggered, this, &DocumentJournalPage::basedOnSelected);
    }

    menu.addSeparator();

    if (requiresLedgerPosting(type)) {
      if (posted) {
        QAction * unpostAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/unpost.svg")),
                                                QStringLiteral("Зняти проведення"));
        unpostAction->setEnabled(canWrite);
        connect(unpostAction, &QAction::triggered, this, &DocumentJournalPage::unpostSelected);
      } else {
        QAction * postAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/post.svg")),
                                              QStringLiteral("Провести"));
        postAction->setEnabled(canWrite);
        connect(postAction, &QAction::triggered, this, &DocumentJournalPage::postSelected);
      }
    }

    QAction * removeAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/delete.svg")),
                                            QStringLiteral("Видалити"));
    removeAction->setEnabled(!posted && canWrite);
    connect(removeAction, &QAction::triggered, this, &DocumentJournalPage::removeSelected);

    if (canPrintDocument(type)) {
      menu.addSeparator();
      QAction * printAction = menu.addAction(QIcon(QStringLiteral(":/help/icons/print.svg")),
                                             QStringLiteral("Друк / PDF"));
      connect(printAction, &QAction::triggered, this, &DocumentJournalPage::printSelected);
    }

    menu.exec(table_->viewport()->mapToGlobal(pos));
  });

  auto * ins = new QShortcut(Qt::Key_Insert, this);
  connect(ins, &QShortcut::activated, createBtn_, &QPushButton::click);

  auto * f9 = new QShortcut(Qt::Key_F9, this);
  connect(f9, &QShortcut::activated, this, [this]() {
    const DocumentId id = selectedId();
    if (id &&
        Access::canWriteDocument(Access::currentRoleOrViewer(session_.handle()), selectedType())) {
      copySelected();
    }
  });

  auto * del = new QShortcut(QKeySequence::Delete, this);
  connect(del, &QShortcut::activated, this, [this]() {
    const DocumentId id = selectedId();
    if (!id)
      return;
    const UserRole role = Access::currentRoleOrViewer(session_.handle());
    if (!Access::canWriteDocument(role, selectedType()))
      return;
    if (selectedPosted()) {
      unpostSelected();
    } else {
      removeSelected();
    }
  });

  auto * enter = new QShortcut(Qt::Key_Return, table_);
  enter->setContext(Qt::WidgetShortcut);
  connect(enter, &QShortcut::activated, this, &DocumentJournalPage::openSelected);
  reload();
}

void DocumentJournalPage::reload()
{
  {
    const QSignalBlocker blocker(counterpartyFilter_);
    const CounterpartyId current = ui::selectedCounterparty(counterpartyFilter_);
    ui::fillCounterpartyCombo(counterpartyFilter_, session_, current, true);
    if (counterpartyFilter_->count() > 0) {
      counterpartyFilter_->setItemText(0, QStringLiteral("Усі контрагенти"));
    }
  }
  if (from_->date() > to_->date()) {
    ui::showError(this, QStringLiteral("дата початку не може бути пізніше дати кінця"));
    return;
  }
  std::optional<DocumentType> type;
  const int typeValue = typeFilter_->currentData().toInt();
  if (typeValue >= 0) {
    type = static_cast<DocumentType>(typeValue);
  }
  const Result<std::vector<DocumentHeader>> list = session_.documents().list(
      from_->date(), to_->date(), type, ui::selectedCounterparty(counterpartyFilter_));
  if (!list) {
    ui::showError(this, list.error());
    return;
  }

  model_->setDocuments(list.value());
  refreshActions();
  applyFilter(searchBar_->text());
}

void DocumentJournalPage::applyFilter(const QString & text)
{
  proxy_->setFilterFixedString(text.trimmed());
}

DocumentId DocumentJournalPage::selectedId() const
{
  const int row = table_->selectionModel()->currentIndex().row();
  if (row < 0 || row >= proxy_->rowCount()) {
    return DocumentId{0};
  }
  const QModelIndex srcIndex = proxy_->mapToSource(proxy_->index(row, 0));
  return model_->documentAt(srcIndex.row()).id;
}

DocumentType DocumentJournalPage::selectedType() const
{
  const int row = table_->selectionModel()->currentIndex().row();
  if (row < 0 || row >= proxy_->rowCount()) {
    return DocumentType::Manual;
  }
  const QModelIndex srcIndex = proxy_->mapToSource(proxy_->index(row, 0));
  return model_->documentAt(srcIndex.row()).type;
}

bool DocumentJournalPage::selectedPosted() const
{
  const int row = table_->selectionModel()->currentIndex().row();
  if (row < 0 || row >= proxy_->rowCount()) {
    return false;
  }
  const QModelIndex srcIndex = proxy_->mapToSource(proxy_->index(row, 0));
  return model_->documentAt(srcIndex.row()).posted;
}

void DocumentJournalPage::refreshActions()
{
  const DocumentId id = selectedId();
  const bool posted = selectedPosted();
  const DocumentType type = selectedType();
  const UserRole role = Access::currentRoleOrViewer(session_.handle());
  bool canCreateAny = false;
  for (DocumentType candidate : kTypes) {
    if (Access::canWriteDocument(role, candidate)) {
      canCreateAny = true;
      break;
    }
  }
  const bool canWrite = Access::canWriteDocument(role, type);
  createBtn_->setEnabled(canCreateAny);
  createBtn_->setToolTip(
      canCreateAny ? QString() : QStringLiteral("недостатньо прав для створення документів"));
  if (!id) {
    reason_->setText(QStringLiteral("Оберіть рядок, щоб відкрити, провести чи друкувати."));
  } else if (!canWrite && !posted) {
    reason_->setText(
        QStringLiteral("Перегляд. Можна відкрити й друкувати, змінювати цей тип не можна."));
  } else if (posted) {
    reason_->setText(QStringLiteral("Проведено. Можна відкрити, копіювати, "
                                    "друкувати або зняти проведення."));
  } else if (!requiresLedgerPosting(type)) {
    reason_->setText(QStringLiteral("Рахунок на оплату не проводиться. Можна "
                                    "змінити, друкувати або видалити."));
  } else {
    reason_->setText(QStringLiteral("Чернетка. Можна змінити, провести, копіювати або видалити."));
  }
}

void DocumentJournalPage::createHrOrder()
{
  Document doc;
  doc.type = DocumentType::Manual;
  doc.date = QDate::currentDate();

  HrOrderDialog dialog(session_, std::move(doc), this);
  dialog.exec();
  if (dialog.persisted()) {
    reload();
  }
}

void DocumentJournalPage::createOfType()
{
  const UserRole role = Access::currentRoleOrViewer(session_.handle());
  QMenu menu(this);
  for (DocumentType type : kTypes) {
    if (!Access::canWriteDocument(role, type)) {
      continue;
    }
    QAction * action = menu.addAction(documentTypeLabelUk(type));
    connect(action, &QAction::triggered, this, [this, type]() {
      if (DocumentDialog::createNew(session_, type, this)) {
        reload();
      }
    });
  }
  if (menu.isEmpty()) {
    return;
  }
  menu.exec(QCursor::pos());
}

void DocumentJournalPage::openSelected()
{
  const DocumentId id = selectedId();
  if (!id) {
    return;
  }
  if (DocumentDialog::editExisting(session_, id, this)) {
    reload();
  }
}

void DocumentJournalPage::copySelected()
{
  const DocumentId id = selectedId();
  if (!id) {
    return;
  }
  Result<Document> loaded = session_.documents().findById(id);
  if (!loaded) {
    ui::showError(this, loaded.error());
    return;
  }
  Document copy = loaded.value();
  copy.id = {};
  copy.posted = false;
  copy.basedOnId = {};
  copy.vatRegistrationDate = QDate();
  copy.date = QDate::currentDate();
  const Result<QString> number = session_.documents().allocateNumber(copy.type);
  if (!number) {
    ui::showError(this, number.error());
    return;
  }
  copy.number = number.value();
  if (DocumentDialog::openDocument(session_, copy, this)) {
    reload();
  }
}

void DocumentJournalPage::basedOnSelected()
{
  const DocumentId id = selectedId();
  if (!id) {
    return;
  }
  Result<Document> loaded = session_.documents().findById(id);
  if (!loaded) {
    ui::showError(this, loaded.error());
    return;
  }
  const std::vector<DocumentType> targets = basedOnTargets(loaded.value().type);
  if (targets.empty()) {
    return;
  }
  const UserRole role = Access::currentRoleOrViewer(session_.handle());
  QMenu menu(this);
  for (DocumentType type : targets) {
    if (!Access::canWriteDocument(role, type)) {
      continue;
    }
    QAction * action = menu.addAction(documentTypeLabelUk(type));
    connect(action, &QAction::triggered, this, [this, source = loaded.value(), type]() {
      if (DocumentDialog::createBasedOn(session_, source, type, this)) {
        reload();
      }
    });
  }
  if (menu.isEmpty()) {
    ui::showError(this, QStringLiteral("недостатньо прав для введення на підставі"));
    return;
  }
  menu.exec(QCursor::pos());
}

void DocumentJournalPage::postSelected()
{
  const DocumentId id = selectedId();
  if (!id) {
    return;
  }
  const Status status = session_.posting().post(id);
  if (!status) {
    ui::showError(this, status);
    return;
  }

  Result<Document> doc = session_.documents().findById(id);
  if (doc &&
      (doc.value().type == DocumentType::Sale || doc.value().type == DocumentType::ServiceSale ||
       doc.value().type == DocumentType::SaleReturn)) {
    const Status fiscal = tryFiscalizeSaleDocument(session_.handle(), id);
    if (!fiscal) {
      QMessageBox::warning(this, QStringLiteral("ПРРО"),
                           QStringLiteral("Документ проведено, але фіскальний чек не створено:\n%1")
                               .arg(fiscal.message()));
    } else {
      doc = session_.documents().findById(id);
      if (doc && !doc.value().fiscalCode.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("ПРРО"),
            QStringLiteral("Фіскальний чек Checkbox: %1").arg(doc.value().fiscalCode));
      }
    }
  }

  QSettings settings;
  if (settings.value(QStringLiteral("ui/sound_enabled"), true).toBool()) {
    QApplication::beep();
  }

  reload();
}

void DocumentJournalPage::unpostSelected()
{
  const DocumentId id = selectedId();
  if (!id) {
    return;
  }
  Result<Document> doc = session_.documents().findById(id);
  if (doc && (!doc.value().fiscalCode.trimmed().isEmpty() ||
              !doc.value().fiscalReceiptId.trimmed().isEmpty())) {
    const auto answer = QMessageBox::question(
        this, QStringLiteral("ПРРО"),
        QStringLiteral("Документ фіскалізовано (чек %1). Анулювати чек у Checkbox "
                       "і розпровести документ?")
            .arg(doc.value().fiscalCode.isEmpty() ? doc.value().fiscalReceiptId
                                                  : doc.value().fiscalCode),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return;
    }
    const Status voided = tryVoidFiscalReceipt(session_.handle(), id);
    if (!voided) {
      ui::showError(this, voided);
      return;
    }
  }
  const Status status = session_.posting().unpost(id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void DocumentJournalPage::removeSelected()
{
  const DocumentId id = selectedId();
  if (!id) {
    return;
  }
  if (QMessageBox::question(this, QStringLiteral("Видалення"),
                            QStringLiteral("Видалити документ?")) != QMessageBox::Yes) {
    return;
  }
  const Status status = session_.documents().remove(id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void DocumentJournalPage::printSelected()
{
  const DocumentId id = selectedId();
  if (!id) {
    return;
  }
  Result<Document> document = session_.documents().findById(id);
  if (!document) {
    ui::showError(this, document.error());
    return;
  }
  const Result<QString> html = PrintForms(session_.handle()).documentHtml(document.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  ui::exportReport(this, documentTypeLabelUk(document.value().type),
                   QStringLiteral("document-%1.pdf").arg(document.value().number), html.value(),
                   QString(), QString());
}

} // namespace fin
