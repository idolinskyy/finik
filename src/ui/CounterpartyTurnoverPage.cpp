#include "ui/CounterpartyTurnoverPage.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/domain/ChartCodes.h"
#include "fin/print/PrintForms.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

namespace fin {

CounterpartyTurnoverPage::CounterpartyTurnoverPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * bar = new QHBoxLayout;
  account_ = new QComboBox;
  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    ui::fillAccountCombo(account_, accounts.value());
    for (int i = 0; i < account_->count(); ++i) {
      const AccountId id{account_->itemData(i).toLongLong()};
      Result<Account> found = session_.accounts().findById(id);
      if (found && found.value().code == QLatin1String(chart::kCustomers)) {
        account_->setCurrentIndex(i);
        break;
      }
    }
  }
  from_ = new QDateEdit(QDate(QDate::currentDate().year(), 1, 1));
  to_ = new QDateEdit(QDate::currentDate());
  from_->setCalendarPopup(true);
  to_->setCalendarPopup(true);
  from_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  to_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  auto * build = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                 QStringLiteral("Сформувати"));
  auto * printBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                                    QStringLiteral("Друк / PDF"));
  bar->addWidget(new QLabel(QStringLiteral("Рахунок")));
  bar->addWidget(account_, 1);
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(build);
  bar->addWidget(printBtn);

  model_ = new CounterpartyTurnoverModel(this);
  view_ = new QTableView(this);
  view_->setModel(model_);
  view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->setSelectionMode(QAbstractItemView::SingleSelection);
  view_->verticalHeader()->setVisible(false);

  layout->addLayout(bar);
  layout->addWidget(view_);
  connect(build, &QPushButton::clicked, this, &CounterpartyTurnoverPage::rebuild);
  connect(printBtn, &QPushButton::clicked, this, &CounterpartyTurnoverPage::printReport);
}

void CounterpartyTurnoverPage::rebuild()
{
  const AccountId id = ui::selectedAccount(account_);
  if (!id) {
    ui::showError(this, QStringLiteral("оберіть рахунок"));
    return;
  }
  const Result<CounterpartyTurnover> report =
      session_.counterpartyTurnover().build(id, from_->date(), to_->date());
  if (!report) {
    ui::showError(this, report.error());
    return;
  }
  lastReport_ = report.value();
  hasReport_ = true;
  model_->setReport(lastReport_);
}

void CounterpartyTurnoverPage::printReport()
{
  if (!hasReport_) {
    rebuild();
    if (!hasReport_) {
      return;
    }
  }
  const Result<QString> html =
      PrintForms(session_.handle()).counterpartyTurnoverHtml(lastReport_);
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  ui::printHtml(this,
                QStringLiteral("Обороти %1").arg(lastReport_.accountCode), html.value());
}

} // namespace fin
