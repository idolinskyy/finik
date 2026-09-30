#include "ui/ConsolidatedTaxPage.h"

#include "fin/edo/DeclarationXml.h"
#include "ui/UiSupport.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {

ConsolidatedTaxPage::ConsolidatedTaxPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * bar = new QHBoxLayout;

  year_ = new QSpinBox;
  year_->setRange(2000, 2100);
  year_->setValue(QDate::currentDate().year());

  quarter_ = new QComboBox;
  quarter_->addItem(QStringLiteral("I Квартал"), 1);
  quarter_->addItem(QStringLiteral("II Квартал"), 2);
  quarter_->addItem(QStringLiteral("III Квартал"), 3);
  quarter_->addItem(QStringLiteral("IV Квартал"), 4);
  int curQ = (QDate::currentDate().month() - 1) / 3 + 1;
  quarter_->setCurrentIndex(curQ - 1);

  auto * btnBuild = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                    QStringLiteral("Розрахувати"));
  auto * btnExportMain = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                         QStringLiteral("XML Розрахунок"));
  auto * btnExport4Df = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                        QStringLiteral("XML 4ДФ"));
  auto * btnExportD1 = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                       QStringLiteral("XML Д1 (ЄСВ)"));
  auto * btnExportD5 = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                       QStringLiteral("XML Д5"));

  bar->addWidget(new QLabel(QStringLiteral("Рік:")));
  bar->addWidget(year_);
  bar->addWidget(new QLabel(QStringLiteral("Квартал:")));
  bar->addWidget(quarter_);
  bar->addWidget(btnBuild);
  bar->addWidget(btnExportMain);
  bar->addWidget(btnExport4Df);
  bar->addWidget(btnExportD1);
  bar->addWidget(btnExportD5);
  bar->addStretch();

  layout->addLayout(bar);

  auto * form = new QFormLayout;
  lblGross_ = new QLabel(QStringLiteral("0.00"));
  lblPit_ = new QLabel(QStringLiteral("0.00"));
  lblMilitary_ = new QLabel(QStringLiteral("0.00"));
  lblSsc_ = new QLabel(QStringLiteral("0.00"));
  lblPaid_ = new QLabel(QStringLiteral("0.00"));
  lblCount_ = new QLabel(QStringLiteral("0"));

  form->addRow(QStringLiteral("Кількість застрахованих осіб:"), lblCount_);
  form->addRow(QStringLiteral("Нараховано доходу:"), lblGross_);
  form->addRow(QStringLiteral("Виплачено доходу:"), lblPaid_);
  form->addRow(QStringLiteral("Нараховано ЄСВ:"), lblSsc_);
  form->addRow(QStringLiteral("Утримано ПДФО:"), lblPit_);
  form->addRow(QStringLiteral("Утримано ВЗ:"), lblMilitary_);

  layout->addLayout(form);

  table_ = new QTableWidget;
  table_->setColumnCount(8);
  table_->setHorizontalHeaderLabels({QStringLiteral("РНОКПП"), QStringLiteral("ПІБ"),
                                     QStringLiteral("Нараховано"), QStringLiteral("Виплачено"),
                                     QStringLiteral("ЄСВ"), QStringLiteral("ПДФО"),
                                     QStringLiteral("ВЗ"), QStringLiteral("ПДФО вип.")});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  layout->addWidget(table_, 1);

  connect(btnBuild, &QPushButton::clicked, this, &ConsolidatedTaxPage::rebuild);
  connect(btnExportMain, &QPushButton::clicked, this, &ConsolidatedTaxPage::doExportMain);
  connect(btnExport4Df, &QPushButton::clicked, this, &ConsolidatedTaxPage::doExport4Df);
  connect(btnExportD1, &QPushButton::clicked, this, &ConsolidatedTaxPage::doExportD1);
  connect(btnExportD5, &QPushButton::clicked, this, &ConsolidatedTaxPage::doExportD5);
}

void ConsolidatedTaxPage::rebuild()
{
  const Result<ConsolidatedTaxReport> built = buildConsolidatedTaxReport(
      session_.handle(), year_->value(), quarter_->currentData().toInt());
  if (!built) {
    ui::showError(this, built.error());
    return;
  }
  report_ = built.value();

  lblGross_->setText(report_.totalGross.toDisplay());
  lblPaid_->setText(report_.totalPaidGross.toDisplay());
  lblPit_->setText(report_.totalPit.toDisplay());
  lblMilitary_->setText(report_.totalMilitary.toDisplay());
  lblSsc_->setText(report_.totalSsc.toDisplay());
  lblCount_->setText(QString::number(report_.employeeCount));

  table_->setRowCount(0);
  for (const EmployeeTaxRow & emp : report_.employees) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, new QTableWidgetItem(emp.taxId));
    table_->setItem(row, 1, new QTableWidgetItem(emp.name));
    table_->setItem(row, 2, new QTableWidgetItem(emp.gross.toDisplay()));
    table_->setItem(row, 3, new QTableWidgetItem(emp.paidGross.toDisplay()));
    table_->setItem(row, 4, new QTableWidgetItem(emp.ssc.toDisplay()));
    table_->setItem(row, 5, new QTableWidgetItem(emp.pit.toDisplay()));
    table_->setItem(row, 6, new QTableWidgetItem(emp.military.toDisplay()));
    table_->setItem(row, 7, new QTableWidgetItem(emp.paidPit.toDisplay()));
  }
}

bool ConsolidatedTaxPage::ensureReport()
{
  if (report_.year == 0) {
    rebuild();
  }
  return report_.year != 0;
}

bool ConsolidatedTaxPage::saveXmlFile(const QString & title, const QString & suggestedName,
                                      const QString & xml)
{
  const QString path =
      QFileDialog::getSaveFileName(this, title, suggestedName, QStringLiteral("XML (*.xml)"));
  if (path.isEmpty()) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    ui::showError(this, Error::database(QStringLiteral("Не вдалося записати файл")));
    return false;
  }
  file.write(xml.toUtf8());
  return true;
}

void ConsolidatedTaxPage::doExportMain()
{
  if (!ensureReport()) {
    return;
  }
  const Result<CompanyProfile> company = session_.company().load();
  if (!company) {
    ui::showError(this, company.error());
    return;
  }
  const Result<QString> xml = buildConsolidatedTaxXml(report_, company.value());
  if (!xml) {
    ui::showError(this, xml.error());
    return;
  }
  const QString name = QStringLiteral("J0500111_%1_Q%2.xml").arg(report_.year).arg(report_.quarter);
  saveXmlFile(QStringLiteral("Зберегти Розрахунок XML"), name, xml.value());
}

void ConsolidatedTaxPage::doExport4Df()
{
  if (!ensureReport()) {
    return;
  }
  const Result<CompanyProfile> company = session_.company().load();
  if (!company) {
    ui::showError(this, company.error());
    return;
  }
  const Result<QString> xml = buildAnnex4DfXml(report_, company.value());
  if (!xml) {
    ui::showError(this, xml.error());
    return;
  }
  const QString name = QStringLiteral("J0510411_%1_Q%2.xml").arg(report_.year).arg(report_.quarter);
  saveXmlFile(QStringLiteral("Зберегти додаток 4ДФ XML"), name, xml.value());
}

void ConsolidatedTaxPage::doExportD1()
{
  if (!ensureReport()) {
    return;
  }
  const Result<CompanyProfile> company = session_.company().load();
  if (!company) {
    ui::showError(this, company.error());
    return;
  }

  // Три місячні файли Д1 у вибрану теку (або послідовно).
  const QString dir = QFileDialog::getExistingDirectory(
      this, QStringLiteral("Тека для XML Д1 (три місяці кварталу)"));
  if (dir.isEmpty()) {
    return;
  }

  const int startMonth = report_.quarter * 3 - 2;
  for (int m = startMonth; m < startMonth + 3; ++m) {
    Result<ConsolidatedTaxReport> monthly =
        buildMonthlyPayrollTaxReport(session_.handle(), report_.year, m);
    if (!monthly) {
      ui::showError(this, monthly.error());
      return;
    }
    if (monthly.value().employees.empty()) {
      continue;
    }
    Result<QString> xml = buildAnnexEssD1Xml(monthly.value(), company.value());
    if (!xml) {
      ui::showError(this, xml.error());
      return;
    }
    const QString path =
        dir +
        QStringLiteral("/J0510111_%1_%2.xml").arg(report_.year).arg(m, 2, 10, QLatin1Char('0'));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
      ui::showError(this, Error::database(QStringLiteral("Не вдалося записати %1").arg(path)));
      return;
    }
    file.write(xml.value().toUtf8());
  }
}

void ConsolidatedTaxPage::doExportD5()
{
  if (!ensureReport()) {
    return;
  }
  const Result<CompanyProfile> company = session_.company().load();
  if (!company) {
    ui::showError(this, company.error());
    return;
  }
  const Result<QString> xml = buildAnnexD5Xml(report_, company.value());
  if (!xml) {
    ui::showError(this, xml.error());
    return;
  }
  const QString name = QStringLiteral("J0510511_%1_Q%2.xml").arg(report_.year).arg(report_.quarter);
  saveXmlFile(QStringLiteral("Зберегти додаток Д5 XML"), name, xml.value());
}

} // namespace fin
