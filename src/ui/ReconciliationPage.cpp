#include "ui/ReconciliationPage.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/domain/ChartCodes.h"
#include "fin/print/PrintForms.h"
#include "ui/DocumentDialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLatin1String>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {
namespace {

QTableWidgetItem * moneyItem(Money value)
{
  auto * item = new QTableWidgetItem(value.isZero() ? QString() : value.toDisplay());
  item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  return item;
}

} // namespace

ReconciliationPage::ReconciliationPage(LedgerSession & session, QWidget * parent)
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
  counterparty_ = new QComboBox;
  ui::fillCounterpartyCombo(counterparty_, session_, {}, false);
  from_ = new QDateEdit(QDate(QDate::currentDate().year(), 1, 1));
  to_ = new QDateEdit(QDate::currentDate());
  from_->setCalendarPopup(true);
  to_->setCalendarPopup(true);
  from_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  to_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  auto * build = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                 QStringLiteral("Сформувати"));
  auto * print = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                                 QStringLiteral("Друк / PDF"));
  bar->addWidget(new QLabel(QStringLiteral("Рахунок")));
  bar->addWidget(account_, 1);
  bar->addWidget(new QLabel(QStringLiteral("Контрагент")));
  bar->addWidget(counterparty_, 1);
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(build);
  bar->addWidget(print);

  table_ = new QTableWidget;
  table_->setColumnCount(5);
  table_->setHorizontalHeaderLabels({QStringLiteral("Дата"), QStringLiteral("Документ"),
                                     QStringLiteral("Зміст"), QStringLiteral("Дебет"),
                                     QStringLiteral("Кредит")});
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  layout->addLayout(bar);
  layout->addWidget(new QLabel(
      QStringLiteral("Акт звірки — рух по рахунку в розрізі контрагента. Для покупця типово "
                     "361, для одержаного авансу 681, для постачальника 631, для виданого "
                     "авансу 371.")));
  layout->addWidget(table_);
  connect(build, &QPushButton::clicked, this, &ReconciliationPage::rebuild);
  connect(print, &QPushButton::clicked, this, &ReconciliationPage::printAct);

  connect(table_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem * item) {
    const QVariant docData = table_->item(item->row(), 0)->data(Qt::UserRole);
    if (docData.isValid() && docData.toLongLong() > 0) {
      fin::DocumentId id;
      id.value = docData.toLongLong();
      if (DocumentDialog::editExisting(session_, id, this)) {
        rebuild();
      }
    }
  });
}

void ReconciliationPage::rebuild()
{
  {
    const QSignalBlocker blocker(counterparty_);
    const CounterpartyId current = ui::selectedCounterparty(counterparty_);
    ui::fillCounterpartyCombo(counterparty_, session_, current, false);
  }
  const AccountId accountId = ui::selectedAccount(account_);
  const CounterpartyId party = ui::selectedCounterparty(counterparty_);
  if (!accountId) {
    ui::showError(this, QStringLiteral("оберіть рахунок"));
    return;
  }
  const Result<ReconciliationAct> act =
      ReconciliationReport(session_.handle()).build(accountId, party, from_->date(), to_->date());
  if (!act) {
    ui::showError(this, act.error());
    return;
  }
  table_->setRowCount(static_cast<int>(act.value().rows.size()) + 3);
  int row = 0;
  table_->setItem(row, 0, new QTableWidgetItem());
  table_->setItem(row, 1, new QTableWidgetItem());
  table_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("Сальдо на початок")));
  table_->setItem(row, 3, moneyItem(act.value().openingDebit));
  table_->setItem(row, 4, moneyItem(act.value().openingCredit));
  ++row;
  for (const AccountCardRow & line : act.value().rows) {
    auto * dateItem = new QTableWidgetItem(line.date.toString(QStringLiteral("dd.MM.yyyy")));
    if (line.documentId) {
      dateItem->setData(Qt::UserRole, QVariant::fromValue(line.documentId.value));
    }
    table_->setItem(row, 0, dateItem);
    table_->setItem(
        row, 1,
        new QTableWidgetItem(line.documentTypeLabel + QLatin1Char(' ') + line.documentNumber));
    table_->setItem(row, 2, new QTableWidgetItem(line.description));
    table_->setItem(row, 3, moneyItem(line.debit));
    table_->setItem(row, 4, moneyItem(line.credit));
    ++row;
  }
  auto * turn = new QTableWidgetItem(QStringLiteral("Обороти"));
  QFont bold = turn->font();
  bold.setBold(true);
  turn->setFont(bold);
  table_->setItem(row, 0, new QTableWidgetItem());
  table_->setItem(row, 1, new QTableWidgetItem());
  table_->setItem(row, 2, turn);
  table_->setItem(row, 3, moneyItem(act.value().turnoverDebit));
  table_->setItem(row, 4, moneyItem(act.value().turnoverCredit));
  ++row;
  auto * close = new QTableWidgetItem(QStringLiteral("Сальдо на кінець"));
  close->setFont(bold);
  table_->setItem(row, 0, new QTableWidgetItem());
  table_->setItem(row, 1, new QTableWidgetItem());
  table_->setItem(row, 2, close);
  table_->setItem(row, 3, moneyItem(act.value().closingDebit));
  table_->setItem(row, 4, moneyItem(act.value().closingCredit));
}

void ReconciliationPage::printAct()
{
  const AccountId accountId = ui::selectedAccount(account_);
  const CounterpartyId party = ui::selectedCounterparty(counterparty_);
  const Result<ReconciliationAct> act =
      ReconciliationReport(session_.handle()).build(accountId, party, from_->date(), to_->date());
  if (!act) {
    ui::showError(this, act.error());
    return;
  }
  const Result<QString> html = PrintForms(session_.handle()).reconciliationHtml(act.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  ui::exportReport(this, QStringLiteral("Акт звірки"), QStringLiteral("akt-zvirky.pdf"),
                   html.value(), QString(), QString());
}

} // namespace fin
