#include "ui/VatStatementPage.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/edo/DeclarationXml.h"
#include "fin/print/PrintForms.h"
#include "fin/reports/Reports.h"

#include <QAbstractItemView>
#include <QDateEdit>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {
namespace {

void putRow(QTableWidget * table, int row, const TrialBalanceRow & item)
{
  table->setItem(row, 0, new QTableWidgetItem(item.code));
  table->setItem(row, 1, new QTableWidgetItem(item.name));
  auto money = [](Money value) {
    auto * cell = new QTableWidgetItem(value.toDisplay());
    cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return cell;
  };
  table->setItem(row, 2, money(item.openingDebit));
  table->setItem(row, 3, money(item.openingCredit));
  table->setItem(row, 4, money(item.turnoverDebit));
  table->setItem(row, 5, money(item.turnoverCredit));
  table->setItem(row, 6, money(item.closingDebit));
  table->setItem(row, 7, money(item.closingCredit));
}

} // namespace

VatStatementPage::VatStatementPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * bar = new QHBoxLayout;
  from_ = new QDateEdit(QDate(QDate::currentDate().year(), 1, 1));
  to_ = new QDateEdit(QDate::currentDate());
  from_->setCalendarPopup(true);
  to_->setCalendarPopup(true);
  from_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  to_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  auto * build = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                 QStringLiteral("Сформувати"));
  auto * exp = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                               QStringLiteral("Друк / PDF"));
  auto * expXml = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                  QStringLiteral("Експорт в XML (ДПС)"));
  bar->addWidget(new QLabel(QStringLiteral("Період")));
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(build);
  bar->addWidget(exp);
  bar->addWidget(expXml);
  bar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(8);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Рахунок"), QStringLiteral("Назва"), QStringLiteral("Сальдо поч. Дт"),
       QStringLiteral("Сальдо поч. Кт"), QStringLiteral("Оборот Дт"), QStringLiteral("Оборот Кт"),
       QStringLiteral("Сальдо кін. Дт"), QStringLiteral("Сальдо кін. Кт")});
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);

  auto * hint = new QLabel(
      QStringLiteral("Обороти 643 (зобов'язання), 644 (кредит) і 641 (бюджет) за той самий "
                     "період, що ОСВ. Це не декларація ДПС."));
  hint->setWordWrap(true);

  layout->addLayout(bar);
  layout->addWidget(hint);
  layout->addWidget(table_);
  connect(build, &QPushButton::clicked, this, &VatStatementPage::rebuild);
  connect(exp, &QPushButton::clicked, this, [this]() {
    const Result<VatStatement> report =
        VatStatementReport(session_.handle()).build(from_->date(), to_->date());
    if (!report) {
      ui::showError(this, report.error());
      return;
    }
    const Result<QString> html = PrintForms(session_.handle()).vatStatementHtml(report.value());
    if (!html) {
      ui::showError(this, html.error());
      return;
    }
    ui::printHtml(this, QStringLiteral("Відомість ПДВ"), html.value());
  });
  connect(expXml, &QPushButton::clicked, this, [this]() {
    const Result<VatStatement> report =
        VatStatementReport(session_.handle()).build(from_->date(), to_->date());
    if (!report) {
      ui::showError(this, report.error());
      return;
    }
    const Result<CompanyProfile> company = session_.company().load();
    if (!company) {
      ui::showError(this, company.error());
      return;
    }
    const Result<QString> xml = buildVatDeclarationXml(report.value(), company.value());
    if (!xml) {
      ui::showError(this, xml.error());
      return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Зберегти XML"),
                                                      QStringLiteral("J0200125.xml"),
                                                      QStringLiteral("XML файлі (*.xml)"));
    if (path.isEmpty()) {
      return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      ui::showError(this, Error::validation(QStringLiteral("Не вдалося відкрити файл для запису")));
      return;
    }
    file.write(xml.value().toUtf8());
    file.close();
  });
  rebuild();
}

void VatStatementPage::rebuild()
{
  const Result<VatStatement> report =
      VatStatementReport(session_.handle()).build(from_->date(), to_->date());
  if (!report) {
    ui::showError(this, report.error());
    return;
  }
  table_->setRowCount(3);
  putRow(table_, 0, report.value().liability);
  putRow(table_, 1, report.value().credit);
  putRow(table_, 2, report.value().budget);
}

} // namespace fin
