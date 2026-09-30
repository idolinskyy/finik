#include "ui/SalePurchaseDialogs.h"

#include "ui/DocumentDialog.h"
#include "ui/FormActions.h"
#include "ui/HelpBrowser.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/domain/ChartCodes.h"
#include "fin/prro/CheckboxClient.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Vat.h"
#include "fin/posting/AdvanceOffset.h"
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
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {
namespace {

bool usesCurrencyUi(DocumentType type)
{
  switch (type) {
  case DocumentType::Purchase:
  case DocumentType::Sale:
  case DocumentType::SaleReturn:
  case DocumentType::PurchaseReturn:
  case DocumentType::ServiceSale:
  case DocumentType::CustomerInvoice:
  case DocumentType::CustomerOrder:
  case DocumentType::SupplierOrder:
    return true;
  default:
    return false;
  }
}

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

static void bindVat(QLineEdit * amount, QLineEdit * vat, QLineEdit * gross, int rateBp)
{
  if (amount == nullptr || vat == nullptr || gross == nullptr) {
    return;
  }
  const auto recalc = [amount, vat, gross, rateBp]() {
    if (amount->text().trimmed().isEmpty()) {
      vat->clear();
      gross->clear();
      return;
    }
    const Result<Money> net = Money::fromDisplayString(amount->text());
    if (!net) {
      return;
    }
    const Result<Money> computed = vatOnNet(net.value(), rateBp);
    if (!computed) {
      return;
    }
    vat->setText(computed.value().toDisplay());
    const Result<Money> sum = net.value().plus(computed.value());
    if (sum) {
      gross->setText(sum.value().toDisplay());
    }
  };
  QObject::connect(amount, &QLineEdit::editingFinished, amount, recalc);
  QObject::connect(vat, &QLineEdit::editingFinished, vat, [amount, vat, gross]() {
    const Result<Money> net = Money::fromDisplayString(amount->text());
    const Result<Money> tax = Money::fromDisplayString(vat->text());
    if (!net || !tax) {
      return;
    }
    const Result<Money> sum = net.value().plus(tax.value());
    if (sum) {
      gross->setText(sum.value().toDisplay());
    }
  });
  recalc();
}

static Result<GoodsLine> goodsFromRow(const QTableWidget * table, int row)
{
  auto * nomenclature = qobject_cast<QComboBox *>(table->cellWidget(row, 0));
  auto * qtyEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 1));
  auto * priceEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 2));
  auto * amountEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 3));
  const bool withVat = table->columnCount() >= 7;
  auto * description = qobject_cast<QLineEdit *>(table->cellWidget(row, withVat ? 6 : 4));
  if (nomenclature == nullptr || qtyEdit == nullptr || priceEdit == nullptr ||
      amountEdit == nullptr) {
    return Result<GoodsLine>::fail(
        Error::validation(QStringLiteral("пошкоджений рядок документа")));
  }
  GoodsLine line;
  line.nomenclatureId = ui::selectedNomenclature(nomenclature);
  line.description = description ? description->text().trimmed() : QString();
  if (!qtyEdit->text().trimmed().isEmpty()) {
    const Result<Quantity> qty = Quantity::fromDisplayString(qtyEdit->text());
    if (!qty) {
      return Result<GoodsLine>::fail(qty.error());
    }
    line.quantity = qty.value();
  }
  if (!priceEdit->text().trimmed().isEmpty()) {
    const Result<Money> price = Money::fromDisplayString(priceEdit->text());
    if (!price) {
      return Result<GoodsLine>::fail(price.error());
    }
    line.unitPrice = price.value();
  }
  if (!amountEdit->text().trimmed().isEmpty()) {
    const Result<Money> amount = Money::fromDisplayString(amountEdit->text());
    if (!amount) {
      return Result<GoodsLine>::fail(amount.error());
    }
    line.amount = amount.value();
  }
  if (withVat) {
    line.vatRateBp = table->property("vatRateBp").toInt();
    auto * vatEdit = qobject_cast<QLineEdit *>(table->cellWidget(row, 4));
    if (vatEdit != nullptr && !vatEdit->text().trimmed().isEmpty()) {
      const Result<Money> vat = Money::fromDisplayString(vatEdit->text());
      if (!vat) {
        return Result<GoodsLine>::fail(vat.error());
      }
      line.vatCents = vat.value().cents();
    }
  }
  return Result<GoodsLine>::ok(line);
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

GoodsDocumentDialog::GoodsDocumentDialog(LedgerSession & session, Document document,
                                         QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(document_.type));
  resize(960, 560);
  setWhatsThis(QStringLiteral("Товарна форма: номенклатура, кількість і ціна. Проводки Дт/Кт "
                              "система складає сама."));

  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
  }

  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  const bool isInternalProduction = document_.type == DocumentType::MaterialIssue ||
                                    document_.type == DocumentType::ProductionReport;
  const bool isKit = document_.type == DocumentType::KitAssembly ||
                     document_.type == DocumentType::KitDisassembly;
  const bool hasCounterparty =
      document_.type != DocumentType::WriteOff && document_.type != DocumentType::Stocktake &&
      document_.type != DocumentType::StockTransfer && !isInternalProduction && !isKit;
  const bool optionalCp = document_.type == DocumentType::WriteOff ||
                          document_.type == DocumentType::Stocktake ||
                          document_.type == DocumentType::StockTransfer || isInternalProduction ||
                          isKit;
  counterparty_ = new QComboBox;
  ui::fillCounterpartyCombo(counterparty_, session_, document_.counterpartyId, optionalCp);

  contract_ = new QComboBox;
  ui::fillContractCombo(contract_, session_, document_.counterpartyId, document_.contractId);

  warehouse_ = new QComboBox;
  ui::fillWarehouseCombo(warehouse_, session_, document_.warehouseId);

  const bool isOrder = document_.type == DocumentType::CustomerOrder ||
                       document_.type == DocumentType::SupplierOrder;
  if (isOrder) {
    orderStatus_ = new QComboBox;
    orderStatus_->addItem(QStringLiteral("Чернетка"), QStringLiteral("draft"));
    orderStatus_->addItem(QStringLiteral("Підтверджено"), QStringLiteral("confirmed"));
    orderStatus_->addItem(QStringLiteral("Виконано"), QStringLiteral("completed"));
    orderStatus_->addItem(QStringLiteral("Скасовано"), QStringLiteral("cancelled"));
    const QString status =
        document_.orderStatus.trimmed().isEmpty() ? QStringLiteral("draft") : document_.orderStatus;
    for (int i = 0; i < orderStatus_->count(); ++i) {
      if (orderStatus_->itemData(i).toString() == status) {
        orderStatus_->setCurrentIndex(i);
        break;
      }
    }
  }

  if (document_.type == DocumentType::StockTransfer) {
    targetWarehouse_ = new QComboBox;
    ui::fillWarehouseCombo(targetWarehouse_, session_, document_.targetWarehouseId);
  }

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Номер"), number_);
  form->addRow(QStringLiteral("Дата"), date_);
  if (hasCounterparty) {
    form->addRow(QStringLiteral("Контрагент"), counterparty_);
    form->addRow(QStringLiteral("Договір"), contract_);
  }
  if (orderStatus_ != nullptr) {
    form->addRow(QStringLiteral("Статус замовлення"), orderStatus_);
  }
  if (usesCurrencyUi(document_.type)) {
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
    form->addRow(QStringLiteral("Валюта"), currency_);
    form->addRow(QStringLiteral("Курс"), currencyRate_);
    const auto reloadRate = [this]() {
      ui::loadCurrencyRateForDate(currencyRate_, session_, ui::selectedCurrencyCode(currency_),
                                  date_->date());
      refreshGate();
    };
    connect(currency_, &QComboBox::currentIndexChanged, this, reloadRate);
    connect(date_, &QDateEdit::dateChanged, this, reloadRate);
    connect(currencyRate_, &QLineEdit::textChanged, this, &GoodsDocumentDialog::refreshGate);
  }
  if (document_.type == DocumentType::StockTransfer) {
    form->addRow(QStringLiteral("Склад-відправник"), warehouse_);
    form->addRow(QStringLiteral("Склад-одержувач"), targetWarehouse_);
  } else if (document_.type != DocumentType::ServiceSale) {
    form->addRow(QStringLiteral("Склад"), warehouse_);
  }

  connect(counterparty_, &QComboBox::currentIndexChanged, this, [this]() {
    const CounterpartyId cpId = ui::selectedCounterparty(counterparty_);
    ui::fillContractCombo(contract_, session_, cpId, {});
    refreshOffsetHint();
    refreshGate();
  });
  if (contract_ != nullptr) {
    connect(contract_, &QComboBox::currentIndexChanged, this, &GoodsDocumentDialog::refreshGate);
  }
  if (warehouse_ != nullptr) {
    connect(warehouse_, &QComboBox::currentIndexChanged, this, &GoodsDocumentDialog::refreshGate);
  }
  if (targetWarehouse_ != nullptr) {
    connect(targetWarehouse_, &QComboBox::currentIndexChanged, this,
            &GoodsDocumentDialog::refreshGate);
  }
  if (orderStatus_ != nullptr) {
    connect(orderStatus_, &QComboBox::currentIndexChanged, this, &GoodsDocumentDialog::refreshGate);
  }

  table_ = new QTableWidget;
  const Result<CompanyProfile> company = session_.company().load();
  vatPayer_ = company && company.value().vatPayer && usesCommercialVat(document_.type) &&
              document_.type != DocumentType::WriteOff &&
              document_.type != DocumentType::Stocktake &&
              document_.type != DocumentType::StockTransfer &&
              document_.type != DocumentType::KitAssembly &&
              document_.type != DocumentType::KitDisassembly && !isInternalProduction;
  vatRateBp_ = company ? company.value().vatRateBp : kDefaultVatRateBp;
  if (vatPayer_) {
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({QStringLiteral("Номенклатура"), QStringLiteral("К-сть"),
                                       QStringLiteral("Ціна"), QStringLiteral("Без ПДВ"),
                                       QStringLiteral("ПДВ"), QStringLiteral("З ПДВ"),
                                       QStringLiteral("Зміст")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    table_->setProperty("vatRateBp", vatRateBp_);
  } else {
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({QStringLiteral("Номенклатура"), QStringLiteral("К-сть"),
                                       QStringLiteral("Ціна"), QStringLiteral("Сума"),
                                       QStringLiteral("Зміст")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  }
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
  total_ = new QLabel;
  offsetHint_ = new QLabel;
  offsetHint_->setWordWrap(true);
  fiscalHint_ = new QLabel;
  fiscalHint_->setWordWrap(true);
  fiscalHint_->hide();
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
  printBtn_->setVisible(canPrintDocument(document_.type));
  auto * help = new QPushButton(QStringLiteral("Довідка"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  addFormButtons(this, buttons, saveBtn_, postBtn_, unpostBtn_, printBtn_, help, close);

  QString hintText;
  switch (document_.type) {
  case DocumentType::StockTransfer:
    hintText =
        QStringLiteral("Внутрішнє переміщення між складами. Списання зі складу-відправника "
                       "та оприбуткування на склад-одержувач за середньою собівартістю (Дт 281 Кт "
                       "281).");
    break;
  case DocumentType::KitAssembly:
    hintText = QStringLiteral(
        "Комплектація: рядок 1 — готовий комплект, далі — компоненти. "
        "Собівартість комплекту = сума середніх собівартостей компонентів (Дт 281 Кт 281).");
    break;
  case DocumentType::KitDisassembly:
    hintText = QStringLiteral(
        "Розукомплектація: рядок 1 — комплект, далі — компоненти. "
        "Собівартість комплекту розподіляється на компоненти (за ціною×к-сть або пропорційно "
        "кількості).");
    break;
  case DocumentType::CustomerOrder:
    hintText = QStringLiteral("Замовлення покупця резервує товари на складі. "
                              "Документ не робить проводок у Головну книгу. На його "
                              "підставі створюється видаткова накладна або рахунок.");
    break;
  case DocumentType::SupplierOrder:
    hintText = QStringLiteral("Замовлення постачальнику формує очікувані надходження товарів на "
                              "склад. "
                              "На його підставі створюється прибуткова накладна або платіж.");
    break;
  case DocumentType::Sale:
    hintText = QStringLiteral("Сума рядка — продаж покупцю. Собівартість 281 підставиться при "
                              "проведенні за середньою. Якщо є кредит 681 по цьому покупцю — "
                              "проведення зробить залік авансу (Дт 681 Кт 361).");
    break;
  case DocumentType::SaleReturn:
    hintText = QStringLiteral("Сума рядка — сторно доходу (Дт 701 Кт 361). Повернення на склад "
                              "оцінюється за середньою при проведенні.");
    break;
  case DocumentType::PurchaseReturn:
    hintText = QStringLiteral("Кількість до повернення постачальнику. Сума списання 281 — середня "
                              "на момент проведення.");
    break;
  case DocumentType::WriteOff:
    hintText = QStringLiteral("Списання з 281 на 92. Сума — середня собівартість при проведенні.");
    break;
  case DocumentType::Stocktake:
    hintText = QStringLiteral("Кількість — фактично облічено. Надлишок іде на 71, нестача — на 92. "
                              "Для надлишку без залишку вкажіть суму.");
    break;
  case DocumentType::ServiceSale:
    hintText = QStringLiteral("Послуга: сума обов'язкова, номенклатура — за бажанням. "
                              "Проводка Дт 361 Кт 703, без складу 281. Аванс 681 заліковується "
                              "при проведенні.");
    break;
  case DocumentType::CustomerInvoice:
    hintText = QStringLiteral("Рахунок покупцю не проводиться в журнал — лише друк. "
                              "Далі в журналі: «На підставі» → реалізація або банківський платіж.");
    break;
  default:
    hintText = QStringLiteral("Кожен рядок — надходження на 281 від постачальника (631). "
                              "Якщо є дебет 371 по цьому постачальнику — проведення залікує "
                              "аванс (Дт 631 Кт 371).");
    break;
  }
  auto * hint = new QLabel(hintText);
  hint->setWordWrap(true);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addLayout(lineBar);
  layout->addWidget(table_, 1);
  layout->addWidget(total_);
  layout->addWidget(offsetHint_);
  layout->addWidget(fiscalHint_);
  layout->addWidget(new QLabel(QStringLiteral("Коментар")));
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(buttons);

  std::vector<GoodsLine> existing = extractGoodsLines(document_.type, document_.lines, accounts_);
  if (document_.type == DocumentType::Stocktake) {
    const Result<std::vector<StockBalance>> onHand = session_.inventory().balances();
    if (onHand) {
      existing = extractStocktakeCounted(document_.lines, accounts_, onHand.value());
    }
  }
  if (existing.empty()) {
    addRow(GoodsLine{});
  } else {
    for (const GoodsLine & row : existing) {
      addRow(row);
    }
  }
  updateTotal();
  refreshOffsetHint();
  refreshGate();

  connect(addLine, &QPushButton::clicked, this, [this]() {
    addRow(GoodsLine{});
    refreshGate();
  });
  connect(removeLine, &QPushButton::clicked, this, [this]() {
    const int row = table_->currentRow();
    if (row >= 0) {
      table_->removeRow(row);
      updateTotal();
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
  connect(unpostBtn_, &QPushButton::clicked, this, &GoodsDocumentDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &GoodsDocumentDialog::printForm);
  connect(help, &QPushButton::clicked, this, &GoodsDocumentDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &GoodsDocumentDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &GoodsDocumentDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &GoodsDocumentDialog::refreshGate);
  connect(counterparty_, &QComboBox::currentIndexChanged, this, &GoodsDocumentDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

void GoodsDocumentDialog::addRow(const GoodsLine & row)
{
  const int index = table_->rowCount();
  table_->insertRow(index);
  auto * nomenclature = new QComboBox;
  ui::fillNomenclatureCombo(nomenclature, session_, row.nomenclatureId);
  auto * qty = new QLineEdit(row.quantity.isZero() ? QString() : row.quantity.toDisplay());
  qty->setAlignment(Qt::AlignRight);
  auto * price = new QLineEdit(row.unitPrice.isZero() ? QString() : row.unitPrice.toDisplay());
  price->setAlignment(Qt::AlignRight);
  auto * amount = new QLineEdit(row.amount.isZero() ? QString() : row.amount.toDisplay());
  amount->setAlignment(Qt::AlignRight);
  auto * description = new QLineEdit(row.description);
  bindQtyPrice(qty, price, amount);
  connect(amount, &QLineEdit::editingFinished, this, &GoodsDocumentDialog::updateTotal);
  connect(qty, &QLineEdit::editingFinished, this, &GoodsDocumentDialog::updateTotal);
  connect(price, &QLineEdit::editingFinished, this, &GoodsDocumentDialog::updateTotal);
  table_->setCellWidget(index, 0, nomenclature);
  table_->setCellWidget(index, 1, qty);
  table_->setCellWidget(index, 2, price);
  table_->setCellWidget(index, 3, amount);
  if (vatPayer_) {
    auto * vat =
        new QLineEdit(row.vatCents == 0 ? QString() : Money::fromCents(row.vatCents).toDisplay());
    vat->setAlignment(Qt::AlignRight);
    auto * gross = new QLineEdit;
    gross->setAlignment(Qt::AlignRight);
    gross->setReadOnly(true);
    bindVat(amount, vat, gross, vatRateBp_);
    table_->setCellWidget(index, 4, vat);
    table_->setCellWidget(index, 5, gross);
    table_->setCellWidget(index, 6, description);
    connect(vat, &QLineEdit::editingFinished, this, &GoodsDocumentDialog::updateTotal);
  } else {
    table_->setCellWidget(index, 4, description);
  }
  connect(nomenclature, &QComboBox::currentIndexChanged, this, &GoodsDocumentDialog::refreshGate);
  connect(qty, &QLineEdit::textChanged, this, &GoodsDocumentDialog::refreshGate);
  connect(price, &QLineEdit::textChanged, this, &GoodsDocumentDialog::refreshGate);
  connect(amount, &QLineEdit::textChanged, this, &GoodsDocumentDialog::refreshGate);
}

void GoodsDocumentDialog::updateTotal()
{
  Money acc = Money::zero();
  for (int row = 0; row < table_->rowCount(); ++row) {
    const Result<GoodsLine> line = goodsFromRow(table_, row);
    if (!line) {
      continue;
    }
    const Result<Money> next = acc.plus(line.value().amount);
    if (!next) {
      break;
    }
    acc = next.value();
  }
  total_->setText(QStringLiteral("Разом: %1 грн").arg(acc.toDisplay()));
  refreshGate();
}

void GoodsDocumentDialog::refreshOffsetHint()
{
  if (offsetHint_ == nullptr) {
    return;
  }
  const Money offset = advanceOffsetAmount(accounts_, document_.type, document_.lines);
  if (!offset.isPositive()) {
    offsetHint_->hide();
    offsetHint_->clear();
    return;
  }
  const bool purchase = document_.type == DocumentType::Purchase;
  offsetHint_->setText(
      purchase
          ? QStringLiteral("Залік виданого авансу (Дт 631 Кт 371): %1 грн").arg(offset.toDisplay())
          : QStringLiteral("Залік одержаного авансу (Дт 681 Кт 361): %1 грн")
                .arg(offset.toDisplay()));
  offsetHint_->show();
}

Status GoodsDocumentDialog::collect(Document & out) const
{
  out = document_;
  out.number = number_->text().trimmed();
  out.date = date_->date();
  out.counterpartyId = ui::selectedCounterparty(counterparty_);
  out.contractId = contract_ ? ui::selectedContract(contract_) : ContractId{};
  out.warehouseId = warehouse_ ? ui::selectedWarehouse(warehouse_) : WarehouseId{};
  out.targetWarehouseId =
      targetWarehouse_ ? ui::selectedWarehouse(targetWarehouse_) : WarehouseId{};
  out.comment = comment_->toPlainText().trimmed();
  if (orderStatus_ != nullptr) {
    out.orderStatus = orderStatus_->currentData().toString();
  }
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
  std::vector<GoodsLine> rows;
  for (int row = 0; row < table_->rowCount(); ++row) {
    const Result<GoodsLine> line = goodsFromRow(table_, row);
    if (!line) {
      return line.asStatus();
    }
    rows.push_back(line.value());
  }
  const auto compose = [this, &rows]() -> Result<std::vector<DocumentLine>> {
    switch (document_.type) {
    case DocumentType::StockTransfer:
      return composeStockTransfer(accounts_, rows);
    case DocumentType::KitAssembly:
      return composeKitAssembly(accounts_, rows);
    case DocumentType::KitDisassembly:
      return composeKitDisassembly(accounts_, rows);
    case DocumentType::CustomerOrder:
      return composeCustomerOrder(accounts_, rows);
    case DocumentType::SupplierOrder:
      return composeSupplierOrder(accounts_, rows);
    case DocumentType::MaterialIssue:
      return composeMaterialIssue(accounts_, rows);
    case DocumentType::ProductionReport:
      return composeProductionReport(accounts_, rows);
    case DocumentType::Sale:
      return composeSale(accounts_, rows);
    case DocumentType::SaleReturn:
      return composeSaleReturn(accounts_, rows);
    case DocumentType::PurchaseReturn:
      return composePurchaseReturn(accounts_, rows);
    case DocumentType::WriteOff:
      return composeWriteOff(accounts_, rows);
    case DocumentType::ServiceSale:
      return composeServiceSale(accounts_, rows);
    case DocumentType::CustomerInvoice:
      return composeCustomerInvoice(accounts_, rows);
    case DocumentType::Stocktake: {
      const WarehouseId wh = warehouse_ ? ui::selectedWarehouse(warehouse_) : WarehouseId{};
      const Result<std::vector<StockBalance>> onHand = session_.inventory().balances(wh);
      if (!onHand) {
        return Result<std::vector<DocumentLine>>::fail(onHand.error());
      }
      return composeStocktake(accounts_, rows, onHand.value());
    }
    default:
      return composePurchase(accounts_, rows);
    }
  };
  const Result<std::vector<DocumentLine>> composed = compose();
  if (!composed) {
    return composed.asStatus();
  }
  out.lines = composed.value();
  return Status::ok();
}

Status GoodsDocumentDialog::save()
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

Status GoodsDocumentDialog::saveAndPost()
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
  if (document_.type == DocumentType::Sale || document_.type == DocumentType::ServiceSale ||
      document_.type == DocumentType::SaleReturn) {
    const Status fiscal = tryFiscalizeSaleDocument(session_.handle(), document_.id);
    if (!fiscal) {
      QMessageBox::warning(this, QStringLiteral("ПРРО"),
                           QStringLiteral("Документ проведено, але фіскальний чек не створено:\n%1")
                               .arg(fiscal.message()));
    } else {
      loaded = session_.documents().findById(document_.id);
      if (loaded) {
        document_ = loaded.value();
      }
      if (!document_.fiscalCode.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("ПРРО"),
            QStringLiteral("Фіскальний чек Checkbox: %1").arg(document_.fiscalCode));
      }
    }
  }
  persisted_ = true;
  updatePostedUi();
  refreshOffsetHint();
  return Status::ok();
}

void GoodsDocumentDialog::unpost()
{
  if (!document_.id) {
    return;
  }
  if (!document_.fiscalCode.trimmed().isEmpty() || !document_.fiscalReceiptId.trimmed().isEmpty()) {
    const auto answer = QMessageBox::question(
        this, QStringLiteral("ПРРО"),
        QStringLiteral("Документ фіскалізовано (чек %1). Анулювати чек у Checkbox "
                       "і розпровести документ?")
            .arg(document_.fiscalCode.isEmpty() ? document_.fiscalReceiptId : document_.fiscalCode),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return;
    }
    const Status voided = tryVoidFiscalReceipt(session_.handle(), document_.id);
    if (!voided) {
      ui::showError(this, voided);
      return;
    }
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
  refreshOffsetHint();
  refreshGate();
}

void GoodsDocumentDialog::printForm()
{
  Status status = Status::ok();
  if (!document_.posted) {
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
    ui::printHtml(this, QStringLiteral("Накладна %1").arg(loaded.value().number), html.value());
  } else if (chosen == pdf) {
    ui::savePdf(this, QStringLiteral("nakladna-%1.pdf").arg(loaded.value().number), html.value());
  }
}

ui::FormButtons GoodsDocumentDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

void GoodsDocumentDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, document_.date,
                             [this](Document & out) { return collect(out); });
}

void GoodsDocumentDialog::updatePostedUi()
{
  const bool posted = document_.posted;
  number_->setEnabled(!posted);
  date_->setEnabled(!posted);
  if (counterparty_ != nullptr)
    counterparty_->setEnabled(!posted);
  if (contract_ != nullptr)
    contract_->setEnabled(!posted);
  if (orderStatus_ != nullptr)
    orderStatus_->setEnabled(!posted);
  if (currency_ != nullptr)
    currency_->setEnabled(!posted);
  if (currencyRate_ != nullptr)
    currencyRate_->setEnabled(!posted && currency_ != nullptr &&
                              ui::selectedCurrencyCode(currency_) != QLatin1String("UAH"));
  if (warehouse_ != nullptr)
    warehouse_->setEnabled(!posted);
  if (targetWarehouse_ != nullptr)
    targetWarehouse_->setEnabled(!posted);
  table_->setEnabled(!posted);
  comment_->setEnabled(!posted);
  if (fiscalHint_ != nullptr) {
    if (!document_.fiscalCode.trimmed().isEmpty()) {
      fiscalHint_->setText(QStringLiteral("ПРРО Checkbox: фіскальний № %1 (%2)")
                               .arg(document_.fiscalCode, document_.fiscalStatus));
      fiscalHint_->show();
    } else {
      fiscalHint_->clear();
      fiscalHint_->hide();
    }
  }
}

void GoodsDocumentDialog::openHelp()
{
  if (document_.type == DocumentType::MaterialIssue ||
      document_.type == DocumentType::ProductionReport) {
    HelpBrowser::openTopic(this, QStringLiteral("production.html"));
    return;
  }
  HelpBrowser::openTopic(this, QStringLiteral("goods.html"));
}

ServicePurchaseDialog::ServicePurchaseDialog(LedgerSession & session, Document document,
                                             QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(document_.type));
  resize(860, 500);
  setWhatsThis(
      QStringLiteral("Надходження послуг від постачальника: рахунок витрат або капітальних "
                     "інвестицій і сума. Кредит завжди 631."));

  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
    for (const Account & account : accounts_) {
      if (isServicePurchaseDebitCode(account.code)) {
        debitAccounts_.push_back(account);
      }
    }
  }

  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  counterparty_ = new QComboBox;
  ui::fillCounterpartyCombo(counterparty_, session_, document_.counterpartyId, false);
  contract_ = new QComboBox;
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
  form->addRow(QStringLiteral("Постачальник"), counterparty_);
  form->addRow(QStringLiteral("Договір"), contract_);
  form->addRow(QStringLiteral("Валюта"), currency_);
  form->addRow(QStringLiteral("Курс"), currencyRate_);

  connect(counterparty_, &QComboBox::currentIndexChanged, this, [this]() {
    ui::fillContractCombo(contract_, session_, ui::selectedCounterparty(counterparty_), {});
    refreshOffsetHint();
    refreshGate();
  });
  connect(contract_, &QComboBox::currentIndexChanged, this, &ServicePurchaseDialog::refreshGate);
  const auto reloadRate = [this]() {
    ui::loadCurrencyRateForDate(currencyRate_, session_, ui::selectedCurrencyCode(currency_),
                                date_->date());
    refreshGate();
  };
  connect(currency_, &QComboBox::currentIndexChanged, this, reloadRate);
  connect(date_, &QDateEdit::dateChanged, this, reloadRate);
  connect(currencyRate_, &QLineEdit::textChanged, this, &ServicePurchaseDialog::refreshGate);

  table_ = new QTableWidget;
  const Result<CompanyProfile> company = session_.company().load();
  vatPayer_ = company && company.value().vatPayer;
  vatRateBp_ = company ? company.value().vatRateBp : kDefaultVatRateBp;
  if (vatPayer_) {
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({QStringLiteral("Рахунок"), QStringLiteral("Без ПДВ"),
                                       QStringLiteral("ПДВ"), QStringLiteral("З ПДВ"),
                                       QStringLiteral("Зміст")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->setProperty("vatRateBp", vatRateBp_);
  } else {
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Рахунок"), QStringLiteral("Сума"), QStringLiteral("Зміст")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  }
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
  total_ = new QLabel;
  offsetHint_ = new QLabel;
  offsetHint_->setWordWrap(true);
  reason_ = new QLabel;
  reason_->setWordWrap(true);
  auto * hint = new QLabel(
      QStringLiteral("Кожен рядок — Дт 92 (витрати), 152 (капекс ОЗ) або 154 (капекс НМА) "
                     "Кт 631. Якщо є дебет 371 по цьому постачальнику — проведення залікує "
                     "аванс (Дт 631 Кт 371). Товар на 281 беріть документом «Надходження»."));
  hint->setWordWrap(true);

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
  addFormButtons(this, buttons, saveBtn_, postBtn_, unpostBtn_, printBtn_, help, close);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addLayout(lineBar);
  layout->addWidget(table_, 1);
  layout->addWidget(total_);
  layout->addWidget(offsetHint_);
  layout->addWidget(new QLabel(QStringLiteral("Коментар")));
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(buttons);

  const std::vector<CashLine> existing = extractServicePurchaseLines(document_.lines, accounts_);
  if (existing.empty()) {
    CashLine row;
    row.correspondenceId = accountIdByCode(accounts_, chart::kAdminExpense);
    addRow(row);
  } else {
    for (const CashLine & row : existing) {
      addRow(row);
    }
  }

  connect(addLine, &QPushButton::clicked, this, [this]() {
    CashLine row;
    row.correspondenceId = accountIdByCode(accounts_, chart::kAdminExpense);
    addRow(row);
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
  connect(unpostBtn_, &QPushButton::clicked, this, &ServicePurchaseDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &ServicePurchaseDialog::printForm);
  connect(help, &QPushButton::clicked, this, &ServicePurchaseDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &ServicePurchaseDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &ServicePurchaseDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &ServicePurchaseDialog::refreshGate);
  connect(counterparty_, &QComboBox::currentIndexChanged, this,
          &ServicePurchaseDialog::refreshGate);
  updatePostedUi();
  refreshOffsetHint();
  refreshGate();
}

void ServicePurchaseDialog::addRow(const CashLine & row)
{
  const int index = table_->rowCount();
  table_->insertRow(index);
  auto * account = new QComboBox;
  ui::fillAccountCombo(account, debitAccounts_, row.correspondenceId);
  auto * amount = new QLineEdit(row.amount.isZero() ? QString() : row.amount.toDisplay());
  amount->setAlignment(Qt::AlignRight);
  auto * description = new QLineEdit(row.description);
  table_->setCellWidget(index, 0, account);
  table_->setCellWidget(index, 1, amount);
  if (vatPayer_) {
    auto * vat =
        new QLineEdit(row.vatCents == 0 ? QString() : Money::fromCents(row.vatCents).toDisplay());
    vat->setAlignment(Qt::AlignRight);
    auto * gross = new QLineEdit;
    gross->setAlignment(Qt::AlignRight);
    gross->setReadOnly(true);
    bindVat(amount, vat, gross, vatRateBp_);
    table_->setCellWidget(index, 2, vat);
    table_->setCellWidget(index, 3, gross);
    table_->setCellWidget(index, 4, description);
  } else {
    table_->setCellWidget(index, 2, description);
  }
  connect(account, &QComboBox::currentIndexChanged, this, &ServicePurchaseDialog::refreshGate);
  connect(amount, &QLineEdit::textChanged, this, [this]() {
    Money acc = Money::zero();
    for (int lineNo = 0; lineNo < table_->rowCount(); ++lineNo) {
      const Result<CashLine> line = cashFromRow(table_, lineNo);
      if (!line) {
        continue;
      }
      const Result<Money> next = acc.plus(line.value().amount);
      if (!next) {
        break;
      }
      acc = next.value();
    }
    total_->setText(QStringLiteral("Разом: %1 грн").arg(acc.toDisplay()));
    refreshGate();
  });
}

Status ServicePurchaseDialog::collect(Document & out) const
{
  out = document_;
  out.number = number_->text().trimmed();
  out.date = date_->date();
  out.counterpartyId = ui::selectedCounterparty(counterparty_);
  out.contractId = contract_ ? ui::selectedContract(contract_) : ContractId{};
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
  std::vector<CashLine> rows;
  for (int row = 0; row < table_->rowCount(); ++row) {
    const Result<CashLine> line = cashFromRow(table_, row);
    if (!line) {
      return line.asStatus();
    }
    rows.push_back(line.value());
  }
  const Result<std::vector<DocumentLine>> composed = composeServicePurchase(accounts_, rows);
  if (!composed) {
    return composed.asStatus();
  }
  out.lines = composed.value();
  return Status::ok();
}

Status ServicePurchaseDialog::save()
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
  refreshOffsetHint();
  return Status::ok();
}

Status ServicePurchaseDialog::saveAndPost()
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
  refreshOffsetHint();
  return Status::ok();
}

void ServicePurchaseDialog::unpost()
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
  refreshOffsetHint();
  refreshGate();
}

void ServicePurchaseDialog::printForm()
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
    ui::printHtml(this, QStringLiteral("Акт %1").arg(loaded.value().number), html.value());
  } else if (chosen == pdf) {
    ui::savePdf(this, QStringLiteral("akt-poslug-%1.pdf").arg(loaded.value().number), html.value());
  }
}

void ServicePurchaseDialog::refreshOffsetHint()
{
  if (offsetHint_ == nullptr) {
    return;
  }
  const Money offset = advanceOffsetAmount(accounts_, document_.type, document_.lines);
  if (!offset.isPositive()) {
    offsetHint_->hide();
    offsetHint_->clear();
    return;
  }
  offsetHint_->setText(
      QStringLiteral("Залік виданого авансу (Дт 631 Кт 371): %1 грн").arg(offset.toDisplay()));
  offsetHint_->show();
}

void ServicePurchaseDialog::updatePostedUi()
{
  const bool posted = document_.posted;
  number_->setEnabled(!posted);
  date_->setEnabled(!posted);
  counterparty_->setEnabled(!posted);
  if (contract_ != nullptr)
    contract_->setEnabled(!posted);
  if (currency_ != nullptr)
    currency_->setEnabled(!posted);
  if (currencyRate_ != nullptr)
    currencyRate_->setEnabled(!posted && currency_ != nullptr &&
                              ui::selectedCurrencyCode(currency_) != QLatin1String("UAH"));
  table_->setEnabled(!posted);
  comment_->setEnabled(!posted);
}

ui::FormButtons ServicePurchaseDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

void ServicePurchaseDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, document_.date,
                             [this](Document & out) { return collect(out); });
}

void ServicePurchaseDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("goods.html"));
}

} // namespace fin
