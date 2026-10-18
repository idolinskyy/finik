#include "ui/BomPage.h"

#include "ui/DocumentDialog.h"
#include "ui/FormActions.h"
#include "ui/UiSupport.h"

#include "fin/domain/BillOfMaterials.h"
#include "fin/domain/DocumentBlueprint.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstdint>
#include <limits>

namespace fin {
namespace {

class BomDialog : public QDialog
{
 public:
  BomDialog(LedgerSession & session, BillOfMaterials bom, QWidget * parent = nullptr)
      : QDialog(parent), session_(session), bom_(std::move(bom))
  {
    setWindowTitle(bom_.id ? QStringLiteral("Редагування специфікації (BOM)")
                           : QStringLiteral("Нова специфікація виробу (BOM)"));
    resize(700, 500);

    name_ = new QLineEdit(bom_.name);
    product_ = new QComboBox;
    ui::fillNomenclatureCombo(product_, session_, bom_.productId);

    outputQuantity_ =
        new QLineEdit(bom_.outputQuantity.isPositive() ? bom_.outputQuantity.toDisplay()
                                                       : QStringLiteral("1.000"));
    isDefault_ = new QCheckBox(QStringLiteral("Основна специфікація для виробу"));
    isDefault_->setChecked(bom_.isDefault);
    isActive_ = new QCheckBox(QStringLiteral("Активна"));
    isActive_->setChecked(bom_.isActive);
    comment_ = new QLineEdit(bom_.comment);

    auto * form = new QFormLayout;
    form->addRow(QStringLiteral("Назва специфікації*"), name_);
    form->addRow(QStringLiteral("Готовий виріб*"), product_);
    form->addRow(QStringLiteral("Кількість випуску*"), outputQuantity_);
    form->addRow(QString(), isDefault_);
    form->addRow(QString(), isActive_);
    form->addRow(QStringLiteral("Коментар"), comment_);

    components_ = new QTableWidget;
    components_->setColumnCount(3);
    components_->setHorizontalHeaderLabels({QStringLiteral("Матеріал / Комплектуючий*"),
                                            QStringLiteral("Норма витрат*"),
                                            QStringLiteral("Примітка")});
    components_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    components_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    components_->verticalHeader()->setVisible(false);

    for (const BomLine & line : bom_.lines) {
      appendComponentRow(line);
    }

    auto * addCompBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                        QStringLiteral("Додати матеріал"));
    auto * removeCompBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/delete.svg")),
                                           QStringLiteral("Видалити"));
    auto * compBar = new QHBoxLayout;
    compBar->addWidget(addCompBtn);
    compBar->addWidget(removeCompBtn);
    compBar->addStretch();

    connect(addCompBtn, &QPushButton::clicked, this, [this]() { appendComponentRow(BomLine{}); });
    connect(removeCompBtn, &QPushButton::clicked, this, [this]() {
      const int row = components_->currentRow();
      if (row >= 0) {
        components_->removeRow(row);
      }
    });

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &BomDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto * layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(new QLabel(
        QStringLiteral("<b>Склад матеріалів та напівфабрикатів на одиницю/партію:</b>")));
    layout->addLayout(compBar);
    layout->addWidget(components_, 1);
    layout->addWidget(buttons);
  }

  const BillOfMaterials & bom() const { return bom_; }

 private:
  void appendComponentRow(const BomLine & line)
  {
    const int row = components_->rowCount();
    components_->insertRow(row);

    auto * combo = new QComboBox;
    ui::fillNomenclatureCombo(combo, session_, line.componentId);
    auto * qtyEdit = new QLineEdit(line.quantity.isPositive() ? line.quantity.toDisplay()
                                                              : QStringLiteral("1.000"));
    auto * noteEdit = new QLineEdit(line.comment);

    components_->setCellWidget(row, 0, combo);
    components_->setCellWidget(row, 1, qtyEdit);
    components_->setCellWidget(row, 2, noteEdit);
  }

  void save()
  {
    if (name_->text().trimmed().isEmpty()) {
      QMessageBox::warning(this, QStringLiteral("Помилка"),
                           QStringLiteral("Вкажіть назву специфікації"));
      return;
    }
    const NomenclatureId prodId = ui::selectedNomenclature(product_);
    if (!prodId) {
      QMessageBox::warning(this, QStringLiteral("Помилка"),
                           QStringLiteral("Оберіть готовий виріб"));
      return;
    }
    const Result<Quantity> outQty = Quantity::fromDisplayString(outputQuantity_->text());
    if (!outQty || !outQty.value().isPositive()) {
      QMessageBox::warning(this, QStringLiteral("Помилка"),
                           QStringLiteral("Вкажіть додатну кількість випуску"));
      return;
    }

    std::vector<BomLine> lines;
    for (int r = 0; r < components_->rowCount(); ++r) {
      auto * combo = qobject_cast<QComboBox *>(components_->cellWidget(r, 0));
      auto * qtyEdit = qobject_cast<QLineEdit *>(components_->cellWidget(r, 1));
      auto * noteEdit = qobject_cast<QLineEdit *>(components_->cellWidget(r, 2));
      if (!combo || !qtyEdit) {
        continue;
      }
      const NomenclatureId matId = ui::selectedNomenclature(combo);
      if (!matId) {
        continue;
      }
      const Result<Quantity> qty = Quantity::fromDisplayString(qtyEdit->text());
      if (!qty || !qty.value().isPositive()) {
        QMessageBox::warning(this, QStringLiteral("Помилка"),
                             QStringLiteral("Рядок %1: некоректна норма витрат").arg(r + 1));
        return;
      }
      BomLine line;
      line.componentId = matId;
      line.quantity = qty.value();
      line.comment = noteEdit ? noteEdit->text().trimmed() : QString();
      lines.push_back(line);
    }
    if (lines.empty()) {
      QMessageBox::warning(this, QStringLiteral("Помилка"),
                           QStringLiteral("Додайте хоча б один матеріал до специфікації"));
      return;
    }

    bom_.name = name_->text().trimmed();
    bom_.productId = prodId;
    bom_.outputQuantity = outQty.value();
    bom_.isDefault = isDefault_->isChecked();
    bom_.isActive = isActive_->isChecked();
    bom_.comment = comment_->text().trimmed();
    bom_.lines = std::move(lines);

    const Status status =
        bom_.id ? session_.bom().update(bom_) : session_.bom().insert(bom_).asStatus();
    if (!status) {
      QMessageBox::critical(this, QStringLiteral("Помилка збереження"), status.error().message);
      return;
    }
    accept();
  }

  LedgerSession & session_;
  BillOfMaterials bom_;
  QLineEdit * name_ = nullptr;
  QComboBox * product_ = nullptr;
  QLineEdit * outputQuantity_ = nullptr;
  QCheckBox * isDefault_ = nullptr;
  QCheckBox * isActive_ = nullptr;
  QLineEdit * comment_ = nullptr;
  QTableWidget * components_ = nullptr;
};

} // namespace

BomPage::BomPage(LedgerSession & session, QWidget * parent) : QWidget(parent), session_(session)
{
  table_ = new QTableWidget;
  table_->setColumnCount(6);
  table_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Назва специфікації"),
                                     QStringLiteral("Готовий виріб"), QStringLiteral("Обсяг"),
                                     QStringLiteral("Основна"), QStringLiteral("Активна")});
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

  componentsTable_ = new QTableWidget;
  componentsTable_->setColumnCount(4);
  componentsTable_->setHorizontalHeaderLabels({QStringLiteral("Матеріал / Напівфабрикат"),
                                               QStringLiteral("Кількість"), QStringLiteral("Од."),
                                               QStringLiteral("Примітка")});
  componentsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  componentsTable_->verticalHeader()->setVisible(false);
  componentsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  componentsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  componentsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

  addBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                            QStringLiteral("Додати специфікацію"));
  editBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/edit.svg")),
                             QStringLiteral("Редагувати"));
  removeBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/delete.svg")),
                               QStringLiteral("Видалити"));
  issueBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                              QStringLiteral("Створити вимогу-накладну"));

  auto * btnLayout = new QHBoxLayout;
  btnLayout->addWidget(addBtn_);
  btnLayout->addWidget(editBtn_);
  btnLayout->addWidget(removeBtn_);
  btnLayout->addWidget(issueBtn_);
  btnLayout->addStretch();

  connect(addBtn_, &QPushButton::clicked, this, &BomPage::add);
  connect(editBtn_, &QPushButton::clicked, this, &BomPage::edit);
  connect(removeBtn_, &QPushButton::clicked, this, &BomPage::removeSelected);
  connect(issueBtn_, &QPushButton::clicked, this, &BomPage::createMaterialIssue);
  connect(table_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *) { edit(); });

  connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
    const BomId id = selectedId();
    componentsTable_->setRowCount(0);
    if (!id) {
      return;
    }
    const Result<BillOfMaterials> loaded = session_.bom().findById(id);
    if (!loaded) {
      return;
    }
    for (const BomLine & line : loaded.value().lines) {
      const int row = componentsTable_->rowCount();
      componentsTable_->insertRow(row);
      const Result<Nomenclature> mat = session_.nomenclature().findById(line.componentId);
      const QString matName = mat ? mat.value().name : QStringLiteral("—");
      const QString unit = mat ? mat.value().unit : QString();
      componentsTable_->setItem(row, 0, new QTableWidgetItem(matName));
      componentsTable_->setItem(row, 1, new QTableWidgetItem(line.quantity.toDisplay()));
      componentsTable_->setItem(row, 2, new QTableWidgetItem(unit));
      componentsTable_->setItem(row, 3, new QTableWidgetItem(line.comment));
    }
  });

  auto * splitter = new QSplitter(Qt::Vertical, this);
  auto * topWidget = new QWidget;
  auto * topLayout = new QVBoxLayout(topWidget);
  topLayout->setContentsMargins(0, 0, 0, 0);
  topLayout->addWidget(
      new QLabel(QStringLiteral("<b>Специфікації виробів (Технологічні карти / BOM):</b>")));
  topLayout->addWidget(table_, 1);
  topLayout->addLayout(btnLayout);

  auto * bottomWidget = new QWidget;
  auto * bottomLayout = new QVBoxLayout(bottomWidget);
  bottomLayout->setContentsMargins(0, 0, 0, 0);
  bottomLayout->addWidget(
      new QLabel(QStringLiteral("<b>Склад матеріалів вибраної специфікації:</b>")));
  bottomLayout->addWidget(componentsTable_, 1);

  splitter->addWidget(topWidget);
  splitter->addWidget(bottomWidget);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);

  auto * mainLayout = new QVBoxLayout(this);
  mainLayout->addWidget(splitter);

  reload();
}

void BomPage::reload()
{
  const Result<std::vector<BillOfMaterials>> list = session_.bom().listAll();
  table_->setRowCount(0);
  componentsTable_->setRowCount(0);
  if (!list) {
    return;
  }
  for (const BillOfMaterials & bom : list.value()) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    const Result<Nomenclature> prod = session_.nomenclature().findById(bom.productId);
    const QString prodName = prod ? prod.value().name : QStringLiteral("—");
    table_->setItem(row, 0, new QTableWidgetItem(QString::number(bom.id.value)));
    table_->setItem(row, 1, new QTableWidgetItem(bom.name));
    table_->setItem(row, 2, new QTableWidgetItem(prodName));
    table_->setItem(row, 3, new QTableWidgetItem(bom.outputQuantity.toDisplay()));
    table_->setItem(row, 4, new QTableWidgetItem(bom.isDefault ? QStringLiteral("✓") : QString()));
    table_->setItem(
        row, 5, new QTableWidgetItem(bom.isActive ? QStringLiteral("✓") : QStringLiteral("Ні")));
  }
}

BomId BomPage::selectedId() const
{
  const int row = table_->currentRow();
  if (row < 0) {
    return {};
  }
  const QTableWidgetItem * item = table_->item(row, 0);
  if (!item) {
    return {};
  }
  return BomId{item->text().toLongLong()};
}

void BomPage::reveal(BomId id)
{
  for (int r = 0; r < table_->rowCount(); ++r) {
    const QTableWidgetItem * item = table_->item(r, 0);
    if (item && item->text().toLongLong() == id.value) {
      table_->selectRow(r);
      return;
    }
  }
}

void BomPage::add()
{
  BillOfMaterials draft;
  draft.isActive = true;
  draft.outputQuantity = Quantity::fromMilli(1000);
  BomDialog dlg(session_, draft, this);
  if (dlg.exec() == QDialog::Accepted) {
    reload();
  }
}

void BomPage::edit()
{
  const BomId id = selectedId();
  if (!id) {
    return;
  }
  const Result<BillOfMaterials> loaded = session_.bom().findById(id);
  if (!loaded) {
    QMessageBox::warning(this, QStringLiteral("Помилка"), loaded.error().message);
    return;
  }
  BomDialog dlg(session_, loaded.value(), this);
  if (dlg.exec() == QDialog::Accepted) {
    reload();
    reveal(id);
  }
}

void BomPage::removeSelected()
{
  const BomId id = selectedId();
  if (!id) {
    return;
  }
  if (QMessageBox::question(this, QStringLiteral("Видалення"),
                            QStringLiteral("Ви дійсно бажаєте видалити цю специфікацію?")) !=
      QMessageBox::Yes) {
    return;
  }
  const Status status = session_.bom().remove(id);
  if (!status) {
    QMessageBox::critical(this, QStringLiteral("Помилка видалення"), status.error().message);
    return;
  }
  reload();
}

void BomPage::createMaterialIssue()
{
  const BomId id = selectedId();
  if (!id) {
    QMessageBox::information(this, QStringLiteral("Вимога-накладна"),
                             QStringLiteral("Спочатку виберіть специфікацію."));
    return;
  }
  const Result<BillOfMaterials> loaded = session_.bom().findById(id);
  if (!loaded) {
    QMessageBox::warning(this, QStringLiteral("Помилка"), loaded.error().message);
    return;
  }
  const BillOfMaterials & bom = loaded.value();
  if (bom.lines.empty()) {
    QMessageBox::warning(this, QStringLiteral("Вимога-накладна"),
                         QStringLiteral("У специфікації немає матеріалів."));
    return;
  }
  if (!bom.outputQuantity.isPositive()) {
    QMessageBox::warning(this, QStringLiteral("Вимога-накладна"),
                         QStringLiteral("Некоректний обсяг випуску в специфікації."));
    return;
  }

  QDialog qtyDialog(this);
  qtyDialog.setWindowTitle(QStringLiteral("Обсяг випуску"));
  auto * qtyEdit = new QLineEdit(bom.outputQuantity.toDisplay());
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Кількість готової продукції"), qtyEdit);
  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, &qtyDialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &qtyDialog, &QDialog::reject);
  auto * layout = new QVBoxLayout(&qtyDialog);
  layout->addWidget(new QLabel(
      QStringLiteral("Норми матеріалів масштабуються відносно обсягу специфікації (%1).")
          .arg(bom.outputQuantity.toDisplay())));
  layout->addLayout(form);
  layout->addWidget(buttons);
  if (qtyDialog.exec() != QDialog::Accepted) {
    return;
  }
  const Result<Quantity> desired = Quantity::fromDisplayString(qtyEdit->text());
  if (!desired || !desired.value().isPositive()) {
    QMessageBox::warning(this, QStringLiteral("Вимога-накладна"),
                         QStringLiteral("Вкажіть додатну кількість випуску."));
    return;
  }

  std::vector<GoodsLine> goods;
  goods.reserve(bom.lines.size());
  for (const BomLine & line : bom.lines) {
    const std::int64_t component = line.quantity.milli();
    const std::int64_t want = desired.value().milli();
    const std::int64_t base = bom.outputQuantity.milli();
    if (component <= 0 || want <= 0 || base <= 0) {
      continue;
    }
    if (component > std::numeric_limits<std::int64_t>::max() / want) {
      QMessageBox::warning(this, QStringLiteral("Вимога-накладна"),
                           QStringLiteral("Переповнення при розрахунку норми матеріалів."));
      return;
    }
    const std::int64_t scaled = (component * want) / base;
    if (scaled <= 0) {
      continue;
    }
    GoodsLine row;
    row.nomenclatureId = line.componentId;
    row.quantity = Quantity::fromMilli(scaled);
    row.description = line.comment;
    goods.push_back(row);
  }
  if (goods.empty()) {
    QMessageBox::warning(this, QStringLiteral("Вимога-накладна"),
                         QStringLiteral("Після масштабування немає рядків матеріалів."));
    return;
  }

  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (!accounts) {
    QMessageBox::warning(this, QStringLiteral("Помилка"), accounts.error().message);
    return;
  }
  Document draft;
  draft.type = DocumentType::MaterialIssue;
  draft.date = QDate::currentDate();
  draft.bomId = bom.id;
  draft.comment = QStringLiteral("За специфікацією «%1»").arg(bom.name);
  const Result<std::vector<DocumentLine>> lines = composeMaterialIssue(accounts.value(), goods);
  if (!lines) {
    QMessageBox::warning(this, QStringLiteral("Помилка"), lines.error().message);
    return;
  }
  draft.lines = lines.value();
  DocumentDialog::openDocument(session_, draft, this);
}

} // namespace fin
