#include "ui/CashExpenseDialogs.h"

#include "ui/DocumentDialog.h"
#include "ui/FormActions.h"
#include "ui/HelpBrowser.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Vat.h"
#include "fin/print/PrintForms.h"

#include <QAction>
#include <QComboBox>
#include <QCursor>
#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {
namespace {

void bindQtyPrice(QLineEdit * qty, QLineEdit * price, QLineEdit * amount)
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
  QObject::connect(qty, &QLineEdit::editingFinished, qty, recalc);
  QObject::connect(price, &QLineEdit::editingFinished, price, recalc);
}

static Result<CashLine> cashFromRow(const QTableWidget * table, int row)
{
  auto * account = qobject_cast<QComboBox *>(table->cellWidget(row, 0));
  auto * amountEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 1));
  auto * description = qobject_cast<QLineEdit *>(table->cellWidget(row, 2));
  if (account == nullptr || amountEdit == nullptr) {
    return Result<CashLine>::fail(Error::validation(QStringLiteral("пошкоджений рядок документа")));
  }
  CashLine line;
  line.correspondenceId = ui::selectedAccount(account);
  line.description = description ? description->text().trimmed() : QString();
  if (!amountEdit->text().trimmed().isEmpty()) {
    const Result<Money> amount = Money::fromDisplayString(amountEdit->text());
    if (!amount) {
      return Result<CashLine>::fail(amount.error());
    }
    line.amount = amount.value();
  }
  if (table->columnCount() >= 5) {
    line.vatRateBp = table->property("vatRateBp").toInt();
    auto * vatEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 2));
    auto * descVat = qobject_cast<QLineEdit *>(table->cellWidget(row, 4));
    line.description = descVat ? descVat->text().trimmed() : line.description;
    if (vatEdit != nullptr && !vatEdit->text().trimmed().isEmpty()) {
      const Result<Money> vat = Money::fromDisplayString(vatEdit->text());
      if (!vat) {
        return Result<CashLine>::fail(vat.error());
      }
      line.vatCents = vat.value().cents();
    }
  }
  return Result<CashLine>::ok(line);
}

void addFormButtons(QWidget * host, QHBoxLayout * buttons, QPushButton * save, QPushButton * post,
                    QPushButton * unpost, QPushButton * print, QPushButton * help,
                    QPushButton * close)
{
  Q_UNUSED(host);
  buttons->addWidget(save);
  buttons->addWidget(post);
  buttons->addWidget(unpost);
  if (print != nullptr) {
    buttons->addWidget(print);
  }
  buttons->addWidget(help);
  buttons->addStretch();
  buttons->addWidget(close);
}

} // namespace

CashDocumentDialog::CashDocumentDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(document_.type));
  resize(820, 480);
  setWhatsThis(document_.type == DocumentType::CashIn
                   ? QStringLiteral("ПКО: каса 301 по дебету, ви вказуєте, звідки "
                                    "надійшли гроші, і суму.")
                   : QStringLiteral("ВКО: каса 301 по кредиту, ви вказуєте, куди "
                                    "пішли гроші, і суму."));

  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
  }

  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  counterparty_ = new QComboBox;
  ui::fillCounterpartyCombo(counterparty_, session_, document_.counterpartyId, true);
  contract_ = new QComboBox;
  contract_->setObjectName(QStringLiteral("contractCombo"));
  ui::fillContractCombo(contract_, session_, document_.counterpartyId, document_.contractId);

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Номер"), number_);
  form->addRow(QStringLiteral("Дата"), date_);
  form->addRow(QStringLiteral("Контрагент"), counterparty_);
  form->addRow(QStringLiteral("Договір"), contract_);
  receivedBy_ = new QLineEdit(document_.receivedBy);
  identityDoc_ = new QLineEdit(document_.identityDoc);
  if (document_.type == DocumentType::CashOut) {
    form->addRow(QStringLiteral("Одержав"), receivedBy_);
    form->addRow(QStringLiteral("Документ особи"), identityDoc_);
  }

  table_ = new QTableWidget;
  table_->setColumnCount(3);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Кореспонденція"), QStringLiteral("Сума"), QStringLiteral("Зміст")});
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);

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
  auto * hint =
      new QLabel(document_.type == DocumentType::CashIn
                     ? QStringLiteral("Кореспонденція — кредит (звідки надходження до каси). "
                                      "Аванс від покупця — рахунок 681. Договір у шапці — "
                                      "субконто розрахунків, якщо воно обов’язкове.")
                     : QStringLiteral("Кореспонденція — дебет (куди списання з каси). "
                                      "Аванс постачальнику — рахунок 371. Договір у шапці — "
                                      "субконто розрахунків, якщо воно обов’язкове."));
  hint->setWordWrap(true);
  reason_ = new QLabel;
  reason_->setWordWrap(true);

  saveBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                             QStringLiteral("Зберегти"));
  postBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                             QStringLiteral("Провести"));
  unpostBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")),
                               QStringLiteral("Зняти проведення"));
  printBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                              QStringLiteral("Друк / PDF"));
  printBtn_->setVisible(canPrintCashOrder(document_.type));
  auto * help = new QPushButton(QStringLiteral("Довідка"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  addFormButtons(this, buttons, saveBtn_, postBtn_, unpostBtn_, printBtn_, help, close);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addLayout(lineBar);
  layout->addWidget(table_, 1);
  layout->addWidget(new QLabel(QStringLiteral("Підстава / коментар")));
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(buttons);

  const std::vector<CashLine> existing =
      extractCashLines(document_.type, document_.lines, accounts_);
  if (existing.empty()) {
    addRow(CashLine{});
  } else {
    for (const CashLine & row : existing) {
      addRow(row);
    }
  }

  connect(addLine, &QPushButton::clicked, this, [this]() {
    addRow(CashLine{});
    refreshGate();
  });
  connect(removeLine, &QPushButton::clicked, this, [this]() {
    const int row = table_->currentRow();
    if (row >= 0) {
      table_->removeRow(row);
      refreshGate();
    }
  });
  connect(saveBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = this->save();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Збережено"));
    }
    refreshGate();
  });
  connect(postBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = saveAndPost();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Проведено"));
    }
    refreshGate();
  });
  connect(unpostBtn_, &QPushButton::clicked, this, &CashDocumentDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &CashDocumentDialog::printForm);
  connect(help, &QPushButton::clicked, this, &CashDocumentDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &CashDocumentDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &CashDocumentDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &CashDocumentDialog::refreshGate);
  connect(counterparty_, &QComboBox::currentIndexChanged, this, [this]() {
    ui::fillContractCombo(contract_, session_, ui::selectedCounterparty(counterparty_), {});
    refreshGate();
  });
  connect(contract_, &QComboBox::currentIndexChanged, this, &CashDocumentDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

void CashDocumentDialog::addRow(const CashLine & row)
{
  const int index = table_->rowCount();
  table_->insertRow(index);
  auto * account = new QComboBox;
  ui::fillAccountCombo(account, accounts_, row.correspondenceId);
  auto * amount = new QLineEdit(row.amount.isZero() ? QString() : row.amount.toDisplay());
  amount->setAlignment(Qt::AlignRight);
  auto * description = new QLineEdit(row.description);
  table_->setCellWidget(index, 0, account);
  table_->setCellWidget(index, 1, amount);
  table_->setCellWidget(index, 2, description);
  connect(account, &QComboBox::currentIndexChanged, this, &CashDocumentDialog::refreshGate);
  connect(amount, &QLineEdit::textChanged, this, &CashDocumentDialog::refreshGate);
}

Status CashDocumentDialog::collect(Document & out) const
{
  out = document_;
  out.number = number_->text().trimmed();
  out.date = date_->date();
  out.counterpartyId = ui::selectedCounterparty(counterparty_);
  out.contractId = ui::selectedContract(contract_);
  out.comment = comment_->toPlainText().trimmed();
  out.receivedBy = receivedBy_ ? receivedBy_->text().trimmed() : QString();
  out.identityDoc = identityDoc_ ? identityDoc_->text().trimmed() : QString();
  std::vector<CashLine> rows;
  for (int row = 0; row < table_->rowCount(); ++row) {
    const Result<CashLine> line = cashFromRow(table_, row);
    if (!line) {
      return line.asStatus();
    }
    rows.push_back(line.value());
  }
  const Result<std::vector<DocumentLine>> composed = document_.type == DocumentType::CashIn
                                                         ? composeCashIn(accounts_, rows)
                                                         : composeCashOut(accounts_, rows);
  if (!composed) {
    return composed.asStatus();
  }
  out.lines = composed.value();
  return Status::ok();
}

Status CashDocumentDialog::save()
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

Status CashDocumentDialog::saveAndPost()
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

void CashDocumentDialog::unpost()
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

void CashDocumentDialog::printForm()
{
  Status status = Status::ok();
  if (!document_.id || !document_.posted) {
    status = save();
    if (!status) {
      ui::showError(this, status);
      return;
    }
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (!loaded) {
    ui::showError(this, loaded.error());
    return;
  }
  const Result<QString> html = PrintForms(session_.handle()).cashOrderHtml(loaded.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  QMenu menu(this);
  QAction * preview = menu.addAction(QStringLiteral("Друк…"));
  QAction * pdf = menu.addAction(QStringLiteral("Зберегти PDF…"));
  QAction * chosen = menu.exec(QCursor::pos());
  const bool incoming = canPrintCashReceipt(loaded.value().type);
  if (chosen == preview) {
    ui::printHtml(
        this,
        (incoming ? QStringLiteral("ПКО %1") : QStringLiteral("ВКО %1")).arg(loaded.value().number),
        html.value());
  } else if (chosen == pdf) {
    ui::savePdf(this,
                (incoming ? QStringLiteral("pko-%1.pdf") : QStringLiteral("vko-%1.pdf"))
                    .arg(loaded.value().number),
                html.value());
  }
}

void CashDocumentDialog::updatePostedUi()
{
  const bool posted = document_.posted;
  number_->setEnabled(!posted);
  date_->setEnabled(!posted);
  counterparty_->setEnabled(!posted);
  contract_->setEnabled(!posted);
  if (receivedBy_ != nullptr) {
    receivedBy_->setEnabled(!posted);
  }
  if (identityDoc_ != nullptr) {
    identityDoc_->setEnabled(!posted);
  }
  table_->setEnabled(!posted);
  comment_->setEnabled(!posted);
}

ui::FormButtons CashDocumentDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

void CashDocumentDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, document_.date,
                             [this](Document & out) { return collect(out); });
}

void CashDocumentDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("cash.html"));
}

static Result<ExpenseLine> expenseFromRow(const QTableWidget * table, int row)
{
  auto * docRefEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 0));
  auto * accCombo = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
  auto * nomCombo = qobject_cast<QComboBox *>(table->cellWidget(row, 2));
  auto * qtyEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 3));
  auto * priceEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 4));
  auto * amountEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 5));
  auto * vatCombo = qobject_cast<QComboBox *>(table->cellWidget(row, 6));
  auto * vatEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 7));
  auto * descEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 9));

  ExpenseLine line;
  if (docRefEdit != nullptr) {
    line.documentRef = docRefEdit->text().trimmed();
  }
  if (accCombo != nullptr) {
    line.expenseAccountId = ui::selectedAccount(accCombo);
  }
  if (nomCombo != nullptr) {
    line.nomenclatureId = ui::selectedNomenclature(nomCombo);
  }
  if (qtyEdit != nullptr && !qtyEdit->text().trimmed().isEmpty()) {
    const Result<Quantity> qty = Quantity::fromDisplayString(qtyEdit->text());
    if (!qty)
      return Result<ExpenseLine>::fail(qty.error());
    line.quantity = qty.value();
  }
  if (priceEdit != nullptr && !priceEdit->text().trimmed().isEmpty()) {
    const Result<Money> pr = Money::fromDisplayString(priceEdit->text());
    if (!pr)
      return Result<ExpenseLine>::fail(pr.error());
    line.unitPrice = pr.value();
  }
  if (amountEdit != nullptr && !amountEdit->text().trimmed().isEmpty()) {
    const Result<Money> amt = Money::fromDisplayString(amountEdit->text());
    if (!amt)
      return Result<ExpenseLine>::fail(amt.error());
    line.amount = amt.value();
  }
  if (vatCombo != nullptr) {
    line.vatRateBp = vatCombo->currentData().toInt();
  }
  if (vatEdit != nullptr && !vatEdit->text().trimmed().isEmpty()) {
    const Result<Money> vat = Money::fromDisplayString(vatEdit->text());
    if (!vat)
      return Result<ExpenseLine>::fail(vat.error());
    line.vatCents = vat.value().cents();
  }
  if (descEdit != nullptr) {
    line.description = descEdit->text().trimmed();
  }
  return Result<ExpenseLine>::ok(std::move(line));
}

ExpenseReportDialog::ExpenseReportDialog(LedgerSession & session, Document document,
                                         QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(DocumentType::ExpenseReport));
  resize(1150, 600);
  setWhatsThis(QStringLiteral("Авансовий звіт: облік коштів підзвітних осіб. Рахунок 3721. "
                              "Витрати (92, 91, 93, 231), ТМЦ на склад (201, 281, 22), податковий "
                              "кредит за чеками (6442)."));

  const Result<std::vector<Account>> accList = session_.accounts().listPostable();
  if (accList) {
    accounts_ = accList.value();
    for (const Account & acc : accounts_) {
      if (acc.code.startsWith(QLatin1String("9")) || acc.code.startsWith(QLatin1String("20")) ||
          acc.code.startsWith(QLatin1String("28")) || acc.code.startsWith(QLatin1String("22")) ||
          acc.code.startsWith(QLatin1String("23")) || acc.code.startsWith(QLatin1String("15")) ||
          acc.code.startsWith(QLatin1String("63")) || acc.code.startsWith(QLatin1String("371")) ||
          acc.code.startsWith(QLatin1String("71"))) {
        expenseAccounts_.push_back(acc);
      }
    }
    if (expenseAccounts_.empty()) {
      expenseAccounts_ = accounts_;
    }
  }

  const Result<CompanyProfile> company = session_.company().load();
  if (company) {
    vatPayer_ = company.value().vatPayer;
    vatRateBp_ = company.value().vatRateBp;
  }

  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));

  employee_ = new QComboBox;
  ui::fillEmployeeCombo(employee_, session_, document_.employeeId, false);

  auto * headerForm = new QFormLayout;
  headerForm->addRow(QStringLiteral("Номер звіту"), number_);
  headerForm->addRow(QStringLiteral("Дата"), date_);
  headerForm->addRow(QStringLiteral("Підзвітна особа"), employee_);

  table_ = new QTableWidget;
  table_->setColumnCount(11);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Документ (чек/квит.)"), QStringLiteral("Рахунок Дт"),
       QStringLiteral("Номенклатура"), QStringLiteral("К-сть"), QStringLiteral("Ціна"),
       QStringLiteral("Сума без ПДВ"), QStringLiteral("% ПДВ"), QStringLiteral("Сума ПДВ"),
       QStringLiteral("Всього"), QStringLiteral("Призначення витрат"), QStringLiteral("")});
  table_->horizontalHeader()->setSectionResizeMode(9, QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);

  const std::vector<ExpenseLine> rows = extractExpenseLines(document_.lines, accounts_);
  if (rows.empty()) {
    ExpenseLine def;
    def.expenseAccountId = accountIdByCode(accounts_, chart::kAdminExpense);
    def.vatRateBp = vatPayer_ ? vatRateBp_ : 0;
    addRow(def);
  } else {
    for (const ExpenseLine & row : rows) {
      addRow(row);
    }
  }

  auto * addRowBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                     QStringLiteral("+ Додати рядок витрат"));
  connect(addRowBtn, &QPushButton::clicked, this, [this]() {
    ExpenseLine def;
    def.expenseAccountId = accountIdByCode(accounts_, chart::kAdminExpense);
    def.vatRateBp = vatPayer_ ? vatRateBp_ : 0;
    addRow(def);
    updateTotal();
  });

  total_ = new QLabel;
  total_->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 11pt;"));

  comment_ = new QTextEdit(document_.comment);
  comment_->setAcceptRichText(false);
  comment_->setMaximumHeight(60);
  comment_->setPlaceholderText(QStringLiteral("Призначення авансу / коментар…"));

  reason_ = new QLabel;
  reason_->setWordWrap(true);

  saveBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                             QStringLiteral("Зберегти"));
  postBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                             QStringLiteral("Провести"));
  unpostBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")),
                               QStringLiteral("Розпровести"));
  printBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                              QStringLiteral("Друк (№ 841)…"));
  auto * helpBtn = new QPushButton(QStringLiteral("Довідка (F1)"));
  auto * closeBtn = new QPushButton(QStringLiteral("Закрити"));

  auto * basedOnBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                      QStringLiteral("Створити на підставі…"));
  auto * basedOnMenu = new QMenu(basedOnBtn);
  auto * actVko = basedOnMenu->addAction(QStringLiteral("Видатковий касовий ордер (ВКО)"));
  auto * actPko = basedOnMenu->addAction(QStringLiteral("Прибутковий касовий ордер (ПКО)"));
  auto * actBank = basedOnMenu->addAction(QStringLiteral("Банківський платіж"));
  basedOnBtn->setMenu(basedOnMenu);

  connect(actVko, &QAction::triggered, this, [this]() {
    DocumentDialog::createBasedOn(session_, document_, DocumentType::CashOut, this);
  });
  connect(actPko, &QAction::triggered, this, [this]() {
    DocumentDialog::createBasedOn(session_, document_, DocumentType::CashIn, this);
  });
  connect(actBank, &QAction::triggered, this, [this]() {
    DocumentDialog::createBasedOn(session_, document_, DocumentType::BankPayment, this);
  });

  connect(saveBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = save();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Збережено"));
    }
  });
  connect(postBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = saveAndPost();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Проведено"));
    }
  });
  connect(unpostBtn_, &QPushButton::clicked, this, &ExpenseReportDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &ExpenseReportDialog::printForm);
  connect(helpBtn, &QPushButton::clicked, this, &ExpenseReportDialog::openHelp);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

  new QShortcut(QKeySequence::HelpContents, this, SLOT(openHelp()));

  auto * tableLayout = new QVBoxLayout;
  tableLayout->addWidget(table_);
  auto * midBar = new QHBoxLayout;
  midBar->addWidget(addRowBtn);
  midBar->addStretch();
  midBar->addWidget(total_);
  tableLayout->addLayout(midBar);

  auto * btnBar = new QHBoxLayout;
  btnBar->addWidget(saveBtn_);
  btnBar->addWidget(postBtn_);
  btnBar->addWidget(unpostBtn_);
  btnBar->addWidget(printBtn_);
  btnBar->addWidget(basedOnBtn);
  btnBar->addStretch();
  btnBar->addWidget(helpBtn);
  btnBar->addWidget(closeBtn);

  auto * mainLayout = new QVBoxLayout(this);
  mainLayout->addLayout(headerForm);
  mainLayout->addLayout(tableLayout);
  mainLayout->addWidget(new QLabel(QStringLiteral("Коментар / призначення авансу:")));
  mainLayout->addWidget(comment_);
  mainLayout->addWidget(reason_);
  mainLayout->addLayout(btnBar);

  updateTotal();
  updatePostedUi();
  refreshGate();

  connect(number_, &QLineEdit::textChanged, this, &ExpenseReportDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &ExpenseReportDialog::refreshGate);
  connect(employee_, &QComboBox::currentIndexChanged, this, &ExpenseReportDialog::refreshGate);
}

void ExpenseReportDialog::addRow(const ExpenseLine & row)
{
  const int index = table_->rowCount();
  table_->insertRow(index);

  auto * docRef = new QLineEdit(row.documentRef);
  docRef->setPlaceholderText(QStringLiteral("Чек №…"));

  auto * accCombo = new QComboBox;
  ui::fillAccountCombo(accCombo, expenseAccounts_, row.expenseAccountId);

  auto * nomCombo = new QComboBox;
  ui::fillNomenclatureCombo(nomCombo, session_, row.nomenclatureId);

  auto * qty = new QLineEdit(row.quantity.isZero() ? QString() : row.quantity.toDisplay());
  qty->setPlaceholderText(QStringLiteral("1.000"));
  auto * price = new QLineEdit(row.unitPrice.isZero() ? QString() : row.unitPrice.toDisplay());
  price->setPlaceholderText(QStringLiteral("0.00"));
  auto * amount = new QLineEdit(row.amount.isZero() ? QString() : row.amount.toDisplay());
  amount->setPlaceholderText(QStringLiteral("0.00"));

  auto * vatCombo = new QComboBox;
  vatCombo->addItem(QStringLiteral("Без ПДВ"), 0);
  vatCombo->addItem(QStringLiteral("20%"), 2000);
  vatCombo->addItem(QStringLiteral("7%"), 700);
  vatCombo->addItem(QStringLiteral("14%"), 1400);
  vatCombo->addItem(QStringLiteral("0%"), 0);
  for (int i = 0; i < vatCombo->count(); ++i) {
    if (vatCombo->itemData(i).toInt() == row.vatRateBp &&
        (row.vatRateBp > 0 || row.vatCents == 0)) {
      vatCombo->setCurrentIndex(i);
      break;
    }
  }

  auto * vatAmount =
      new QLineEdit(row.vatCents > 0 ? Money::fromCents(row.vatCents).toDisplay() : QString());
  vatAmount->setPlaceholderText(QStringLiteral("0.00"));

  auto * totalCell = new QLineEdit;
  totalCell->setReadOnly(true);

  auto * description = new QLineEdit(row.description);
  description->setPlaceholderText(QStringLiteral("Канцтовари, паливо, добові…"));

  auto * delBtn = new QPushButton(QStringLiteral("✕"));
  delBtn->setMaximumWidth(30);

  const auto recalcVatAndTotal = [amount, vatCombo, vatAmount, totalCell, this]() {
    const Result<Money> base = Money::fromDisplayString(amount->text());
    Money net = base ? base.value() : Money::zero();
    const int rateBp = vatCombo->currentData().toInt();
    if (rateBp > 0 && net.isPositive()) {
      const Result<Money> v = vatOnNet(net, rateBp);
      if (v) {
        vatAmount->setText(v.value().toDisplay());
      }
    }
    const Result<Money> vAmt = Money::fromDisplayString(vatAmount->text());
    Money total = net;
    if (vAmt) {
      if (const auto next = total.plus(vAmt.value())) {
        total = next.value();
      }
    }
    totalCell->setText(total.toDisplay());
    updateTotal();
  };

  bindQtyPrice(qty, price, amount);
  connect(amount, &QLineEdit::textChanged, this, recalcVatAndTotal);
  connect(vatCombo, &QComboBox::currentIndexChanged, this, recalcVatAndTotal);
  connect(vatAmount, &QLineEdit::textChanged, this, recalcVatAndTotal);

  const auto updateStockState = [accCombo, nomCombo, this]() {
    const AccountId id = ui::selectedAccount(accCombo);
    bool isStock = false;
    for (const Account & a : accounts_) {
      if (a.id == id) {
        isStock = a.code.startsWith(QLatin1String("20")) ||
                  a.code.startsWith(QLatin1String("28")) || a.code.startsWith(QLatin1String("22"));
        break;
      }
    }
    nomCombo->setEnabled(isStock);
    if (!isStock) {
      nomCombo->setCurrentIndex(0);
    }
  };
  connect(accCombo, &QComboBox::currentIndexChanged, this, updateStockState);
  updateStockState();
  recalcVatAndTotal();

  table_->setCellWidget(index, 0, docRef);
  table_->setCellWidget(index, 1, accCombo);
  table_->setCellWidget(index, 2, nomCombo);
  table_->setCellWidget(index, 3, qty);
  table_->setCellWidget(index, 4, price);
  table_->setCellWidget(index, 5, amount);
  table_->setCellWidget(index, 6, vatCombo);
  table_->setCellWidget(index, 7, vatAmount);
  table_->setCellWidget(index, 8, totalCell);
  table_->setCellWidget(index, 9, description);
  table_->setCellWidget(index, 10, delBtn);

  connect(delBtn, &QPushButton::clicked, this, [this, delBtn]() {
    for (int r = 0; r < table_->rowCount(); ++r) {
      if (table_->cellWidget(r, 10) == delBtn) {
        table_->removeRow(r);
        break;
      }
    }
    updateTotal();
    refreshGate();
  });

  connect(docRef, &QLineEdit::textChanged, this, &ExpenseReportDialog::refreshGate);
  connect(accCombo, &QComboBox::currentIndexChanged, this, &ExpenseReportDialog::refreshGate);
  connect(nomCombo, &QComboBox::currentIndexChanged, this, &ExpenseReportDialog::refreshGate);
  connect(qty, &QLineEdit::textChanged, this, &ExpenseReportDialog::refreshGate);
  connect(price, &QLineEdit::textChanged, this, &ExpenseReportDialog::refreshGate);
  connect(amount, &QLineEdit::textChanged, this, &ExpenseReportDialog::refreshGate);
  connect(vatAmount, &QLineEdit::textChanged, this, &ExpenseReportDialog::refreshGate);
  connect(description, &QLineEdit::textChanged, this, &ExpenseReportDialog::refreshGate);
}

void ExpenseReportDialog::updateTotal()
{
  Money totalNet = Money::zero();
  Money totalVat = Money::zero();
  Money grandTotal = Money::zero();
  for (int r = 0; r < table_->rowCount(); ++r) {
    const Result<ExpenseLine> line = expenseFromRow(table_, r);
    if (!line)
      continue;
    if (const auto n = totalNet.plus(line.value().amount))
      totalNet = n.value();
    if (line.value().vatCents > 0) {
      if (const auto v = totalVat.plus(Money::fromCents(line.value().vatCents)))
        totalVat = v.value();
    }
  }
  if (const auto g = totalNet.plus(totalVat))
    grandTotal = g.value();
  total_->setText(QStringLiteral("Разом витрат: %1 грн (без ПДВ: %2 грн, ПДВ: %3 грн)")
                      .arg(grandTotal.toDisplay(), totalNet.toDisplay(), totalVat.toDisplay()));
  refreshGate();
}

Status ExpenseReportDialog::collect(Document & out) const
{
  out = document_;
  out.number = number_->text().trimmed();
  out.date = date_->date();
  out.employeeId = ui::selectedEmployee(employee_);
  out.comment = comment_->toPlainText().trimmed();
  std::vector<ExpenseLine> rows;
  for (int r = 0; r < table_->rowCount(); ++r) {
    const Result<ExpenseLine> line = expenseFromRow(table_, r);
    if (!line)
      return line.asStatus();
    rows.push_back(line.value());
  }
  const Result<std::vector<DocumentLine>> composed = composeExpenseReport(accounts_, rows);
  if (!composed) {
    return composed.asStatus();
  }
  out.lines = composed.value();
  return Status::ok();
}

Status ExpenseReportDialog::save()
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

Status ExpenseReportDialog::saveAndPost()
{
  Status status = save();
  if (!status) {
    return status;
  }
  status = session_.posting().post(document_.id);
  if (!status) {
    return status;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  } else {
    document_.posted = true;
  }
  persisted_ = true;
  updatePostedUi();
  return Status::ok();
}

void ExpenseReportDialog::unpost()
{
  if (!document_.id) {
    return;
  }
  const Status status = session_.posting().unpost(document_.id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  } else {
    document_.posted = false;
  }
  persisted_ = true;
  updatePostedUi();
  refreshGate();
}

void ExpenseReportDialog::printForm()
{
  Status status = Status::ok();
  if (!document_.id || !document_.posted) {
    status = save();
    if (!status) {
      ui::showError(this, status);
      return;
    }
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (!loaded) {
    ui::showError(this, loaded.error());
    return;
  }
  const Result<QString> html = PrintForms(session_.handle()).documentHtml(loaded.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  QMenu menu(this);
  QAction * preview = menu.addAction(QStringLiteral("Друк…"));
  QAction * pdf = menu.addAction(QStringLiteral("Зберегти PDF…"));
  QAction * chosen = menu.exec(QCursor::pos());
  if (chosen == preview) {
    ui::printHtml(this, QStringLiteral("Авансовий звіт %1").arg(loaded.value().number),
                  html.value());
  } else if (chosen == pdf) {
    ui::savePdf(this, QStringLiteral("avansovyi-zvit-%1.pdf").arg(loaded.value().number),
                html.value());
  }
}

void ExpenseReportDialog::updatePostedUi()
{
  const bool posted = document_.posted;
  number_->setEnabled(!posted);
  date_->setEnabled(!posted);
  employee_->setEnabled(!posted);
  table_->setEnabled(!posted);
  comment_->setEnabled(!posted);
}

ui::FormButtons ExpenseReportDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

void ExpenseReportDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, document_.date,
                             [this](Document & out) { return collect(out); });
}

void ExpenseReportDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("expense-report.html"));
}

} // namespace fin
