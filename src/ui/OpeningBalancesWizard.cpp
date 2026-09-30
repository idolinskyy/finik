#include "OpeningBalancesWizard.h"

#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "UiSupport.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/Document.h"
#include "fin/posting/PostingEngine.h"

namespace fin {

OpeningBalancesWizard::OpeningBalancesWizard(LedgerSession & session, QWidget * parent)
    : QWizard(parent), session_(session)
{
  setWindowTitle(QStringLiteral("Майстер Введення Початкових Залишків"));
  setMinimumSize(800, 600);
  setWizardStyle(ModernStyle);

  addPage(createIntroPage());
  addPage(createMoneyPage());
  addPage(createInventoryPage());
  addPage(createDebtPage());
}

QWizardPage * OpeningBalancesWizard::createIntroPage()
{
  auto * page = new QWizardPage;
  page->setTitle(QStringLiteral("Введення початкових залишків"));

  auto * layout = new QVBoxLayout(page);
  auto * label = new QLabel(
      QStringLiteral("Цей майстер допоможе вам швидко ввести початкові залишки підприємства: "
                     "гроші в касі та на банківських рахунках, залишки товарів на складах, "
                     "а також заборгованості перед постачальниками та від покупців.\n\n"
                     "Усі дані будуть автоматично збережені як єдиний проведений документ "
                     "«Введення залишків» через допоміжний рахунок 00.\n\n"
                     "Будь ласка, вкажіть дату, на яку ви вводите залишки "
                     "(зазвичай це 31 грудня року, що передує початку роботи в програмі):"));
  label->setWordWrap(true);
  layout->addWidget(label);

  auto * form = new QFormLayout;
  dateEdit_ = new QDateEdit(QDate::currentDate());
  dateEdit_->setCalendarPopup(true);
  form->addRow(QStringLiteral("Дата залишків:"), dateEdit_);
  layout->addLayout(form);

  layout->addStretch();
  return page;
}

QWizardPage * OpeningBalancesWizard::createMoneyPage()
{
  auto * page = new QWizardPage;
  page->setTitle(QStringLiteral("Грошові кошти (Каса та Банк)"));

  auto * layout = new QVBoxLayout(page);

  moneyTable_ = new QTableWidget(0, 2);
  moneyTable_->setHorizontalHeaderLabels({QStringLiteral("Рахунок"), QStringLiteral("Сума (грн)")});
  moneyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  moneyTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  layout->addWidget(moneyTable_);

  auto * addBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                  QStringLiteral("Додати рядок"));
  connect(addBtn, &QPushButton::clicked, this, &OpeningBalancesWizard::addMoneyRow);
  layout->addWidget(addBtn);

  return page;
}

void OpeningBalancesWizard::addMoneyRow()
{
  const int row = moneyTable_->rowCount();
  moneyTable_->insertRow(row);

  auto * combo = new QComboBox;
  combo->addItem(QStringLiteral("301  Каса в національній валюті"),
                 QString::fromUtf8(chart::kCash));
  combo->addItem(QStringLiteral("311  Поточні рахунки в національній валюті"),
                 QString::fromUtf8(chart::kBank));
  moneyTable_->setCellWidget(row, 0, combo);

  auto * amountEdit = new QLineEdit;
  amountEdit->setPlaceholderText(QStringLiteral("0.00"));
  moneyTable_->setCellWidget(row, 1, amountEdit);
}

QWizardPage * OpeningBalancesWizard::createInventoryPage()
{
  auto * page = new QWizardPage;
  page->setTitle(QStringLiteral("Залишки товарів (Рахунок 281)"));

  auto * layout = new QVBoxLayout(page);

  inventoryTable_ = new QTableWidget(0, 4);
  inventoryTable_->setHorizontalHeaderLabels(
      {QStringLiteral("Склад"), QStringLiteral("Номенклатура"), QStringLiteral("Кількість"),
       QStringLiteral("Загальна вартість (грн)")});
  inventoryTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  layout->addWidget(inventoryTable_);

  auto * addBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                  QStringLiteral("Додати товар"));
  connect(addBtn, &QPushButton::clicked, this, &OpeningBalancesWizard::addInventoryRow);
  layout->addWidget(addBtn);

  return page;
}

void OpeningBalancesWizard::addInventoryRow()
{
  const int row = inventoryTable_->rowCount();
  inventoryTable_->insertRow(row);

  auto * whCombo = new QComboBox;
  const auto whResult = session_.warehouses().listAll();
  if (whResult) {
    for (const auto & wh : whResult.value()) {
      whCombo->addItem(wh.name, QVariant::fromValue(static_cast<qlonglong>(wh.id.value)));
    }
  }
  inventoryTable_->setCellWidget(row, 0, whCombo);

  auto * nomCombo = new QComboBox;
  const auto nomResult = session_.nomenclature().listAll();
  if (nomResult) {
    for (const auto & nom : nomResult.value()) {
      nomCombo->addItem(nom.name, QVariant::fromValue(static_cast<qlonglong>(nom.id.value)));
    }
  }
  inventoryTable_->setCellWidget(row, 1, nomCombo);

  auto * qtyEdit = new QLineEdit;
  qtyEdit->setPlaceholderText(QStringLiteral("0.000"));
  inventoryTable_->setCellWidget(row, 2, qtyEdit);

  auto * amountEdit = new QLineEdit;
  amountEdit->setPlaceholderText(QStringLiteral("0.00"));
  inventoryTable_->setCellWidget(row, 3, amountEdit);
}

QWizardPage * OpeningBalancesWizard::createDebtPage()
{
  auto * page = new QWizardPage;
  page->setTitle(QStringLiteral("Взаєморозрахунки (Рахунки 361 та 631)"));

  auto * layout = new QVBoxLayout(page);

  debtTable_ = new QTableWidget(0, 3);
  debtTable_->setHorizontalHeaderLabels({QStringLiteral("Контрагент"),
                                         QStringLiteral("Тип заборгованості"),
                                         QStringLiteral("Сума (грн)")});
  debtTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  layout->addWidget(debtTable_);

  auto * addBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                  QStringLiteral("Додати контрагента"));
  connect(addBtn, &QPushButton::clicked, this, &OpeningBalancesWizard::addDebtRow);
  layout->addWidget(addBtn);

  return page;
}

void OpeningBalancesWizard::addDebtRow()
{
  const int row = debtTable_->rowCount();
  debtTable_->insertRow(row);

  auto * cpCombo = new QComboBox;
  const auto cpResult = session_.counterparties().listAll();
  if (cpResult) {
    for (const auto & cp : cpResult.value()) {
      cpCombo->addItem(cp.name, QVariant::fromValue(static_cast<qlonglong>(cp.id.value)));
    }
  }
  debtTable_->setCellWidget(row, 0, cpCombo);

  auto * typeCombo = new QComboBox;
  typeCombo->addItem(QStringLiteral("Нам винні (Дебіторська 361)"),
                     QString::fromUtf8(chart::kCustomers));
  typeCombo->addItem(QStringLiteral("Ми винні (Кредиторська 631)"),
                     QString::fromUtf8(chart::kSuppliers));
  debtTable_->setCellWidget(row, 1, typeCombo);

  auto * amountEdit = new QLineEdit;
  amountEdit->setPlaceholderText(QStringLiteral("0.00"));
  debtTable_->setCellWidget(row, 2, amountEdit);
}

void OpeningBalancesWizard::accept()
{
  const QDate date = dateEdit_->date();
  Document doc;
  doc.type = DocumentType::OpeningBalance;
  doc.date = date;
  // Номер видає репозиторій перед записом (див. allocateNumber нижче).

  const Result<std::vector<Account>> accResult = session_.accounts().listAll();
  if (!accResult) {
    ui::showError(this, accResult.asStatus());
    return;
  }
  const std::vector<Account> & accounts = accResult.value();

  const AccountId zeroAcc = accountIdByCode(accounts, chart::kAuxiliary);
  if (!zeroAcc) {
    ui::showError(this, QStringLiteral("Не знайдено допоміжний рахунок 00"));
    return;
  }

  auto getAccount = [&](const QString & code) -> AccountId {
    return accountIdByCode(accounts, code.toUtf8().constData());
  };

  // Money
  for (int i = 0; i < moneyTable_->rowCount(); ++i) {
    auto * combo = qobject_cast<QComboBox *>(moneyTable_->cellWidget(i, 0));
    auto * amountEdit = qobject_cast<QLineEdit *>(moneyTable_->cellWidget(i, 1));
    if (!combo || !amountEdit || amountEdit->text().trimmed().isEmpty())
      continue;

    const Result<Money> money = Money::fromDisplayString(amountEdit->text());
    if (!money || money.value().cents() <= 0)
      continue;

    DocumentLine line;
    line.debitAccountId = getAccount(combo->currentData().toString());
    line.creditAccountId = zeroAcc;
    line.amount = money.value();
    doc.lines.push_back(line);
  }

  // Inventory
  for (int i = 0; i < inventoryTable_->rowCount(); ++i) {
    auto * whCombo = qobject_cast<QComboBox *>(inventoryTable_->cellWidget(i, 0));
    auto * nomCombo = qobject_cast<QComboBox *>(inventoryTable_->cellWidget(i, 1));
    auto * qtyEdit = qobject_cast<QLineEdit *>(inventoryTable_->cellWidget(i, 2));
    auto * amountEdit = qobject_cast<QLineEdit *>(inventoryTable_->cellWidget(i, 3));

    if (!whCombo || !nomCombo || !qtyEdit || !amountEdit)
      continue;
    if (qtyEdit->text().trimmed().isEmpty() || amountEdit->text().trimmed().isEmpty())
      continue;

    const Result<Quantity> qty = Quantity::fromDisplayString(qtyEdit->text());
    const Result<Money> money = Money::fromDisplayString(amountEdit->text());
    if (!qty || !money || qty.value().milli() <= 0 || money.value().cents() <= 0)
      continue;

    DocumentLine line;
    line.debitAccountId = getAccount(QString::fromUtf8(chart::kGoods));
    line.creditAccountId = zeroAcc;
    line.warehouseId = WarehouseId{whCombo->currentData().toLongLong()};
    line.nomenclatureId = NomenclatureId{nomCombo->currentData().toLongLong()};
    line.quantity = qty.value();
    line.amount = money.value();
    doc.lines.push_back(line);
  }

  // Debt
  for (int i = 0; i < debtTable_->rowCount(); ++i) {
    auto * cpCombo = qobject_cast<QComboBox *>(debtTable_->cellWidget(i, 0));
    auto * typeCombo = qobject_cast<QComboBox *>(debtTable_->cellWidget(i, 1));
    auto * amountEdit = qobject_cast<QLineEdit *>(debtTable_->cellWidget(i, 2));

    if (!cpCombo || !typeCombo || !amountEdit || amountEdit->text().trimmed().isEmpty())
      continue;

    const Result<Money> money = Money::fromDisplayString(amountEdit->text());
    if (!money || money.value().cents() <= 0)
      continue;

    const QString typeStr = typeCombo->currentData().toString();

    // Заборгованість — рядок того самого документа з контрагентом у рядку:
    // так усі залишки проводяться однією транзакцією, без частково введеного стану.
    DocumentLine line;
    if (typeStr == QString::fromUtf8(chart::kCustomers)) {
      line.debitAccountId = getAccount(QString::fromUtf8(chart::kCustomers));
      line.creditAccountId = zeroAcc;
    } else {
      line.debitAccountId = zeroAcc;
      line.creditAccountId = getAccount(QString::fromUtf8(chart::kSuppliers));
    }
    line.counterpartyId = CounterpartyId{cpCombo->currentData().toLongLong()};
    line.amount = money.value();
    doc.lines.push_back(line);
  }

  if (doc.lines.empty()) {
    ui::showError(this, QStringLiteral("Не введено жодних даних."));
    return;
  }

  const Result<QString> numAlloc =
      session_.documents().allocateNumber(DocumentType::OpeningBalance);
  if (!numAlloc) {
    ui::showError(this, numAlloc.asStatus());
    return;
  }
  doc.number = numAlloc.value();
  const Result<DocumentId> saved = session_.documents().insert(doc);
  if (!saved) {
    ui::showError(this, saved.asStatus());
    return;
  }
  PostingEngine engine(session_.handle());
  const Status posted = engine.post(saved.value());
  if (!posted) {
    ui::showError(this, posted);
    // Документ лишається чернеткою — прибираємо, щоб не тягнути незбалансований
    // залишок у журналі.
    (void)session_.documents().remove(saved.value());
    return;
  }

  QWizard::accept();
}

} // namespace fin
