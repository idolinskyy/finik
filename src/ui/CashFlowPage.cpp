#include "ui/CashFlowPage.h"

#include "ui/UiSupport.h"

#include "fin/reports/Reports.h"

#include <QAbstractItemView>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {
namespace {

QString marginText(int bp)
{
  const bool neg = bp < 0;
  const int abs = neg ? -bp : bp;
  return QStringLiteral("%1%2,%3%")
      .arg(neg ? QStringLiteral("-") : QString())
      .arg(abs / 100)
      .arg(abs % 100, 2, 10, QLatin1Char('0'));
}

void putMoney(QTableWidget * table, int row, const QString & label, Money amount)
{
  table->setItem(row, 0, new QTableWidgetItem(label));
  auto * value = new QTableWidgetItem(amount.toDisplay());
  value->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  table->setItem(row, 1, value);
}

} // namespace

CashFlowPage::CashFlowPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * bar = new QHBoxLayout;
  from_ = new QDateEdit(QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1));
  to_ = new QDateEdit(QDate::currentDate());
  from_->setCalendarPopup(true);
  to_->setCalendarPopup(true);
  from_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  to_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  auto * build = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                 QStringLiteral("Сформувати"));
  bar->addWidget(new QLabel(QStringLiteral("Період")));
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(build);
  bar->addStretch();
  layout->addLayout(bar);

  summary_ = new QLabel;
  summary_->setWordWrap(true);
  layout->addWidget(summary_);

  layout->addWidget(new QLabel(QStringLiteral("Рентабельність (на основі фінрезультатів)")));
  margin_ = new QTableWidget;
  margin_->setColumnCount(2);
  margin_->setHorizontalHeaderLabels({QStringLiteral("Показник"), QStringLiteral("Значення")});
  margin_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  margin_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  margin_->verticalHeader()->setVisible(false);
  margin_->setMaximumHeight(160);
  layout->addWidget(margin_);

  layout->addWidget(new QLabel(QStringLiteral("Рух коштів за кореспонденцією (301/302 + 311/312)")));
  table_ = new QTableWidget;
  table_->setColumnCount(4);
  table_->setHorizontalHeaderLabels({QStringLiteral("Рахунок"), QStringLiteral("Назва"),
                                     QStringLiteral("Надходження"), QStringLiteral("Списання")});
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  layout->addWidget(table_, 1);

  connect(build, &QPushButton::clicked, this, &CashFlowPage::rebuild);
  rebuild();
}

void CashFlowPage::rebuild()
{
  CashFlowReport report(session_.handle());
  const Result<CashFlowStatement> cf = report.build(from_->date(), to_->date());
  if (!cf) {
    ui::showError(this, cf.error());
    return;
  }
  const Result<Profitability> profit = report.profitability(from_->date(), to_->date());
  if (!profit) {
    ui::showError(this, profit.error());
    return;
  }

  const CashFlowStatement & st = cf.value();
  const Money net = Money::fromCents(st.incoming.cents() - st.outgoing.cents());
  summary_->setText(QStringLiteral("Початковий залишок (каса 301/302 %1 + банк 311/312 %2) = %3. "
                                   "Надходження %4, списання %5, чистий рух %6. "
                                   "Кінцевий залишок (каса %7 + банк %8) = %9.")
                        .arg(st.openingCash.toDisplay(), st.openingBank.toDisplay(),
                             st.openingTotal.toDisplay(), st.incoming.toDisplay(),
                             st.outgoing.toDisplay(), net.toDisplay(), st.closingCash.toDisplay(),
                             st.closingBank.toDisplay(), st.closingTotal.toDisplay()));

  const Profitability & p = profit.value();
  margin_->setRowCount(4);
  putMoney(margin_, 0, QStringLiteral("Дохід (701+703+71+714)"), p.revenue);
  putMoney(margin_, 1, QStringLiteral("Витрати (902+92+944)"), p.costs);
  putMoney(margin_, 2, QStringLiteral("Фінансовий результат"), p.profit);
  margin_->setItem(3, 0, new QTableWidgetItem(QStringLiteral("Рентабельність (маржа)")));
  margin_->setItem(3, 1, new QTableWidgetItem(marginText(p.marginBp)));

  table_->setRowCount(static_cast<int>(st.byCorrespondence.size()));
  for (int i = 0; i < static_cast<int>(st.byCorrespondence.size()); ++i) {
    const CashFlowCorrRow & row = st.byCorrespondence[static_cast<size_t>(i)];
    table_->setItem(i, 0, new QTableWidgetItem(row.accountCode));
    table_->setItem(i, 1, new QTableWidgetItem(row.accountName));
    auto * in = new QTableWidgetItem(row.incoming.toDisplay());
    in->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(i, 2, in);
    auto * out = new QTableWidgetItem(row.outgoing.toDisplay());
    out->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(i, 3, out);
  }
}

} // namespace fin
