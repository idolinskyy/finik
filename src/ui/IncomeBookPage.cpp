#include "ui/IncomeBookPage.h"

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

IncomeBookPage::IncomeBookPage(LedgerSession & session, QWidget * parent)
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
                                  QStringLiteral("XML декларація ЄП"));
  bar->addWidget(new QLabel(QStringLiteral("Період")));
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(build);
  bar->addWidget(exp);
  bar->addWidget(expXml);
  bar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(6);
  table_->setHorizontalHeaderLabels({QStringLiteral("Дата"), QStringLiteral("Номер"),
                                     QStringLiteral("Документ"), QStringLiteral("Контрагент"),
                                     QStringLiteral("Рахунок"), QStringLiteral("Сума")});
  table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);

  layout->addLayout(bar);
  layout->addWidget(table_);
  connect(build, &QPushButton::clicked, this, &IncomeBookPage::rebuild);
  connect(exp, &QPushButton::clicked, this, [this]() {
    const Result<IncomeBook> book =
        IncomeBookReport(session_.handle()).build(from_->date(), to_->date());
    if (!book) {
      ui::showError(this, book.error());
      return;
    }
    const Result<QString> html = PrintForms(session_.handle()).incomeBookHtml(book.value());
    if (!html) {
      ui::showError(this, html.error());
      return;
    }
    ui::printHtml(this, QStringLiteral("Книга доходів"), html.value());
  });
  connect(expXml, &QPushButton::clicked, this, &IncomeBookPage::doExportXml);
  rebuild();
}

void IncomeBookPage::rebuild()
{
  const Result<IncomeBook> book =
      IncomeBookReport(session_.handle()).build(from_->date(), to_->date());
  if (!book) {
    ui::showError(this, book.error());
    return;
  }
  const IncomeBook & bookData = book.value();
  table_->setRowCount(static_cast<int>(bookData.rows.size()) + 2);
  int row = 0;
  for (const IncomeBookRow & item : bookData.rows) {
    table_->setItem(row, 0, new QTableWidgetItem(item.date.toString(QStringLiteral("dd.MM.yyyy"))));
    table_->setItem(row, 1, new QTableWidgetItem(item.number));
    table_->setItem(row, 2, new QTableWidgetItem(item.typeLabel));
    table_->setItem(row, 3, new QTableWidgetItem(item.party));
    table_->setItem(row, 4, new QTableWidgetItem(item.account));
    auto * amount = new QTableWidgetItem(item.amount.toDisplay());
    amount->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 5, amount);
    ++row;
  }
  auto * incomeLabel = new QTableWidgetItem(QStringLiteral("Дохід"));
  QFont bold = incomeLabel->font();
  bold.setBold(true);
  incomeLabel->setFont(bold);
  table_->setItem(row, 0, incomeLabel);
  auto * income = new QTableWidgetItem(bookData.income.toDisplay());
  income->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  income->setFont(bold);
  table_->setItem(row, 5, income);
  ++row;
  const QString taxLabel = bookData.taxSystem == TaxSystem::Simplified
                               ? QStringLiteral("Єдиний податок %1%")
                                     .arg(QString::number(bookData.rateBp / 100.0, 'f', 2))
                               : QStringLiteral("Єдиний податок (загальна система — не рахується)");
  auto * taxTitle = new QTableWidgetItem(taxLabel);
  taxTitle->setFont(bold);
  table_->setItem(row, 0, taxTitle);
  auto * tax = new QTableWidgetItem(bookData.tax.toDisplay());
  tax->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  tax->setFont(bold);
  table_->setItem(row, 5, tax);
}

void IncomeBookPage::doExportXml()
{
  const Result<IncomeBook> book =
      IncomeBookReport(session_.handle()).build(from_->date(), to_->date());
  if (!book) {
    ui::showError(this, book.error());
    return;
  }

  const Result<CompanyProfile> company = session_.company().load();
  if (!company) {
    ui::showError(this, company.error());
    return;
  }

  // Попередній квартал → R13 / R24.
  Money priorTax = Money::zero();
  Money priorMil = Money::zero();
  const QDate curFrom = book.value().from;
  const int curQ = (curFrom.month() - 1) / 3 + 1;
  int prevYear = curFrom.year();
  int prevQ = curQ - 1;
  if (prevQ < 1) {
    prevQ = 4;
    --prevYear;
  }
  const QDate prevFrom(prevYear, prevQ * 3 - 2, 1);
  const QDate prevTo(prevYear, prevQ * 3, QDate(prevYear, prevQ * 3, 1).daysInMonth());
  Result<IncomeBook> prior = IncomeBookReport(session_.handle()).build(prevFrom, prevTo);
  if (prior && prior.value().taxSystem == TaxSystem::Simplified) {
    priorTax = prior.value().tax;
    // ВЗ 1% з доходу попереднього кварталу (для R24).
    priorMil = Money::fromCents((prior.value().income.cents() + 50) / 100);
  }

  const Result<QString> xml =
      buildSingleTaxDeclarationXml(book.value(), company.value(), priorTax, priorMil);
  if (!xml) {
    ui::showError(this, xml.error());
    return;
  }

  const int quarter = (book.value().to.month() - 1) / 3 + 1;
  const QString suggested =
      QStringLiteral("F0103308_%1_Q%2.xml").arg(book.value().to.year()).arg(quarter);
  const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Зберегти декларацію ЄП"),
                                                    suggested, QStringLiteral("XML (*.xml)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    ui::showError(this, Error::database(QStringLiteral("Не вдалося записати файл")));
    return;
  }
  file.write(xml.value().toUtf8());
}

} // namespace fin
