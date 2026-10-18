#include "ui/TaxInvoiceDialog.h"

#include "ui/HelpBrowser.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/db/CounterpartyRepository.h"
#include "fin/domain/Vat.h"
#include "fin/edo/TaxInvoiceXml.h"
#include "fin/print/PrintForms.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIODevice>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {

TaxInvoiceDialog::TaxInvoiceDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(DocumentType::TaxInvoice));
  resize(720, 480);
  setWhatsThis(QStringLiteral("Податкова накладна проводить 643 або 644. Дата реєстрації в ЄРПН "
                              "переносить суму на 641. XML — для Кабінету / M.E.doc, без підпису в "
                              "Фініку."));

  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
  }

  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  party_ = new QLineEdit;
  party_->setReadOnly(true);
  if (document_.counterpartyId) {
    const Result<Counterparty> party =
        CounterpartyRepository(session_.handle()).findById(document_.counterpartyId);
    if (party) {
      party_->setText(party.value().name);
    }
  }
  source_ = new QLineEdit;
  source_->setReadOnly(true);
  if (document_.basedOnId) {
    const Result<Document> source = session_.documents().findById(document_.basedOnId);
    if (source) {
      source_->setText(QStringLiteral("%1 %2").arg(documentTypeLabelUk(source.value().type),
                                                   source.value().number));
    }
  }
  registered_ = new QCheckBox(QStringLiteral("Зареєстровано в ЄРПН"));
  registrationDate_ =
      new QDateEdit(document_.vatRegistrationDate.isValid() ? document_.vatRegistrationDate
                                                            : QDate::currentDate());
  registrationDate_->setCalendarPopup(true);
  registrationDate_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  registered_->setChecked(document_.vatRegistrationDate.isValid());
  registrationDate_->setEnabled(registered_->isChecked() && !document_.posted);

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Номер"), number_);
  form->addRow(QStringLiteral("Дата ПН"), date_);
  form->addRow(QStringLiteral("Контрагент"), party_);
  contract_ = new QComboBox;
  contract_->setObjectName(QStringLiteral("contractCombo"));
  ui::fillContractCombo(contract_, session_, document_.counterpartyId, document_.contractId);
  form->addRow(QStringLiteral("Договір"), contract_);
  form->addRow(QStringLiteral("На підставі"), source_);
  form->addRow(registered_);
  form->addRow(QStringLiteral("Дата реєстрації"), registrationDate_);

  table_ = new QTableWidget;
  table_->setColumnCount(3);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Дебет"), QStringLiteral("Кредит"), QStringLiteral("ПДВ")});
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  fillLines();

  comment_ = new QTextEdit(document_.comment);
  comment_->setAcceptRichText(false);
  comment_->setMaximumHeight(70);
  reason_ = new QLabel;
  reason_->setWordWrap(true);
  auto * hint = new QLabel(
      QStringLiteral("Проведення без дати реєстрації — лише 643/644. Після проведення можна "
                     "вказати дату ЄРПН (навіть у наступному місяці) без зняття ПН, якщо "
                     "дата реєстрації ще не в закритому періоді."));
  hint->setWordWrap(true);

  saveBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                             QStringLiteral("Зберегти"));
  postBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                             QStringLiteral("Провести"));
  unpostBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")),
                               QStringLiteral("Зняти проведення"));
  registerBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                                 QStringLiteral("Зареєструвати"));
  unregisterBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")),
                                   QStringLiteral("Скасувати реєстрацію"));
  printBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                              QStringLiteral("Друк / PDF"));
  exportBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                               QStringLiteral("Експорт XML"));
  auto * help = new QPushButton(QStringLiteral("Довідка"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  buttons->addWidget(saveBtn_);
  buttons->addWidget(postBtn_);
  buttons->addWidget(unpostBtn_);
  buttons->addWidget(registerBtn_);
  buttons->addWidget(unregisterBtn_);
  buttons->addWidget(printBtn_);
  buttons->addWidget(exportBtn_);
  buttons->addWidget(help);
  buttons->addStretch();
  buttons->addWidget(close);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addWidget(table_, 1);
  layout->addWidget(new QLabel(QStringLiteral("Коментар")));
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(buttons);

  connect(registered_, &QCheckBox::toggled, this, [this](bool on) {
    registrationDate_->setEnabled(on && !document_.posted);
    refreshGate();
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
  connect(unpostBtn_, &QPushButton::clicked, this, &TaxInvoiceDialog::unpost);
  connect(registerBtn_, &QPushButton::clicked, this, &TaxInvoiceDialog::registerNow);
  connect(unregisterBtn_, &QPushButton::clicked, this, &TaxInvoiceDialog::unregisterNow);
  connect(printBtn_, &QPushButton::clicked, this, &TaxInvoiceDialog::printForm);
  connect(exportBtn_, &QPushButton::clicked, this, &TaxInvoiceDialog::exportXml);
  connect(help, &QPushButton::clicked, this, &TaxInvoiceDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &TaxInvoiceDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &TaxInvoiceDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &TaxInvoiceDialog::refreshGate);
  connect(contract_, &QComboBox::currentIndexChanged, this, &TaxInvoiceDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

void TaxInvoiceDialog::fillLines()
{
  table_->setRowCount(0);
  for (const DocumentLine & line : document_.lines) {
    if (isVatRegistrationLine(accounts_, line)) {
      continue;
    }
    const int row = table_->rowCount();
    table_->insertRow(row);
    QString debit;
    QString credit;
    for (const Account & account : accounts_) {
      if (account.id == line.debitAccountId) {
        debit = account.code;
      }
      if (account.id == line.creditAccountId) {
        credit = account.code;
      }
    }
    table_->setItem(row, 0, new QTableWidgetItem(debit));
    table_->setItem(row, 1, new QTableWidgetItem(credit));
    auto * amount = new QTableWidgetItem(line.amount.toDisplay());
    amount->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 2, amount);
  }
}

Status TaxInvoiceDialog::collect(Document & out) const
{
  out = document_;
  out.number = number_->text().trimmed();
  out.date = date_->date();
  out.contractId = ui::selectedContract(contract_);
  out.comment = comment_->toPlainText().trimmed();
  if (registered_->isChecked()) {
    out.vatRegistrationDate = registrationDate_->date();
  } else {
    out.vatRegistrationDate = QDate();
  }
  return Status::ok();
}

Status TaxInvoiceDialog::save()
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

Status TaxInvoiceDialog::saveAndPost()
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
  fillLines();
  updatePostedUi();
  return Status::ok();
}

void TaxInvoiceDialog::unpost()
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
  fillLines();
  updatePostedUi();
  refreshGate();
}

void TaxInvoiceDialog::registerNow()
{
  if (!document_.id || !document_.posted) {
    return;
  }
  const Status status =
      session_.posting().registerTaxInvoice(document_.id, registrationDate_->date());
  if (!status) {
    ui::showError(this, status);
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  }
  persisted_ = true;
  registered_->setChecked(true);
  fillLines();
  updatePostedUi();
  refreshGate();
}

void TaxInvoiceDialog::unregisterNow()
{
  if (!document_.id) {
    return;
  }
  const Status status = session_.posting().unregisterTaxInvoice(document_.id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  }
  persisted_ = true;
  registered_->setChecked(false);
  fillLines();
  updatePostedUi();
  refreshGate();
}

void TaxInvoiceDialog::printForm()
{
  Status status = Status::ok();
  if (!document_.id) {
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
  ui::printHtml(this, QStringLiteral("Податкова накладна %1").arg(loaded.value().number),
                html.value());
}

void TaxInvoiceDialog::exportXml()
{
  Status status = Status::ok();
  if (!document_.id) {
    status = save();
    if (!status) {
      ui::showError(this, status);
      return;
    }
  }
  const Result<QString> xml = exportTaxInvoiceXml(session_.handle(), document_.id);
  if (!xml) {
    ui::showError(this, xml.error());
    return;
  }
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("Експорт податкової накладної"),
      QStringLiteral("%1.xml").arg(document_.number), QStringLiteral("XML (*.xml)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    ui::showError(this, Error::database(QStringLiteral("не вдалося записати файл XML")));
    return;
  }
  if (file.write(xml.value().toUtf8()) < 0) {
    ui::showError(this, Error::database(QStringLiteral("не вдалося записати файл XML")));
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  }
  persisted_ = true;
}

void TaxInvoiceDialog::updatePostedUi()
{
  const bool posted = document_.posted;
  number_->setEnabled(!posted);
  date_->setEnabled(!posted);
  contract_->setEnabled(!posted);
  comment_->setEnabled(!posted);
  registered_->setEnabled(!posted || !document_.vatRegistrationDate.isValid());
  registrationDate_->setEnabled((!posted && registered_->isChecked()) ||
                                (posted && !document_.vatRegistrationDate.isValid()));
}

ui::FormButtons TaxInvoiceDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

void TaxInvoiceDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, document_.date,
                             [this](Document & out) { return collect(out); });
  registerBtn_->setEnabled(document_.posted && !document_.vatRegistrationDate.isValid());
  unregisterBtn_->setEnabled(document_.posted && document_.vatRegistrationDate.isValid());
  exportBtn_->setEnabled(document_.id.value > 0 || !number_->text().trimmed().isEmpty());
}

void TaxInvoiceDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("tax.html"));
}

} // namespace fin
