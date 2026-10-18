#include "ui/MsDialogs.h"

#include "ui/HelpBrowser.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Vat.h"
#include "fin/print/PrintForms.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {

MsDocumentDialog::MsDocumentDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(document_.type));
  resize(920, 520);
  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
  }
  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  counterparty_ = new QComboBox;
  ui::fillCounterpartyCombo(counterparty_, session_, document_.counterpartyId, !isReceipt());
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Номер"), number_);
  form->addRow(QStringLiteral("Дата"), date_);
  if (isReceipt()) {
    form->addRow(QStringLiteral("Постачальник"), counterparty_);
  } else {
    counterparty_->hide();
  }

  auto * addLine = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                   QStringLiteral("Додати рядок"));
  auto * removeLine = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/delete.svg")),
                                      QStringLiteral("Видалити рядок"));
  auto * lineBar = new QHBoxLayout;
  lineBar->addWidget(addLine);
  lineBar->addWidget(removeLine);
  lineBar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(isReceipt() ? 4 : 2);
  if (isReceipt()) {
    table_->setHorizontalHeaderLabels({QStringLiteral("МШП"), QStringLiteral("К-сть"),
                                       QStringLiteral("Ціна"), QStringLiteral("Сума")});
  } else {
    table_->setHorizontalHeaderLabels({QStringLiteral("МШП"), QStringLiteral("К-сть")});
  }
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);

  comment_ = new QTextEdit(document_.comment);
  comment_->setAcceptRichText(false);
  comment_->setMaximumHeight(70);
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
  auto * help = new QPushButton(QStringLiteral("Довідка"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  buttons->addWidget(saveBtn_);
  buttons->addWidget(postBtn_);
  buttons->addWidget(unpostBtn_);
  buttons->addWidget(printBtn_);
  buttons->addWidget(help);
  buttons->addStretch();
  buttons->addWidget(close);

  auto * hint = new QLabel(
      isReceipt() ? QStringLiteral("Надходження на 22 від постачальника (631). Картку створюють у "
                                   "довіднику МШП. Залишок і сума змінюються лише проведенням.")
                  : QStringLiteral("Передача в експлуатацію: Дт 92 Кт 22 за середньою з картки. "
                                   "100% на витрати в місяці передачі."));
  hint->setWordWrap(true);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addLayout(lineBar);
  layout->addWidget(table_, 1);
  layout->addWidget(new QLabel(QStringLiteral("Коментар")));
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(buttons);

  if (document_.lines.empty()) {
    addRow({});
  } else {
    for (const DocumentLine & line : document_.lines) {
      MsLine row;
      row.itemId = line.lowValueId;
      row.quantity = line.quantity;
      row.unitPrice = line.unitPrice;
      row.amount = line.amount;
      row.description = line.description;
      addRow(row);
    }
  }

  connect(addLine, &QPushButton::clicked, this, [this]() {
    addRow({});
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
  connect(unpostBtn_, &QPushButton::clicked, this, &MsDocumentDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &MsDocumentDialog::printForm);
  connect(help, &QPushButton::clicked, this, &MsDocumentDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &MsDocumentDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &MsDocumentDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &MsDocumentDialog::refreshGate);
  connect(counterparty_, &QComboBox::currentIndexChanged, this, &MsDocumentDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

bool MsDocumentDialog::isReceipt() const
{
  return usesMsReceiptForm(document_.type);
}

void MsDocumentDialog::addRow(const MsLine & row)
{
  const int index = table_->rowCount();
  table_->insertRow(index);
  auto * combo = new QComboBox;
  ui::fillLowValueCombo(combo, session_, row.itemId);
  table_->setCellWidget(index, 0, combo);
  auto * qty = new QLineEdit(row.quantity.isZero() ? QString() : row.quantity.toDisplay());
  qty->setAlignment(Qt::AlignRight);
  table_->setCellWidget(index, 1, qty);
  connect(combo, &QComboBox::currentIndexChanged, this, &MsDocumentDialog::refreshGate);
  connect(qty, &QLineEdit::textChanged, this, &MsDocumentDialog::refreshGate);
  if (!isReceipt()) {
    return;
  }
  auto * price = new QLineEdit(row.unitPrice.isZero() ? QString() : row.unitPrice.toDisplay());
  auto * amount = new QLineEdit(row.amount.isZero() ? QString() : row.amount.toDisplay());
  price->setAlignment(Qt::AlignRight);
  amount->setAlignment(Qt::AlignRight);
  table_->setCellWidget(index, 2, price);
  table_->setCellWidget(index, 3, amount);
  connect(price, &QLineEdit::textChanged, this, [this, qty, price, amount]() {
    if (qty->text().trimmed().isEmpty() || price->text().trimmed().isEmpty()) {
      refreshGate();
      return;
    }
    const Result<Quantity> quantity = Quantity::fromDisplayString(qty->text());
    const Result<Money> unit = Money::fromDisplayString(price->text());
    if (quantity && unit) {
      const Result<Money> sum = quantity.value().times(unit.value());
      if (sum) {
        amount->setText(sum.value().toDisplay());
      }
    }
    refreshGate();
  });
  connect(qty, &QLineEdit::textChanged, this, [this, qty, price, amount]() {
    if (!qty->text().trimmed().isEmpty() && !price->text().trimmed().isEmpty()) {
      const Result<Quantity> quantity = Quantity::fromDisplayString(qty->text());
      const Result<Money> unit = Money::fromDisplayString(price->text());
      if (quantity && unit) {
        const Result<Money> sum = quantity.value().times(unit.value());
        if (sum) {
          amount->setText(sum.value().toDisplay());
        }
      }
    }
    refreshGate();
  });
  connect(amount, &QLineEdit::textChanged, this, &MsDocumentDialog::refreshGate);
}

ui::FormButtons MsDocumentDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

Status MsDocumentDialog::collect(Document & out) const
{
  out = document_;
  out.number = number_->text();
  out.date = date_->date();
  out.counterpartyId = isReceipt() ? ui::selectedCounterparty(counterparty_) : CounterpartyId{};
  out.comment = comment_->toPlainText();
  std::vector<MsLine> rows;
  for (int row = 0; row < table_->rowCount(); ++row) {
    auto * combo = qobject_cast<QComboBox *>(table_->cellWidget(row, 0));
    auto * qtyEdit = qobject_cast<QLineEdit *>(table_->cellWidget(row, 1));
    if (combo == nullptr || qtyEdit == nullptr) {
      continue;
    }
    MsLine line;
    line.itemId = ui::selectedLowValue(combo);
    if (!line.itemId && qtyEdit->text().trimmed().isEmpty()) {
      continue;
    }
    const Result<Quantity> qty = Quantity::fromDisplayString(qtyEdit->text());
    if (!qty) {
      return qty.asStatus();
    }
    line.quantity = qty.value();
    if (isReceipt()) {
      auto * priceEdit = qobject_cast<QLineEdit *>(table_->cellWidget(row, 2));
      auto * amountEdit = qobject_cast<QLineEdit *>(table_->cellWidget(row, 3));
      if (priceEdit == nullptr || amountEdit == nullptr) {
        continue;
      }
      const Result<Money> price = Money::fromDisplayString(priceEdit->text());
      const Result<Money> amount = Money::fromDisplayString(amountEdit->text());
      if (!price) {
        return price.asStatus();
      }
      if (!amount) {
        return amount.asStatus();
      }
      line.unitPrice = price.value();
      line.amount = amount.value();
    }
    rows.push_back(line);
  }
  if (isReceipt()) {
    const Result<std::vector<DocumentLine>> lines = composeMsReceipt(accounts_, rows);
    if (!lines) {
      return lines.asStatus();
    }
    out.lines = lines.value();
    const Result<CompanyProfile> company = session_.company().load();
    if (company && company.value().vatPayer) {
      return stampSupplierVat(out.lines, accounts_, company.value().vatRateBp);
    }
    return Status::ok();
  }
  const Result<std::vector<LowValueItem>> items = session_.lowValue().listAll();
  if (!items) {
    return items.asStatus();
  }
  const Result<std::vector<DocumentLine>> lines = composeMsIssue(accounts_, items.value(), rows);
  if (!lines) {
    return lines.asStatus();
  }
  out.lines = lines.value();
  return Status::ok();
}

Status MsDocumentDialog::save()
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

Status MsDocumentDialog::saveAndPost()
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

void MsDocumentDialog::unpost()
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
  }
  document_.posted = false;
  persisted_ = true;
  updatePostedUi();
  refreshGate();
}

void MsDocumentDialog::printForm()
{
  Status status = document_.id ? Status::ok() : save();
  if (!status) {
    ui::showError(this, status);
    return;
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
  ui::exportReport(this, documentTypeLabelUk(loaded.value().type),
                   QStringLiteral("ms-%1.pdf").arg(loaded.value().number), html.value(), QString(),
                   QString());
}

void MsDocumentDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, date_->date(),
                             [this](Document & out) { return collect(out); });
}

void MsDocumentDialog::updatePostedUi()
{
  const bool locked = document_.posted;
  number_->setReadOnly(locked);
  date_->setReadOnly(locked);
  counterparty_->setEnabled(!locked);
  comment_->setReadOnly(locked);
  table_->setEnabled(!locked);
}

void MsDocumentDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("low-value.html"));
}

} // namespace fin
