#include "ui/AccountCardPage.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/domain/Account.h"
#include "fin/domain/ChartCodes.h"
#include "fin/print/PrintForms.h"
#include "ui/DocumentDialog.h"

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

AccountCardPage::AccountCardPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * bar = new QHBoxLayout;

  account_ = new QComboBox;
  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    ui::fillAccountCombo(account_, accounts.value());
    // Встановлюємо початково рахунок 301 (Каса)
    for (int i = 0; i < account_->count(); ++i) {
      const AccountId id{account_->itemData(i).toLongLong()};
      Result<Account> found = session_.accounts().findById(id);
      if (found && found.value().code == QLatin1String(chart::kCash)) {
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
  auto * exp = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                               QStringLiteral("Друк / PDF / CSV"));

  bar->addWidget(new QLabel(QStringLiteral("Рахунок")));
  bar->addWidget(account_, 1);
  counterparty_ = new QComboBox;
  ui::fillCounterpartyCombo(counterparty_, session_, {}, true);
  bar->addWidget(new QLabel(QStringLiteral("Контрагент")));
  bar->addWidget(counterparty_, 1);
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(build);
  bar->addWidget(exp);

  model_ = new AccountCardModel(this);
  view_ = new QTableView(this);
  view_->setModel(model_);
  view_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  view_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  for (int c = 3; c < AccountCardModel::kColumns; ++c)
    view_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->verticalHeader()->setVisible(false);

  layout->addLayout(bar);
  layout->addWidget(view_);

  connect(build, &QPushButton::clicked, this, &AccountCardPage::rebuild);
  connect(exp, &QPushButton::clicked, this, [this]() {
    const AccountId id = ui::selectedAccount(account_);
    if (!id) {
      ui::showError(this, QStringLiteral("оберіть рахунок"));
      return;
    }
    const Result<AccountCard> card = session_.accountCard().build(
        id, from_->date(), to_->date(), ui::selectedCounterparty(counterparty_));
    if (!card) {
      ui::showError(this, card.error());
      return;
    }
    PrintForms forms(session_.handle());
    const Result<QString> html = forms.accountCardHtml(card.value());
    const Result<QString> csv = forms.accountCardCsv(card.value());
    if (!html || !csv) {
      ui::showError(this, html ? csv.error() : html.error());
      return;
    }
    ui::exportReport(this, QStringLiteral("Картка рахунку"), QStringLiteral("kartka.pdf"),
                     html.value(), QStringLiteral("kartka.csv"), csv.value());
  });

  connect(view_, &QTableView::doubleClicked, this, [this](const QModelIndex & idx) {
    const DocumentId id = model_->documentIdAt(idx.row());
    if (id) {
      if (DocumentDialog::editExisting(session_, id, this)) {
        rebuild();
      }
    }
  });
}

void AccountCardPage::setAccountAndPeriod(AccountId id, QDate from, QDate to)
{
  showAccount(id);
  from_->setDate(from);
  to_->setDate(to);
  rebuild();
}

void AccountCardPage::rebuild()
{
  const AccountId id = ui::selectedAccount(account_);
  if (!id) {
    ui::showError(this, QStringLiteral("оберіть рахунок"));
    return;
  }

  const Result<AccountCard> card = session_.accountCard().build(
      id, from_->date(), to_->date(), ui::selectedCounterparty(counterparty_));
  if (!card) {
    ui::showError(this, card.error());
    return;
  }

  model_->setReport(card.value());
}

void AccountCardPage::showAccount(AccountId id)
{
  if (!id)
    return;
  for (int i = 0; i < account_->count(); ++i) {
    if (account_->itemData(i).toLongLong() == id.value) {
      account_->setCurrentIndex(i);
      rebuild();
      return;
    }
  }
}

} // namespace fin
