#include "ui/DocumentDialog.h"

#include "ui/CashExpenseDialogs.h"
#include "ui/FaDialogs.h"
#include "ui/FormActions.h"
#include "ui/HelpBrowser.h"
#include "ui/HrOrderDialog.h"
#include "ui/IaDialogs.h"
#include "ui/MsDialogs.h"
#include "ui/PayrollDialog.h"
#include "ui/SalePurchaseDialogs.h"
#include "ui/SickLeaveDialog.h"
#include "ui/TaxInvoiceDialog.h"
#include "ui/TypedDialogs.h"
#include "ui/UiSupport.h"
#include "ui/VacationDialog.h"

#include "fin/core/Quantity.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"
#include "fin/domain/FixedAsset.h"
#include "fin/domain/IntangibleAsset.h"
#include "fin/domain/LowValueItem.h"
#include "fin/domain/MonthClose.h"
#include "fin/domain/Vat.h"

#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {
namespace {

constexpr DocumentType kAllTypes[] = {
    DocumentType::OpeningBalance,   DocumentType::Manual,          DocumentType::CashIn,
    DocumentType::CashOut,          DocumentType::Purchase,        DocumentType::Sale,
    DocumentType::ServiceSale,      DocumentType::ServicePurchase, DocumentType::CustomerInvoice,
    DocumentType::BankPayment,      DocumentType::SaleReturn,      DocumentType::PurchaseReturn,
    DocumentType::WriteOff,         DocumentType::Stocktake,       DocumentType::PeriodResult,
    DocumentType::FaCommission,     DocumentType::FaDepreciation,  DocumentType::FaWriteOff,
    DocumentType::IaCommission,     DocumentType::IaDepreciation,  DocumentType::IaWriteOff,
    DocumentType::MsReceipt,        DocumentType::MsIssue,         DocumentType::MaterialIssue,
    DocumentType::ProductionReport, DocumentType::TaxInvoice,      DocumentType::Payroll,
    DocumentType::ExpenseReport};

} // namespace

DocumentDialog::DocumentDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(document_.type));
  resize(1100, 560);
  setWhatsThis(
      QStringLiteral("Форма бухгалтерської проводки: дебет, кредит, сума. Для товару й каси "
                     "відкривайте відповідні документи з журналу — там немає колонок Дт/Кт."));

  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
  }

  type_ = new QComboBox;
  for (DocumentType type : kAllTypes) {
    type_->addItem(documentTypeLabelUk(type), static_cast<int>(type));
    if (type == document_.type) {
      type_->setCurrentIndex(type_->count() - 1);
    }
  }
  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  counterparty_ = new QComboBox;
  ui::fillCounterpartyCombo(counterparty_, session_, document_.counterpartyId, true);

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Тип"), type_);
  form->addRow(QStringLiteral("Номер"), number_);
  form->addRow(QStringLiteral("Дата"), date_);
  form->addRow(QStringLiteral("Контрагент"), counterparty_);

  lines_ = new QTableWidget;
  lines_->setColumnCount(7);
  lines_->setHorizontalHeaderLabels({QStringLiteral("Дебет"), QStringLiteral("Кредит"),
                                     QStringLiteral("Номенклатура"), QStringLiteral("К-сть"),
                                     QStringLiteral("Ціна"), QStringLiteral("Сума"),
                                     QStringLiteral("Зміст")});
  lines_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
  lines_->verticalHeader()->setVisible(false);

  auto * addLine = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                   QStringLiteral("Додати рядок"));
  auto * removeLine = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/delete.svg")),
                                      QStringLiteral("Видалити рядок"));
  auto * lineBar = new QHBoxLayout;
  lineBar->addWidget(addLine);
  lineBar->addWidget(removeLine);
  lineBar->addStretch();

  comment_ = new QTextEdit(document_.comment);
  comment_->setAcceptRichText(false);
  comment_->setMaximumHeight(70);
  hint_ = new QLabel;
  hint_->setWordWrap(true);
  hint_->hide();
  reason_ = new QLabel;
  reason_->setWordWrap(true);

  saveBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                             QStringLiteral("Зберегти"));
  postBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                             QStringLiteral("Провести"));
  unpostBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")),
                               QStringLiteral("Зняти проведення"));
  auto * help = new QPushButton(QStringLiteral("Довідка"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  buttons->addWidget(saveBtn_);
  buttons->addWidget(postBtn_);
  buttons->addWidget(unpostBtn_);
  buttons->addWidget(help);
  buttons->addStretch();
  buttons->addWidget(close);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addLayout(lineBar);
  layout->addWidget(lines_, 1);
  layout->addWidget(hint_);
  layout->addWidget(reason_);
  layout->addWidget(new QLabel(QStringLiteral("Коментар")));
  layout->addWidget(comment_);
  layout->addLayout(buttons);

  if (document_.lines.empty()) {
    applyTypeDefaults();
  } else {
    for (const DocumentLine & line : document_.lines) {
      addLineRow(line);
    }
  }

  connect(addLine, &QPushButton::clicked, this, [this]() {
    const auto type = static_cast<DocumentType>(type_->currentData().toInt());
    const std::vector<DocumentLine> seeded = defaultLinesForType(type, accounts_);
    addLineRow(seeded.empty() ? DocumentLine{} : seeded.front());
    refreshGate();
  });
  connect(removeLine, &QPushButton::clicked, this, [this]() {
    const int row = lines_->currentRow();
    if (row >= 0) {
      lines_->removeRow(row);
      refreshGate();
    }
  });
  connect(saveBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = this->save();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Документ збережено"));
    }
    refreshGate();
  });
  connect(postBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = saveAndPost();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Документ проведено"));
    }
    refreshGate();
  });
  connect(unpostBtn_, &QPushButton::clicked, this, &DocumentDialog::unpost);
  connect(help, &QPushButton::clicked, this, [this]() {
    HelpBrowser::openTopic(this, HelpBrowser::topicForDocument(document_.type));
  });
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, [this]() {
    HelpBrowser::openTopic(this, HelpBrowser::topicForDocument(document_.type));
  });

  auto * ctrlS = new QShortcut(QKeySequence::Save, this);
  connect(ctrlS, &QShortcut::activated, saveBtn_, &QPushButton::click);

  auto * ctrlEnter = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
  connect(ctrlEnter, &QShortcut::activated, this, [this]() {
    if (postBtn_->isEnabled()) {
      postBtn_->click();
      if (document_.posted) {
        accept();
      }
    }
  });

  type_->setEnabled(false);
  connect(number_, &QLineEdit::textChanged, this, &DocumentDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &DocumentDialog::refreshGate);
  connect(counterparty_, &QComboBox::currentIndexChanged, this, &DocumentDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

bool DocumentDialog::createNew(LedgerSession & session, DocumentType type, QWidget * parent)
{
  if (type == DocumentType::PeriodResult) {
    const QDate today = QDate::currentDate();
    const Result<Document> built =
        buildPeriodResultDocument(session.handle(), today.year(), today.month());
    if (!built) {
      ui::showError(parent, built.error());
      return false;
    }
    return openDocument(session, built.value(), parent);
  }
  if (type == DocumentType::FaDepreciation) {
    const QDate today = QDate::currentDate();
    const Result<Document> built =
        buildFaDepreciationDocument(session.handle(), today.year(), today.month());
    if (built) {
      return openDocument(session, built.value(), parent);
    }
    if (built.error().code == ErrorCode::Conflict) {
      ui::showError(parent, built.error());
      return false;
    }
    Document document;
    document.type = type;
    document.date = QDate(today.year(), today.month(), today.daysInMonth());
    Result<QString> number = session.documents().allocateNumber(type);
    if (!number) {
      ui::showError(parent, number.error());
      return false;
    }
    document.number = number.value();
    return openDocument(session, document, parent);
  }
  if (type == DocumentType::IaDepreciation) {
    const QDate today = QDate::currentDate();
    const Result<Document> built =
        buildIaDepreciationDocument(session.handle(), today.year(), today.month());
    if (built) {
      return openDocument(session, built.value(), parent);
    }
    if (built.error().code == ErrorCode::Conflict) {
      ui::showError(parent, built.error());
      return false;
    }
    Document document;
    document.type = type;
    document.date = QDate(today.year(), today.month(), today.daysInMonth());
    Result<QString> number = session.documents().allocateNumber(type);
    if (!number) {
      ui::showError(parent, number.error());
      return false;
    }
    document.number = number.value();
    return openDocument(session, document, parent);
  }
  Document document;
  document.type = type;
  document.date = QDate::currentDate();
  Result<QString> number = session.documents().allocateNumber(type);
  if (!number) {
    ui::showError(parent, number.error());
    return false;
  }
  document.number = number.value();
  return openDocument(session, document, parent);
}

bool DocumentDialog::createBasedOn(LedgerSession & session, const Document & source,
                                   DocumentType target, QWidget * parent)
{
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  if (!accounts) {
    ui::showError(parent, accounts.error());
    return false;
  }
  Result<Document> built =
      Result<Document>::fail(Error::validation(QStringLiteral("не вдалося скласти документ")));
  if (target == DocumentType::TaxInvoice) {
    const Result<CompanyProfile> company = session.company().load();
    if (!company) {
      ui::showError(parent, company.error());
      return false;
    }
    built = composeTaxInvoice(session.handle(), accounts.value(), source, company.value(),
                              QDate::currentDate());
  } else {
    built = buildBasedOn(source, target, accounts.value(), session.handle());
  }
  if (!built) {
    ui::showError(parent, built.error());
    return false;
  }
  Result<QString> number = session.documents().allocateNumber(target);
  if (!number) {
    ui::showError(parent, number.error());
    return false;
  }
  built.value().number = number.value();
  return openDocument(session, built.value(), parent);
}

bool DocumentDialog::editExisting(LedgerSession & session, DocumentId id, QWidget * parent)
{
  Result<Document> document = session.documents().findById(id);
  if (!document) {
    ui::showError(parent, document.error());
    return false;
  }
  return openDocument(session, document.value(), parent);
}

bool DocumentDialog::openDocument(LedgerSession & session, Document document, QWidget * parent)
{
  switch (documentEditorKind(document.type)) {
  case DocumentEditorKind::Goods: {
    GoodsDocumentDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::Cash: {
    CashDocumentDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::Bank: {
    BankDocumentDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::ServicePurchase: {
    ServicePurchaseDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::Opening: {
    OpeningDocumentDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::FaCommission: {
    FaCommissionDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::FaDepreciation: {
    FaDepreciationDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::FaWriteOff: {
    FaWriteOffDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::IaCommission: {
    IaCommissionDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::IaDepreciation: {
    IaDepreciationDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::IaWriteOff: {
    IaWriteOffDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::Ms: {
    MsDocumentDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::TaxInvoice: {
    TaxInvoiceDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::VacationPayroll: {
    VacationDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::SickLeavePayroll: {
    SickLeaveDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::Payroll: {
    PayrollDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::ExpenseReport: {
    ExpenseReportDialog dialog(session, std::move(document), parent);
    dialog.exec();
    return dialog.persisted();
  }
  case DocumentEditorKind::Generic:
    break;
  }

  if (document.type == DocumentType::Manual) {
    if (document.id && session.hrOrders().findByDocument(document.id)) {
      HrOrderDialog dialog(session, std::move(document), parent);
      dialog.exec();
      return dialog.persisted();
    }
  }
  DocumentDialog dialog(session, std::move(document), parent);
  dialog.exec();
  return dialog.persisted();
}

void DocumentDialog::bindQtyPrice(QLineEdit * qty, QLineEdit * price, QLineEdit * amount)
{
  const auto recalc = [qty, price, amount]() {
    if (qty->text().trimmed().isEmpty() || price->text().trimmed().isEmpty()) {
      return;
    }
    const Result<Quantity> quantity = Quantity::fromDisplayString(qty->text());
    const Result<Money> unit = Money::fromDisplayString(price->text());
    if (!quantity || !unit) {
      return;
    }
    const Result<Money> sum = quantity.value().times(unit.value());
    if (sum) {
      amount->setText(sum.value().toDisplay());
    }
  };
  connect(qty, &QLineEdit::editingFinished, this, recalc);
  connect(price, &QLineEdit::editingFinished, this, recalc);
}

void DocumentDialog::addLineRow(const DocumentLine & line)
{
  const int row = lines_->rowCount();
  lines_->insertRow(row);
  auto * debit = new QComboBox;
  auto * credit = new QComboBox;
  ui::fillAccountCombo(debit, accounts_, line.debitAccountId);
  ui::fillAccountCombo(credit, accounts_, line.creditAccountId);
  auto * nomenclature = new QComboBox;
  ui::fillNomenclatureCombo(nomenclature, session_, line.nomenclatureId);
  auto * qty = new QLineEdit(line.quantity.isZero() ? QString() : line.quantity.toDisplay());
  qty->setAlignment(Qt::AlignRight);
  auto * price = new QLineEdit(line.unitPrice.isZero() ? QString() : line.unitPrice.toDisplay());
  price->setAlignment(Qt::AlignRight);
  auto * amount = new QLineEdit(line.amount.isZero() ? QString() : line.amount.toDisplay());
  amount->setAlignment(Qt::AlignRight);
  auto * description = new QLineEdit(line.description);
  bindQtyPrice(qty, price, amount);
  bool lockDebit = false;
  bool lockCredit = false;
  accountLocksForType(static_cast<DocumentType>(type_->currentData().toInt()), lockDebit,
                      lockCredit);
  debit->setEnabled(!lockDebit);
  credit->setEnabled(!lockCredit);
  lines_->setCellWidget(row, 0, debit);
  lines_->setCellWidget(row, 1, credit);
  lines_->setCellWidget(row, 2, nomenclature);
  lines_->setCellWidget(row, 3, qty);
  lines_->setCellWidget(row, 4, price);
  lines_->setCellWidget(row, 5, amount);
  lines_->setCellWidget(row, 6, description);
  connect(debit, &QComboBox::currentIndexChanged, this, &DocumentDialog::refreshGate);
  connect(credit, &QComboBox::currentIndexChanged, this, &DocumentDialog::refreshGate);
  connect(nomenclature, &QComboBox::currentIndexChanged, this, &DocumentDialog::refreshGate);
  connect(qty, &QLineEdit::textChanged, this, &DocumentDialog::refreshGate);
  connect(price, &QLineEdit::textChanged, this, &DocumentDialog::refreshGate);
  connect(amount, &QLineEdit::textChanged, this, &DocumentDialog::refreshGate);
  connect(description, &QLineEdit::textChanged, this, &DocumentDialog::refreshGate);
}

void DocumentDialog::applyTypeDefaults()
{
  lines_->setRowCount(0);
  const auto type = static_cast<DocumentType>(type_->currentData().toInt());
  for (const DocumentLine & line : defaultLinesForType(type, accounts_)) {
    addLineRow(line);
  }
  if (hint_ != nullptr) {
    const bool sale = type == DocumentType::Sale;
    hint_->setVisible(sale);
    hint_->setText(sale ? QStringLiteral("Сума списання 281 підставляється за середньою "
                                         "собівартістю під час проведення.")
                        : QString());
  }
}

Status DocumentDialog::collect(Document & out) const
{
  out = document_;
  out.type = static_cast<DocumentType>(type_->currentData().toInt());
  out.number = number_->text().trimmed();
  out.date = date_->date();
  out.counterpartyId = ui::selectedCounterparty(counterparty_);
  out.comment = comment_->toPlainText().trimmed();
  out.lines.clear();
  for (int row = 0; row < lines_->rowCount(); ++row) {
    auto * debit = qobject_cast<QComboBox *>(lines_->cellWidget(row, 0));
    auto * credit = qobject_cast<QComboBox *>(lines_->cellWidget(row, 1));
    auto * nomenclature = qobject_cast<QComboBox *>(lines_->cellWidget(row, 2));
    auto * qtyEdit = qobject_cast<QLineEdit *>(lines_->cellWidget(row, 3));
    auto * priceEdit = qobject_cast<QLineEdit *>(lines_->cellWidget(row, 4));
    auto * amountEdit = qobject_cast<QLineEdit *>(lines_->cellWidget(row, 5));
    auto * description = qobject_cast<QLineEdit *>(lines_->cellWidget(row, 6));
    if (debit == nullptr || credit == nullptr || amountEdit == nullptr) {
      return Status::fail(Error::validation(QStringLiteral("пошкоджений рядок документа")));
    }
    if (amountEdit->text().trimmed().isEmpty() && !ui::selectedAccount(debit) &&
        !ui::selectedAccount(credit)) {
      continue;
    }
    const Result<Money> amount = Money::fromDisplayString(amountEdit->text());
    if (!amount) {
      return Status::fail(amount.error());
    }
    DocumentLine line;
    line.debitAccountId = ui::selectedAccount(debit);
    line.creditAccountId = ui::selectedAccount(credit);
    line.amount = amount.value();
    if (nomenclature != nullptr) {
      line.nomenclatureId = ui::selectedNomenclature(nomenclature);
    }
    if (qtyEdit != nullptr && !qtyEdit->text().trimmed().isEmpty()) {
      const Result<Quantity> qty = Quantity::fromDisplayString(qtyEdit->text());
      if (!qty) {
        return Status::fail(qty.error());
      }
      line.quantity = qty.value();
    }
    if (priceEdit != nullptr && !priceEdit->text().trimmed().isEmpty()) {
      const Result<Money> price = Money::fromDisplayString(priceEdit->text());
      if (!price) {
        return Status::fail(price.error());
      }
      line.unitPrice = price.value();
    }
    line.description = description ? description->text().trimmed() : QString();
    out.lines.push_back(line);
  }
  return Status::ok();
}

Status DocumentDialog::save()
{
  Document collected;
  Status status = collect(collected);
  if (!status) {
    return status;
  }
  if (!collected.id) {
    Result<DocumentId> id = session_.documents().insert(collected);
    if (!id) {
      return id.asStatus();
    }
    document_ = collected;
    document_.id = id.value();
  } else {
    status = session_.documents().update(collected);
    if (!status) {
      return status;
    }
    document_ = collected;
  }
  persisted_ = true;
  updatePostedUi();
  return Status::ok();
}

Status DocumentDialog::saveAndPost()
{
  Status status = save();
  if (!status) {
    return status;
  }
  status = session_.posting().post(document_.id);
  if (!status) {
    return status;
  }
  document_.posted = true;
  persisted_ = true;
  updatePostedUi();
  return Status::ok();
}

void DocumentDialog::unpost()
{
  if (!document_.id) {
    return;
  }
  const Status status = session_.posting().unpost(document_.id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  document_.posted = false;
  persisted_ = true;
  updatePostedUi();
  refreshGate();
}

void DocumentDialog::updatePostedUi()
{
  const bool posted = document_.posted;
  number_->setEnabled(!posted);
  date_->setEnabled(!posted);
  counterparty_->setEnabled(!posted);
  lines_->setEnabled(!posted);
  comment_->setEnabled(!posted);
  type_->setEnabled(false);
}

ui::FormButtons DocumentDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, nullptr, reason_};
}

void DocumentDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, document_.date,
                             [this](Document & out) { return collect(out); });
}

} // namespace fin
