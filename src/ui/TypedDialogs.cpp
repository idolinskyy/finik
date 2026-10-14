#include "ui/TypedDialogs.h"

#include "ui/HelpBrowser.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/print/PrintForms.h"

#include <QVariant>

#include <QAction>
#include <QComboBox>
#include <QCursor>
#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLatin1String>
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
  QObject::connect(qty, &QLineEdit::textChanged, qty, recalc);
  QObject::connect(price, &QLineEdit::textChanged, price, recalc);
}

} // namespace

BankDocumentDialog::BankDocumentDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(document_.type));
  resize(820, 480);

  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
  }

  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  direction_ = new QComboBox;
  direction_->addItem(QStringLiteral("Надходження на 311"), true);
  direction_->addItem(QStringLiteral("Списання з 311"), false);
  const bool incoming = bankDocumentIncoming(document_.lines, accounts_);
  direction_->setCurrentIndex(incoming ? 0 : 1);
  counterparty_ = new QComboBox;
  ui::fillCounterpartyCombo(counterparty_, session_, document_.counterpartyId, true);
  contract_ = new QComboBox;
  contract_->setObjectName(QStringLiteral("contractCombo"));
  ui::fillContractCombo(contract_, session_, document_.counterpartyId, document_.contractId);
  currency_ = new QComboBox;
  ui::fillCurrencyCombo(currency_, session_, document_.currencyCode);
  currencyRate_ = new QLineEdit;
  currencyRate_->setPlaceholderText(QStringLiteral("Курс грн / 1 од."));
  currencyRate_->setAlignment(Qt::AlignRight);
  if (!document_.currencyCode.isEmpty() &&
      document_.currencyCode.compare(QStringLiteral("UAH"), Qt::CaseInsensitive) != 0 &&
      document_.currencyRateMicros > 0) {
    currencyRate_->setText(
        ui::currencyRateToDisplay(document_.currencyRateMicros, document_.currencyNominal));
    currencyRate_->setProperty("currencyNominal", document_.currencyNominal);
    currencyRate_->setEnabled(true);
  } else {
    ui::loadCurrencyRateForDate(currencyRate_, session_, ui::selectedCurrencyCode(currency_),
                                date_->date());
  }

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Номер"), number_);
  form->addRow(QStringLiteral("Дата"), date_);
  form->addRow(QStringLiteral("Рух"), direction_);
  form->addRow(QStringLiteral("Контрагент"), counterparty_);
  form->addRow(QStringLiteral("Договір"), contract_);
  form->addRow(QStringLiteral("Валюта"), currency_);
  form->addRow(QStringLiteral("Курс"), currencyRate_);

  const auto reloadRate = [this]() {
    ui::loadCurrencyRateForDate(currencyRate_, session_, ui::selectedCurrencyCode(currency_),
                                date_->date());
    refreshGate();
  };
  connect(currency_, &QComboBox::currentIndexChanged, this, reloadRate);
  connect(date_, &QDateEdit::dateChanged, this, reloadRate);
  connect(currencyRate_, &QLineEdit::textChanged, this, &BankDocumentDialog::refreshGate);

  table_ = new QTableWidget;
  table_->setColumnCount(3);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Кореспонденція"), QStringLiteral("Сума"), QStringLiteral("Призначення")});
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
  auto * bar = new QHBoxLayout;
  bar->addWidget(saveBtn_);
  bar->addWidget(postBtn_);
  bar->addWidget(unpostBtn_);
  bar->addWidget(printBtn_);
  bar->addWidget(help);
  bar->addStretch();
  bar->addWidget(close);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(
      new QLabel(QStringLiteral("Кореспонденція в рядку: покупець 361, аванс від покупця 681, "
                                "постачальник 631, аванс постачальнику 371, каса 301, комісія 92 "
                                "чи інший рахунок. Контрагент потрібен для 361, 631, 371 і 681. "
                                "Договір — якщо рахунок вимагає субконто договору.")));
  layout->addLayout(lineBar);
  layout->addWidget(table_, 1);
  layout->addWidget(new QLabel(QStringLiteral("Призначення платежу")));
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(bar);

  const std::vector<BankLine> existing = extractBankLines(document_.lines, accounts_);
  if (existing.empty()) {
    addRow(BankLine{});
  } else {
    for (const BankLine & row : existing) {
      addRow(row);
    }
  }

  connect(addLine, &QPushButton::clicked, this, [this]() {
    addRow(BankLine{});
    refreshGate();
  });
  connect(removeLine, &QPushButton::clicked, this, [this]() {
    if (table_->currentRow() >= 0) {
      table_->removeRow(table_->currentRow());
      refreshGate();
    }
  });
  connect(saveBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = save();
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
  connect(unpostBtn_, &QPushButton::clicked, this, &BankDocumentDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &BankDocumentDialog::printForm);
  connect(help, &QPushButton::clicked, this, &BankDocumentDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &BankDocumentDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &BankDocumentDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &BankDocumentDialog::refreshGate);
  connect(direction_, &QComboBox::currentIndexChanged, this, &BankDocumentDialog::refreshGate);
  connect(counterparty_, &QComboBox::currentIndexChanged, this, [this]() {
    ui::fillContractCombo(contract_, session_, ui::selectedCounterparty(counterparty_), {});
    refreshGate();
  });
  connect(contract_, &QComboBox::currentIndexChanged, this, &BankDocumentDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

void BankDocumentDialog::addRow(const BankLine & row)
{
  const int index = table_->rowCount();
  table_->insertRow(index);
  auto * account = new QComboBox;
  std::vector<Account> correspondence;
  correspondence.reserve(accounts_.size());
  for (const Account & item : accounts_) {
    if (item.code.compare(QLatin1String(chart::kBank), Qt::CaseInsensitive) == 0 ||
        item.code.compare(QLatin1String(chart::kAuxiliary), Qt::CaseInsensitive) == 0) {
      continue;
    }
    correspondence.push_back(item);
  }
  ui::fillAccountCombo(account, correspondence, row.correspondenceId);
  auto * amount = new QLineEdit(row.amount.isZero() ? QString() : row.amount.toDisplay());
  amount->setAlignment(Qt::AlignRight);
  auto * description = new QLineEdit(row.description);
  table_->setCellWidget(index, 0, account);
  table_->setCellWidget(index, 1, amount);
  table_->setCellWidget(index, 2, description);
  connect(account, &QComboBox::currentIndexChanged, this, &BankDocumentDialog::refreshGate);
  connect(amount, &QLineEdit::textChanged, this, &BankDocumentDialog::refreshGate);
  connect(description, &QLineEdit::textChanged, this, &BankDocumentDialog::refreshGate);
}

Status BankDocumentDialog::collect(Document & out) const
{
  out = document_;
  out.number = number_->text().trimmed();
  out.date = date_->date();
  out.counterpartyId = ui::selectedCounterparty(counterparty_);
  out.contractId = ui::selectedContract(contract_);
  out.comment = comment_->toPlainText().trimmed();
  if (currency_ != nullptr) {
    const QString code = ui::selectedCurrencyCode(currency_);
    if (code == QLatin1String("UAH")) {
      out.currencyCode.clear();
      out.currencyRateMicros = 0;
      out.currencyNominal = 1;
    } else {
      out.currencyCode = code;
      int nominal = currencyRate_->property("currencyNominal").toInt();
      if (nominal <= 0) {
        nominal = 1;
      }
      out.currencyNominal = nominal;
      Status rateStatus =
          ui::currencyRateFromDisplay(currencyRate_->text(), nominal, out.currencyRateMicros);
      if (!rateStatus) {
        return rateStatus;
      }
    }
  }
  std::vector<BankLine> rows;
  for (int row = 0; row < table_->rowCount(); ++row) {
    auto * account = qobject_cast<QComboBox *>(table_->cellWidget(row, 0));
    auto * amountEdit = qobject_cast<QLineEdit *>(table_->cellWidget(row, 1));
    auto * description = qobject_cast<QLineEdit *>(table_->cellWidget(row, 2));
    if (account == nullptr || amountEdit == nullptr) {
      return Status::fail(Error::validation(QStringLiteral("пошкоджений рядок")));
    }
    BankLine line;
    line.correspondenceId = ui::selectedAccount(account);
    line.description = description ? description->text().trimmed() : QString();
    if (!amountEdit->text().trimmed().isEmpty()) {
      const Result<Money> amount = Money::fromDisplayString(amountEdit->text());
      if (!amount) {
        return amount.asStatus();
      }
      line.amount = amount.value();
    }
    rows.push_back(line);
  }
  const Result<std::vector<DocumentLine>> composed =
      composeBank(accounts_, direction_->currentData().toBool(), rows);
  if (!composed) {
    return composed.asStatus();
  }
  out.lines = composed.value();
  return Status::ok();
}

Status BankDocumentDialog::save()
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

Status BankDocumentDialog::saveAndPost()
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

void BankDocumentDialog::unpost()
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

void BankDocumentDialog::printForm()
{
  if (!document_.posted) {
    const Status status = save();
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
  const Result<QString> html = PrintForms(session_.handle()).bankPaymentHtml(loaded.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  QMenu menu(this);
  QAction * preview = menu.addAction(QStringLiteral("Друк…"));
  QAction * pdf = menu.addAction(QStringLiteral("Зберегти PDF…"));
  QAction * chosen = menu.exec(QCursor::pos());
  if (chosen == preview) {
    ui::printHtml(this, QStringLiteral("Банк %1").arg(loaded.value().number), html.value());
  } else if (chosen == pdf) {
    ui::savePdf(this, QStringLiteral("bank-%1.pdf").arg(loaded.value().number), html.value());
  }
}

ui::FormButtons BankDocumentDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

void BankDocumentDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, document_.date,
                             [this](Document & out) { return collect(out); });
}

void BankDocumentDialog::updatePostedUi()
{
  const bool posted = document_.posted;
  number_->setEnabled(!posted);
  date_->setEnabled(!posted);
  direction_->setEnabled(!posted);
  counterparty_->setEnabled(!posted);
  contract_->setEnabled(!posted);
  if (currency_ != nullptr)
    currency_->setEnabled(!posted);
  if (currencyRate_ != nullptr)
    currencyRate_->setEnabled(!posted && currency_ != nullptr &&
                              ui::selectedCurrencyCode(currency_) != QLatin1String("UAH"));
  table_->setEnabled(!posted);
  comment_->setEnabled(!posted);
}

void BankDocumentDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("bank.html"));
}

OpeningDocumentDialog::OpeningDocumentDialog(LedgerSession & session, Document document,
                                             QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(document_.type));
  resize(980, 640);

  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
  }

  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Номер"), number_);
  form->addRow(QStringLiteral("Дата"), date_);

  money_ = new QTableWidget;
  money_->setColumnCount(3);
  money_->setHorizontalHeaderLabels(
      {QStringLiteral("Рахунок"), QStringLiteral("Дебет"), QStringLiteral("Кредит")});
  money_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  money_->verticalHeader()->setVisible(false);

  goods_ = new QTableWidget;
  goods_->setColumnCount(4);
  goods_->setHorizontalHeaderLabels({QStringLiteral("Номенклатура"), QStringLiteral("К-сть"),
                                     QStringLiteral("Ціна"), QStringLiteral("Сума")});
  goods_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  goods_->verticalHeader()->setVisible(false);

  auto * addMoney = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                    QStringLiteral("Рахунок"));
  auto * removeMoney = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/delete.svg")),
                                       QStringLiteral("Прибрати рахунок"));
  auto * addGoods = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                    QStringLiteral("Товар"));
  auto * removeGoods = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/delete.svg")),
                                       QStringLiteral("Прибрати товар"));
  auto * moneyBar = new QHBoxLayout;
  moneyBar->addWidget(new QLabel(QStringLiteral("Залишки рахунків (без 281 і 00)")));
  moneyBar->addStretch();
  moneyBar->addWidget(addMoney);
  moneyBar->addWidget(removeMoney);
  auto * goodsBar = new QHBoxLayout;
  goodsBar->addWidget(new QLabel(QStringLiteral("Залишки товарів (281)")));
  goodsBar->addStretch();
  goodsBar->addWidget(addGoods);
  goodsBar->addWidget(removeGoods);

  comment_ = new QTextEdit(document_.comment);
  comment_->setAcceptRichText(false);
  comment_->setMaximumHeight(60);
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
  auto * bar = new QHBoxLayout;
  bar->addWidget(saveBtn_);
  bar->addWidget(postBtn_);
  bar->addWidget(unpostBtn_);
  bar->addWidget(help);
  bar->addStretch();
  bar->addWidget(close);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(
      new QLabel(QStringLiteral("Рахунок 00 підставляється сам. Заповніть дебет або кредит, "
                                "не обидва.")));
  layout->addLayout(moneyBar);
  layout->addWidget(money_, 1);
  layout->addLayout(goodsBar);
  layout->addWidget(goods_, 1);
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(bar);

  const std::vector<OpeningMoneyLine> moneyRows =
      extractOpeningMoneyLines(document_.lines, accounts_);
  const std::vector<GoodsLine> goodsRows =
      extractGoodsLines(document_.type, document_.lines, accounts_);
  if (moneyRows.empty()) {
    addMoneyRow(OpeningMoneyLine{});
  } else {
    for (const OpeningMoneyLine & row : moneyRows) {
      addMoneyRow(row);
    }
  }
  if (goodsRows.empty()) {
    addGoodsRow(GoodsLine{});
  } else {
    for (const GoodsLine & row : goodsRows) {
      addGoodsRow(row);
    }
  }

  connect(addMoney, &QPushButton::clicked, this, [this]() {
    addMoneyRow(OpeningMoneyLine{});
    refreshGate();
  });
  connect(removeMoney, &QPushButton::clicked, this, [this]() {
    if (money_->currentRow() >= 0) {
      money_->removeRow(money_->currentRow());
      refreshGate();
    }
  });
  connect(addGoods, &QPushButton::clicked, this, [this]() {
    addGoodsRow(GoodsLine{});
    refreshGate();
  });
  connect(removeGoods, &QPushButton::clicked, this, [this]() {
    if (goods_->currentRow() >= 0) {
      goods_->removeRow(goods_->currentRow());
      refreshGate();
    }
  });
  connect(saveBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = save();
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
  connect(unpostBtn_, &QPushButton::clicked, this, &OpeningDocumentDialog::unpost);
  connect(help, &QPushButton::clicked, this, &OpeningDocumentDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &OpeningDocumentDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &OpeningDocumentDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &OpeningDocumentDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

void OpeningDocumentDialog::addMoneyRow(const OpeningMoneyLine & row)
{
  const int index = money_->rowCount();
  money_->insertRow(index);
  auto * account = new QComboBox;
  account->addItem(QStringLiteral("—"), QVariant::fromValue(qlonglong(0)));
  int selected = 0;
  for (const Account & acc : accounts_) {
    if (acc.code.compare(QLatin1String(chart::kAuxiliary), Qt::CaseInsensitive) == 0 ||
        acc.code.compare(QLatin1String(chart::kGoods), Qt::CaseInsensitive) == 0) {
      continue;
    }
    account->addItem(ui::accountLabel(acc),
                     QVariant::fromValue(static_cast<qlonglong>(acc.id.value)));
    if (acc.id == row.accountId) {
      selected = account->count() - 1;
    }
  }
  account->setCurrentIndex(selected);
  auto * debit = new QLineEdit(row.debit.isZero() ? QString() : row.debit.toDisplay());
  auto * credit = new QLineEdit(row.credit.isZero() ? QString() : row.credit.toDisplay());
  debit->setAlignment(Qt::AlignRight);
  credit->setAlignment(Qt::AlignRight);
  money_->setCellWidget(index, 0, account);
  money_->setCellWidget(index, 1, debit);
  money_->setCellWidget(index, 2, credit);
  connect(account, &QComboBox::currentIndexChanged, this, &OpeningDocumentDialog::refreshGate);
  connect(debit, &QLineEdit::textChanged, this, &OpeningDocumentDialog::refreshGate);
  connect(credit, &QLineEdit::textChanged, this, &OpeningDocumentDialog::refreshGate);
}

void OpeningDocumentDialog::addGoodsRow(const GoodsLine & row)
{
  const int index = goods_->rowCount();
  goods_->insertRow(index);
  auto * nomenclature = new QComboBox;
  ui::fillNomenclatureCombo(nomenclature, session_, row.nomenclatureId);
  auto * qty = new QLineEdit(row.quantity.isZero() ? QString() : row.quantity.toDisplay());
  auto * price = new QLineEdit(row.unitPrice.isZero() ? QString() : row.unitPrice.toDisplay());
  auto * amount = new QLineEdit(row.amount.isZero() ? QString() : row.amount.toDisplay());
  qty->setAlignment(Qt::AlignRight);
  price->setAlignment(Qt::AlignRight);
  amount->setAlignment(Qt::AlignRight);
  bindQtyPrice(qty, price, amount);
  goods_->setCellWidget(index, 0, nomenclature);
  goods_->setCellWidget(index, 1, qty);
  goods_->setCellWidget(index, 2, price);
  goods_->setCellWidget(index, 3, amount);
  connect(nomenclature, &QComboBox::currentIndexChanged, this, &OpeningDocumentDialog::refreshGate);
  connect(qty, &QLineEdit::textChanged, this, &OpeningDocumentDialog::refreshGate);
  connect(price, &QLineEdit::textChanged, this, &OpeningDocumentDialog::refreshGate);
  connect(amount, &QLineEdit::textChanged, this, &OpeningDocumentDialog::refreshGate);
}

Status OpeningDocumentDialog::collect(Document & out) const
{
  out = document_;
  out.number = number_->text().trimmed();
  out.date = date_->date();
  out.comment = comment_->toPlainText().trimmed();
  std::vector<OpeningMoneyLine> money;
  for (int row = 0; row < money_->rowCount(); ++row) {
    auto * account = qobject_cast<QComboBox *>(money_->cellWidget(row, 0));
    auto * debitEdit = qobject_cast<QLineEdit *>(money_->cellWidget(row, 1));
    auto * creditEdit = qobject_cast<QLineEdit *>(money_->cellWidget(row, 2));
    if (account == nullptr || debitEdit == nullptr || creditEdit == nullptr) {
      return Status::fail(Error::validation(QStringLiteral("пошкоджений рядок рахунку")));
    }
    OpeningMoneyLine line;
    line.accountId = ui::selectedAccount(account);
    if (!debitEdit->text().trimmed().isEmpty()) {
      const Result<Money> debit = Money::fromDisplayString(debitEdit->text());
      if (!debit) {
        return debit.asStatus();
      }
      line.debit = debit.value();
    }
    if (!creditEdit->text().trimmed().isEmpty()) {
      const Result<Money> credit = Money::fromDisplayString(creditEdit->text());
      if (!credit) {
        return credit.asStatus();
      }
      line.credit = credit.value();
    }
    money.push_back(line);
  }
  std::vector<GoodsLine> goods;
  for (int row = 0; row < goods_->rowCount(); ++row) {
    auto * nomenclature = qobject_cast<QComboBox *>(goods_->cellWidget(row, 0));
    auto * qtyEdit = qobject_cast<QLineEdit *>(goods_->cellWidget(row, 1));
    auto * priceEdit = qobject_cast<QLineEdit *>(goods_->cellWidget(row, 2));
    auto * amountEdit = qobject_cast<QLineEdit *>(goods_->cellWidget(row, 3));
    if (nomenclature == nullptr || qtyEdit == nullptr || amountEdit == nullptr) {
      return Status::fail(Error::validation(QStringLiteral("пошкоджений рядок товару")));
    }
    GoodsLine line;
    line.nomenclatureId = ui::selectedNomenclature(nomenclature);
    if (!qtyEdit->text().trimmed().isEmpty()) {
      const Result<Quantity> qty = Quantity::fromDisplayString(qtyEdit->text());
      if (!qty) {
        return qty.asStatus();
      }
      line.quantity = qty.value();
    }
    if (priceEdit != nullptr && !priceEdit->text().trimmed().isEmpty()) {
      const Result<Money> price = Money::fromDisplayString(priceEdit->text());
      if (!price) {
        return price.asStatus();
      }
      line.unitPrice = price.value();
    }
    if (!amountEdit->text().trimmed().isEmpty()) {
      const Result<Money> amount = Money::fromDisplayString(amountEdit->text());
      if (!amount) {
        return amount.asStatus();
      }
      line.amount = amount.value();
    }
    goods.push_back(line);
  }
  const Result<std::vector<DocumentLine>> composed = composeOpening(accounts_, money, goods);
  if (!composed) {
    return composed.asStatus();
  }
  out.lines = composed.value();
  return Status::ok();
}

Status OpeningDocumentDialog::save()
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

Status OpeningDocumentDialog::saveAndPost()
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

void OpeningDocumentDialog::unpost()
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

ui::FormButtons OpeningDocumentDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, nullptr, reason_};
}

void OpeningDocumentDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, document_.date,
                             [this](Document & out) { return collect(out); });
}

void OpeningDocumentDialog::updatePostedUi()
{
  const bool posted = document_.posted;
  number_->setEnabled(!posted);
  date_->setEnabled(!posted);
  money_->setEnabled(!posted);
  goods_->setEnabled(!posted);
  comment_->setEnabled(!posted);
}

void OpeningDocumentDialog::openHelp()
{
  HelpBrowser::openTopic(this, HelpBrowser::topicForDocument(document_.type));
}

} // namespace fin
