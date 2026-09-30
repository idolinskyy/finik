#include "ui/CurrencyPage.h"

#include "ui/UiSupport.h"

#include "fin/currency/NbuRateClient.h"
#include "fin/domain/Currency.h"

#include <QAbstractItemView>
#include <QDate>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {
namespace {

bool promptNewCurrency(QWidget * parent, Currency & currency)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("Нова валюта"));
  auto * form = new QFormLayout;
  auto * code = new QLineEdit;
  code->setPlaceholderText(QStringLiteral("USD, EUR, GBP..."));
  code->setMaxLength(3);
  auto * numCode = new QSpinBox;
  numCode->setRange(1, 999);
  numCode->setValue(840);
  auto * name = new QLineEdit;
  name->setPlaceholderText(QStringLiteral("Долар США, Євро..."));
  auto * symbol = new QLineEdit;
  symbol->setPlaceholderText(QStringLiteral("$, €, £..."));

  form->addRow(QStringLiteral("Код (ISO 4217)"), code);
  form->addRow(QStringLiteral("Числовий код"), numCode);
  form->addRow(QStringLiteral("Назва (укр.)"), name);
  form->addRow(QStringLiteral("Символ"), symbol);

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  dialog.resize(360, 220);

  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  currency.code = code->text().trimmed().toUpper();
  currency.numericCode = numCode->value();
  currency.nameUa = name->text().trimmed();
  currency.symbol = symbol->text().trimmed();
  currency.isActive = true;
  return !currency.code.isEmpty() && !currency.nameUa.isEmpty();
}

bool promptNewRate(QWidget * parent, const QString & currencyCode, CurrencyRate & rate)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("Встановлення курсу НБУ"));
  auto * form = new QFormLayout;
  auto * dateEdit = new QDateEdit(QDate::currentDate());
  dateEdit->setCalendarPopup(true);
  dateEdit->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));

  auto * nominalSpin = new QSpinBox;
  nominalSpin->setRange(1, 10000);
  nominalSpin->setValue(1);

  auto * rateEdit = new QLineEdit;
  rateEdit->setPlaceholderText(QStringLiteral("Наприклад, 41.5234"));

  form->addRow(QStringLiteral("Валюта"), new QLabel(currencyCode));
  form->addRow(QStringLiteral("Дата"), dateEdit);
  form->addRow(QStringLiteral("Номінал (одиниць)"), nominalSpin);
  form->addRow(QStringLiteral("Курс (грн)"), rateEdit);

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  dialog.resize(360, 220);

  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }

  const QString text = rateEdit->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.'));
  const QStringList parts = text.split(QLatin1Char('.'));
  if (parts.isEmpty()) {
    return false;
  }
  bool ok1 = false;
  const std::int64_t intPart = parts[0].toLongLong(&ok1);
  if (!ok1) {
    return false;
  }
  std::int64_t fracPart = 0;
  if (parts.size() > 1) {
    QString fracStr = parts[1].left(4);
    while (fracStr.length() < 4) {
      fracStr += QLatin1Char('0');
    }
    bool ok2 = false;
    fracPart = fracStr.toLongLong(&ok2);
    if (!ok2) {
      return false;
    }
  }

  const std::int64_t micros = intPart * ExchangeRate::kMicrosPerUah + fracPart * 100;
  if (micros <= 0) {
    return false;
  }

  rate.currencyCode = currencyCode;
  rate.rateDate = dateEdit->date();
  rate.nominal = nominalSpin->value();
  rate.rateMicros = micros;
  return true;
}

} // namespace

CurrencyPage::CurrencyPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  nbuClient_ = new NbuRateClient(this);

  auto * rootLayout = new QVBoxLayout(this);
  auto * splitter = new QSplitter(Qt::Horizontal, this);

  // 1. Ліва панель: Довідник валют
  auto * leftWidget = new QWidget(this);
  auto * leftLayout = new QVBoxLayout(leftWidget);
  auto * leftToolbar = new QHBoxLayout;
  addCurrencyBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                    QStringLiteral("Додати валюту"));
  leftToolbar->addWidget(addCurrencyBtn_);
  leftToolbar->addStretch();

  currencyTable_ = new QTableWidget(this);
  currencyTable_->setColumnCount(4);
  currencyTable_->setHorizontalHeaderLabels({QStringLiteral("Код"), QStringLiteral("Символ"),
                                             QStringLiteral("Числовий"), QStringLiteral("Назва")});
  currencyTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  currencyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  currencyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  currencyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  currencyTable_->verticalHeader()->setVisible(false);

  leftLayout->addLayout(leftToolbar);
  leftLayout->addWidget(currencyTable_);
  splitter->addWidget(leftWidget);

  // 2. Права панель: Курси НБУ та Калькулятор
  auto * rightWidget = new QWidget(this);
  auto * rightLayout = new QVBoxLayout(rightWidget);
  auto * rightToolbar = new QHBoxLayout;
  setRateBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/edit.svg")),
                                QStringLiteral("Встановити курс"));
  removeRateBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/delete.svg")),
                                   QStringLiteral("Видалити курс"));
  syncNbuBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                QStringLiteral("Оновити з сайту НБУ"));
  rightToolbar->addWidget(setRateBtn_);
  rightToolbar->addWidget(removeRateBtn_);
  rightToolbar->addWidget(syncNbuBtn_);
  rightToolbar->addStretch();

  ratesTable_ = new QTableWidget(this);
  ratesTable_->setColumnCount(3);
  ratesTable_->setHorizontalHeaderLabels(
      {QStringLiteral("Дата"), QStringLiteral("Номінал"), QStringLiteral("Курс НБУ (грн)")});
  ratesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  ratesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  ratesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  ratesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ratesTable_->verticalHeader()->setVisible(false);

  // Калькулятор конвертації
  auto * calcGroup = new QGroupBox(QStringLiteral("Швидкий калькулятор НБУ"), this);
  auto * calcLayout = new QHBoxLayout(calcGroup);
  calcDateEdit_ = new QDateEdit(QDate::currentDate(), calcGroup);
  calcDateEdit_->setCalendarPopup(true);
  calcDateEdit_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  calcForeignEdit_ = new QLineEdit(calcGroup);
  calcForeignEdit_->setPlaceholderText(QStringLiteral("Сума у валюті"));
  calcUahEdit_ = new QLineEdit(calcGroup);
  calcUahEdit_->setPlaceholderText(QStringLiteral("Сума в грн"));
  calcToUahBtn_ = new QPushButton(QStringLiteral("→ у Грн"), calcGroup);
  calcToForeignBtn_ = new QPushButton(QStringLiteral("← у Валюту"), calcGroup);

  calcLayout->addWidget(new QLabel(QStringLiteral("Дата:")));
  calcLayout->addWidget(calcDateEdit_);
  calcLayout->addWidget(calcForeignEdit_);
  calcLayout->addWidget(calcToUahBtn_);
  calcLayout->addWidget(calcToForeignBtn_);
  calcLayout->addWidget(calcUahEdit_);

  rightLayout->addLayout(rightToolbar);
  rightLayout->addWidget(ratesTable_);
  rightLayout->addWidget(calcGroup);
  splitter->addWidget(rightWidget);

  splitter->setSizes({400, 600});
  rootLayout->addWidget(splitter);

  connect(addCurrencyBtn_, &QPushButton::clicked, this, &CurrencyPage::addCurrency);
  connect(setRateBtn_, &QPushButton::clicked, this, &CurrencyPage::setRate);
  connect(removeRateBtn_, &QPushButton::clicked, this, &CurrencyPage::removeRate);
  connect(syncNbuBtn_, &QPushButton::clicked, this, &CurrencyPage::syncFromNbu);
  connect(currencyTable_, &QTableWidget::itemSelectionChanged, this, &CurrencyPage::reloadRates);
  connect(calcToUahBtn_, &QPushButton::clicked, this, &CurrencyPage::calculate);
  connect(calcToForeignBtn_, &QPushButton::clicked, this, [this]() {
    const int row = currencyTable_->currentRow();
    if (row < 0) {
      return;
    }
    const QString code = currencyTable_->item(row, 0)->text();
    const Result<CurrencyRate> rateRes = session_.currencies().getRate(code, calcDateEdit_->date());
    if (!rateRes) {
      QMessageBox::warning(
          this, QStringLiteral("Курс не знайдено"),
          QStringLiteral("Не вдалося знайти курс для %1 на %2")
              .arg(code, calcDateEdit_->date().toString(QStringLiteral("dd.MM.yyyy"))));
      return;
    }
    const Result<Money> uah = Money::fromDisplayString(calcUahEdit_->text());
    if (!uah) {
      return;
    }
    const ExchangeRate ex(rateRes.value().rateMicros, rateRes.value().nominal);
    const Result<Money> f = ex.toForeign(uah.value());
    if (f) {
      calcForeignEdit_->setText(f.value().toDisplay());
    }
  });

  reload();
}

void CurrencyPage::reload()
{
  currencyTable_->setRowCount(0);
  const auto list = session_.currencies().listAll();
  if (!list) {
    return;
  }
  for (const Currency & c : list.value()) {
    const int r = currencyTable_->rowCount();
    currencyTable_->insertRow(r);
    currencyTable_->setItem(r, 0, new QTableWidgetItem(c.code));
    currencyTable_->setItem(r, 1, new QTableWidgetItem(c.symbol));
    currencyTable_->setItem(r, 2, new QTableWidgetItem(QString::number(c.numericCode)));
    currencyTable_->setItem(r, 3, new QTableWidgetItem(c.nameUa));
  }
  if (currencyTable_->rowCount() > 0) {
    currencyTable_->selectRow(0);
  } else {
    reloadRates();
  }
}

void CurrencyPage::reloadRates()
{
  ratesTable_->setRowCount(0);
  const int row = currencyTable_->currentRow();
  if (row < 0) {
    setRateBtn_->setEnabled(false);
    removeRateBtn_->setEnabled(false);
    calcToUahBtn_->setEnabled(false);
    calcToForeignBtn_->setEnabled(false);
    return;
  }
  setRateBtn_->setEnabled(true);
  removeRateBtn_->setEnabled(true);
  calcToUahBtn_->setEnabled(true);
  calcToForeignBtn_->setEnabled(true);

  const QString code = currencyTable_->item(row, 0)->text();
  const auto list = session_.currencies().listRates(code);
  if (!list) {
    return;
  }
  for (const CurrencyRate & r : list.value()) {
    const int idx = ratesTable_->rowCount();
    ratesTable_->insertRow(idx);
    ratesTable_->setItem(idx, 0,
                         new QTableWidgetItem(r.rateDate.toString(QStringLiteral("dd.MM.yyyy"))));
    ratesTable_->setItem(idx, 1, new QTableWidgetItem(QString::number(r.nominal)));
    const ExchangeRate ex(r.rateMicros, r.nominal);
    ratesTable_->setItem(idx, 2, new QTableWidgetItem(ex.formatRate(code)));
  }
}

void CurrencyPage::addCurrency()
{
  Currency c;
  if (!promptNewCurrency(this, c)) {
    return;
  }
  Status s = session_.currencies().insert(c);
  if (!s) {
    QMessageBox::warning(this, QStringLiteral("Помилка"), s.error().message);
    return;
  }
  reload();
}

void CurrencyPage::setRate()
{
  const int row = currencyTable_->currentRow();
  if (row < 0) {
    return;
  }
  const QString code = currencyTable_->item(row, 0)->text();
  CurrencyRate rate;
  if (!promptNewRate(this, code, rate)) {
    return;
  }
  Status s = session_.currencies().setRate(rate);
  if (!s) {
    QMessageBox::warning(this, QStringLiteral("Помилка"), s.error().message);
    return;
  }
  reloadRates();
}

void CurrencyPage::removeRate()
{
  const int curRow = currencyTable_->currentRow();
  const int rateRow = ratesTable_->currentRow();
  if (curRow < 0 || rateRow < 0) {
    return;
  }
  const QString code = currencyTable_->item(curRow, 0)->text();
  const QDate date =
      QDate::fromString(ratesTable_->item(rateRow, 0)->text(), QStringLiteral("dd.MM.yyyy"));
  if (QMessageBox::question(this, QStringLiteral("Підтвердження"),
                            QStringLiteral("Видалити курс для %1 на %2?")
                                .arg(code, date.toString(QStringLiteral("dd.MM.yyyy")))) !=
      QMessageBox::Yes) {
    return;
  }
  Status s = session_.currencies().removeRate(code, date);
  if (!s) {
    QMessageBox::warning(this, QStringLiteral("Помилка"), s.error().message);
    return;
  }
  reloadRates();
}

void CurrencyPage::calculate()
{
  const int row = currencyTable_->currentRow();
  if (row < 0) {
    return;
  }
  const QString code = currencyTable_->item(row, 0)->text();
  const Result<CurrencyRate> rateRes = session_.currencies().getRate(code, calcDateEdit_->date());
  if (!rateRes) {
    QMessageBox::warning(
        this, QStringLiteral("Курс не знайдено"),
        QStringLiteral("Не вдалося знайти курс для %1 на %2")
            .arg(code, calcDateEdit_->date().toString(QStringLiteral("dd.MM.yyyy"))));
    return;
  }
  const Result<Money> foreign = Money::fromDisplayString(calcForeignEdit_->text());
  if (!foreign) {
    return;
  }
  const ExchangeRate ex(rateRes.value().rateMicros, rateRes.value().nominal);
  const Result<Money> uah = ex.toUah(foreign.value());
  if (uah) {
    calcUahEdit_->setText(uah.value().toDisplay());
  }
}

void CurrencyPage::syncFromNbu()
{
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("Оновлення курсів з сайту НБУ"));
  auto * layout = new QVBoxLayout(&dialog);
  auto * lbl =
      new QLabel(QStringLiteral("Оберіть дату, за яку завантажити офіційні курси НБУ:\n"
                                "(Курси буде автоматично записано в базу даних для всіх валют)"),
                 &dialog);
  auto * dateEdit =
      new QDateEdit(calcDateEdit_ ? calcDateEdit_->date() : QDate::currentDate(), &dialog);
  dateEdit->setCalendarPopup(true);
  dateEdit->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));

  auto * todayBtn = new QPushButton(QStringLiteral("Сьогодні"), &dialog);
  connect(todayBtn, &QPushButton::clicked, &dialog,
          [dateEdit]() { dateEdit->setDate(QDate::currentDate()); });

  auto * btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  if (auto * okBtn = btnBox->button(QDialogButtonBox::Ok)) {
    okBtn->setText(QStringLiteral("Завантажити"));
  }
  if (auto * cancelBtn = btnBox->button(QDialogButtonBox::Cancel)) {
    cancelBtn->setText(QStringLiteral("Скасувати"));
  }
  connect(btnBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(btnBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto * dateLayout = new QHBoxLayout;
  dateLayout->addWidget(dateEdit);
  dateLayout->addWidget(todayBtn);

  layout->addWidget(lbl);
  layout->addLayout(dateLayout);
  layout->addWidget(btnBox);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QDate targetDate = dateEdit->date();
  syncNbuBtn_->setEnabled(false);
  syncNbuBtn_->setText(QStringLiteral("Завантаження..."));

  nbuClient_->syncRatesToRepository(
      session_.handle(), targetDate, [this, targetDate](Status status) {
        syncNbuBtn_->setEnabled(true);
        syncNbuBtn_->setText(QStringLiteral("Оновити з сайту НБУ"));
        if (!status) {
          QMessageBox::warning(this, QStringLiteral("Помилка оновлення курсів"),
                               QStringLiteral("Не вдалося завантажити курси з API НБУ:\n%1")
                                   .arg(status.error().message));
          return;
        }
        reloadRates();
        QMessageBox::information(
            this, QStringLiteral("Успішно"),
            QStringLiteral("Офіційні курси НБУ на %1 успішно завантажено та збережено.")
                .arg(targetDate.toString(QStringLiteral("dd.MM.yyyy"))));
      });
}

} // namespace fin
