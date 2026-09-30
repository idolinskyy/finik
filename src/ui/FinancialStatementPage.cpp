#include "ui/FinancialStatementPage.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/edo/DeclarationXml.h"
#include "fin/print/PrintForms.h"
#include "fin/reports/Reports.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCursor>
#include <QDateEdit>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {
namespace {

void putMoney(QTableWidget * table, int row, const QString & label, Money amount)
{
  table->setItem(row, 0, new QTableWidgetItem(label));
  auto * value = new QTableWidgetItem(amount.toDisplay());
  value->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  table->setItem(row, 1, value);
}

} // namespace

FinancialStatementPage::FinancialStatementPage(LedgerSession & session, QWidget * parent)
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
                                  QStringLiteral("Експорт Балансу в XML (ДПС)"));
  bar->addWidget(new QLabel(QStringLiteral("Період")));
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(build);
  bar->addWidget(exp);
  bar->addWidget(expXml);
  bar->addStretch();

  income_ = new QTableWidget;
  income_->setColumnCount(2);
  income_->setHorizontalHeaderLabels({QStringLiteral("Стаття"), QStringLiteral("Сума")});
  income_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  income_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  income_->verticalHeader()->setVisible(false);

  balance_ = new QTableWidget;
  balance_->setColumnCount(3);
  balance_->setHorizontalHeaderLabels(
      {QStringLiteral("Розділ"), QStringLiteral("Рахунок"), QStringLiteral("Сума")});
  balance_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  balance_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  balance_->verticalHeader()->setVisible(false);

  layout->addLayout(bar);
  layout->addWidget(new QLabel(QStringLiteral("Звіт про фінансові результати")));
  layout->addWidget(income_);
  layout->addWidget(new QLabel(QStringLiteral("Баланс на дату «по»")));
  layout->addWidget(balance_);

  connect(build, &QPushButton::clicked, this, &FinancialStatementPage::rebuild);
  connect(exp, &QPushButton::clicked, this, [this]() {
    FinancialStatementReport report(session_.handle());
    const Result<IncomeStatement> income = report.income(from_->date(), to_->date());
    const Result<BalanceSheet> balance = report.balance(to_->date());
    if (!income || !balance) {
      ui::showError(this, income ? balance.error() : income.error());
      return;
    }
    PrintForms forms(session_.handle());
    const Result<QString> incomeHtml = forms.incomeStatementHtml(income.value());
    const Result<QString> balanceHtml = forms.balanceSheetHtml(balance.value());
    if (!incomeHtml || !balanceHtml) {
      ui::showError(this, incomeHtml ? balanceHtml.error() : incomeHtml.error());
      return;
    }
    QMenu menu(this);
    QAction * pl = menu.addAction(QStringLiteral("Фінрезультати…"));
    QAction * bs = menu.addAction(QStringLiteral("Баланс…"));
    QAction * chosen = menu.exec(QCursor::pos());
    if (chosen == pl) {
      ui::printHtml(this, QStringLiteral("Фінрезультати"), incomeHtml.value());
    } else if (chosen == bs) {
      ui::printHtml(this, QStringLiteral("Баланс"), balanceHtml.value());
    }
  });
  connect(expXml, &QPushButton::clicked, this, [this]() {
    FinancialStatementReport report(session_.handle());
    const Result<IncomeStatement> income = report.income(from_->date(), to_->date());
    const Result<BalanceSheet> balance = report.balance(to_->date());
    if (!income || !balance) {
      ui::showError(this, income ? balance.error() : income.error());
      return;
    }
    const Result<CompanyProfile> company = session_.company().load();
    if (!company) {
      ui::showError(this, company.error());
      return;
    }
    const Result<QString> xml = buildBalance1MXml(balance.value(), income.value(), company.value());
    if (!xml) {
      ui::showError(this, xml.error());
      return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Зберегти XML"),
                                                      QStringLiteral("J0901107.xml"),
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

void FinancialStatementPage::rebuild()
{
  FinancialStatementReport report(session_.handle());
  const Result<IncomeStatement> income = report.income(from_->date(), to_->date());
  if (!income) {
    ui::showError(this, income.error());
    return;
  }
  const IncomeStatement & pl = income.value();
  CashFlowReport cf(session_.handle());
  const Result<Profitability> margin = cf.profitability(from_->date(), to_->date());
  const int marginBp = margin ? margin.value().marginBp : 0;
  income_->setRowCount(7);
  putMoney(income_, 0, QStringLiteral("Дохід від реалізації товарів (701)"), pl.goodsRevenue);
  putMoney(income_, 1, QStringLiteral("Дохід від реалізації послуг (703)"), pl.serviceRevenue);
  putMoney(income_, 2, QStringLiteral("Інший операційний дохід (71+714)"), pl.otherIncome);
  putMoney(income_, 3, QStringLiteral("Собівартість реалізації (902)"), pl.cogs);
  putMoney(income_, 4, QStringLiteral("Адміністративні / курсові витрати (92+944)"), pl.admin);
  putMoney(income_, 5, QStringLiteral("Фінансовий результат"), pl.profit);
  {
    const bool neg = marginBp < 0;
    const int abs = neg ? -marginBp : marginBp;
    const QString text = QStringLiteral("%1%2,%3%")
                             .arg(neg ? QStringLiteral("-") : QString())
                             .arg(abs / 100)
                             .arg(abs % 100, 2, 10, QLatin1Char('0'));
    income_->setItem(6, 0, new QTableWidgetItem(QStringLiteral("Рентабельність (маржа)")));
    income_->setItem(6, 1, new QTableWidgetItem(text));
  }

  const Result<BalanceSheet> balance = report.balance(to_->date());
  if (!balance) {
    ui::showError(this, balance.error());
    return;
  }
  const BalanceSheet & sheet = balance.value();
  const int rows =
      static_cast<int>(sheet.assets.size() + sheet.equity.size() + sheet.liabilities.size() + 3);
  balance_->setRowCount(rows);
  int row = 0;
  auto addSection = [&](const QString & title, const std::vector<StatementLine> & items,
                        Money total) {
    auto * head = new QTableWidgetItem(title);
    QFont font = head->font();
    font.setBold(true);
    head->setFont(font);
    balance_->setItem(row, 0, head);
    balance_->setItem(row, 1, new QTableWidgetItem);
    auto * sum = new QTableWidgetItem(total.toDisplay());
    sum->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sum->setFont(font);
    balance_->setItem(row, 2, sum);
    ++row;
    for (const StatementLine & item : items) {
      balance_->setItem(row, 0, new QTableWidgetItem(item.name));
      balance_->setItem(row, 1, new QTableWidgetItem(item.code));
      auto * amount = new QTableWidgetItem(item.amount.toDisplay());
      amount->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      balance_->setItem(row, 2, amount);
      ++row;
    }
  };
  addSection(QStringLiteral("Актив"), sheet.assets, sheet.assetsTotal);
  addSection(QStringLiteral("Власний капітал"), sheet.equity, sheet.equityTotal);
  addSection(QStringLiteral("Зобов'язання"), sheet.liabilities, sheet.liabilitiesTotal);
}

} // namespace fin
