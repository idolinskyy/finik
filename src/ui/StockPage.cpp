#include "ui/StockPage.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/print/PrintForms.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {
namespace {

QTableWidgetItem * qtyItem(Quantity value, bool bold)
{
  auto * item = new QTableWidgetItem(value.isZero() ? QString() : value.toDisplay());
  item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  if (bold) {
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
  }
  return item;
}

QTableWidgetItem * moneyItem(Money value, bool bold)
{
  auto * item = new QTableWidgetItem(value.isZero() ? QString() : value.toDisplay());
  item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  if (bold) {
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
  }
  return item;
}

} // namespace

StockPage::StockPage(LedgerSession & session, QWidget * parent) : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * bar = new QHBoxLayout;
  from_ = new QDateEdit(QDate(QDate::currentDate().year(), 1, 1));
  to_ = new QDateEdit(QDate::currentDate());
  from_->setCalendarPopup(true);
  to_->setCalendarPopup(true);
  from_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  to_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  warehouse_ = new QComboBox;
  ui::fillWarehouseCombo(warehouse_, session_, {}, true);

  auto * build = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                 QStringLiteral("Сформувати"));
  auto * exp = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                               QStringLiteral("Друк / PDF / CSV"));
  bar->addWidget(new QLabel(QStringLiteral("Період")));
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(new QLabel(QStringLiteral("Склад")));
  bar->addWidget(warehouse_);
  bar->addWidget(build);
  bar->addWidget(exp);
  bar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(10);
  table_->setHorizontalHeaderLabels({QStringLiteral("Номенклатура"), QStringLiteral("Од."),
                                     QStringLiteral("Поч. к-сть"), QStringLiteral("Поч. сума"),
                                     QStringLiteral("Надх. к-сть"), QStringLiteral("Надх. сума"),
                                     QStringLiteral("Виб. к-сть"), QStringLiteral("Виб. сума"),
                                     QStringLiteral("Кін. к-сть"), QStringLiteral("Кін. сума")});
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  layout->addLayout(bar);
  layout->addWidget(table_);
  connect(build, &QPushButton::clicked, this, &StockPage::rebuild);
  connect(warehouse_, &QComboBox::currentIndexChanged, this, &StockPage::rebuild);
  connect(exp, &QPushButton::clicked, this, [this]() {
    const WarehouseId whId = ui::selectedWarehouse(warehouse_);
    const Result<StockReportData> report = session_.stock().build(from_->date(), to_->date(), whId);
    if (!report) {
      ui::showError(this, report.error());
      return;
    }
    PrintForms forms(session_.handle());
    const Result<QString> html = forms.stockHtml(report.value());
    const Result<QString> csv = forms.stockCsv(report.value());
    if (!html || !csv) {
      ui::showError(this, html ? csv.error() : html.error());
      return;
    }
    ui::exportReport(this, QStringLiteral("Залишки товарів"), QStringLiteral("sklad.pdf"),
                     html.value(), QStringLiteral("sklad.csv"), csv.value());
  });
  rebuild();
}

void StockPage::rebuild()
{
  const WarehouseId whId = ui::selectedWarehouse(warehouse_);
  const Result<StockReportData> report = session_.stock().build(from_->date(), to_->date(), whId);
  if (!report) {
    ui::showError(this, report.error());
    return;
  }
  table_->setRowCount(static_cast<int>(report.value().rows.size()) + 1);
  int row = 0;
  const auto fill = [&](const StockRow & rowData, bool bold) {
    auto * name = new QTableWidgetItem(rowData.name);
    if (bold) {
      QFont font = name->font();
      font.setBold(true);
      name->setFont(font);
    }
    table_->setItem(row, 0, name);
    table_->setItem(row, 1, new QTableWidgetItem(rowData.unit));
    table_->setItem(row, 2, qtyItem(rowData.openingQty, bold));
    table_->setItem(row, 3, moneyItem(rowData.openingAmount, bold));
    table_->setItem(row, 4, qtyItem(rowData.inQty, bold));
    table_->setItem(row, 5, moneyItem(rowData.inAmount, bold));
    table_->setItem(row, 6, qtyItem(rowData.outQty, bold));
    table_->setItem(row, 7, moneyItem(rowData.outAmount, bold));
    table_->setItem(row, 8, qtyItem(rowData.closingQty, bold));
    table_->setItem(row, 9, moneyItem(rowData.closingAmount, bold));
    ++row;
  };
  for (const StockRow & item : report.value().rows) {
    fill(item, false);
  }
  fill(report.value().totals, true);
}

} // namespace fin
