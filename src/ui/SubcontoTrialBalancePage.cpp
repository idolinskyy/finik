#include "ui/SubcontoTrialBalancePage.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/db/AccountRepository.h"
#include "fin/db/SubcontoRepository.h"

#include <QComboBox>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

namespace fin {

SubcontoTrialBalancePage::SubcontoTrialBalancePage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * toolbar = new QHBoxLayout;

  toolbar->addWidget(new QLabel(QStringLiteral("Рахунок:")));
  accountCb_ = new QComboBox;
  toolbar->addWidget(accountCb_, 1);

  toolbar->addWidget(new QLabel(QStringLiteral("Субконто:")));
  lineCb_ = new QComboBox;
  toolbar->addWidget(lineCb_);

  kindHint_ = new QLabel;
  kindHint_->setStyleSheet(QStringLiteral("color: gray;"));
  toolbar->addWidget(kindHint_);

  toolbar->addWidget(new QLabel(QStringLiteral("Період з:")));
  from_ = new QDateEdit(QDate(QDate::currentDate().year(), 1, 1));
  from_->setCalendarPopup(true);
  from_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  toolbar->addWidget(from_);

  toolbar->addWidget(new QLabel(QStringLiteral("по:")));
  to_ = new QDateEdit(QDate::currentDate());
  to_->setCalendarPopup(true);
  to_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  toolbar->addWidget(to_);

  auto * buildBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                    QStringLiteral("Сформувати"));
  auto * exportBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                     QStringLiteral("Експорт CSV"));
  toolbar->addWidget(buildBtn);
  toolbar->addWidget(exportBtn);
  layout->addLayout(toolbar);

  model_ = new SubcontoTrialBalanceModel(this);
  view_ = new QTableView(this);
  view_->setModel(model_);
  view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int c = 1; c < SubcontoTrialBalanceModel::kColumns; ++c)
    view_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->verticalHeader()->setVisible(false);
  layout->addWidget(view_);

  reloadAccounts();

  connect(accountCb_, &QComboBox::currentIndexChanged, this, [this]() { reloadLineNos(); });
  connect(lineCb_, &QComboBox::currentIndexChanged, this,
          [this]() { kindHint_->setText(lineCb_->currentData(Qt::UserRole + 1).toString()); });
  connect(buildBtn, &QPushButton::clicked, this, &SubcontoTrialBalancePage::rebuild);
  connect(exportBtn, &QPushButton::clicked, this, [this]() {
    QString csv;
    for (int c = 0; c < model_->columnCount(); ++c) {
      if (c > 0)
        csv += QLatin1Char(';');
      csv += model_->headerData(c, Qt::Horizontal).toString();
    }
    csv += QLatin1Char('\n');
    for (int r = 0; r < model_->rowCount(); ++r) {
      QStringList cells;
      for (int c = 0; c < model_->columnCount(); ++c) {
        cells << model_->data(model_->index(r, c)).toString();
      }
      csv += cells.join(QLatin1Char(';')) + QLatin1Char('\n');
    }
    ui::saveCsv(this, QStringLiteral("osv-subconto.csv"), csv);
  });
}

void SubcontoTrialBalancePage::reloadAccounts()
{
  accountCb_->blockSignals(true);
  accountCb_->clear();
  AccountRepository repo(session_.handle());
  SubcontoRepository sub(session_.handle());
  const auto accountsRes = repo.listAll();
  if (accountsRes) {
    for (const auto & acc : accountsRes.value()) {
      if (acc.isFolder)
        continue;
      const auto settings = sub.listForAccount(acc.id);
      if (!settings || settings.value().empty())
        continue;
      accountCb_->addItem(acc.code + QStringLiteral(" — ") + acc.name,
                          QVariant::fromValue(static_cast<qulonglong>(acc.id.value)));
    }
  }
  accountCb_->blockSignals(false);
  reloadLineNos();
}

void SubcontoTrialBalancePage::reloadLineNos()
{
  lineCb_->blockSignals(true);
  lineCb_->clear();
  kindHint_->clear();
  if (accountCb_->currentIndex() < 0) {
    lineCb_->blockSignals(false);
    return;
  }
  const AccountId accountId{static_cast<std::int64_t>(accountCb_->currentData().toULongLong())};
  const auto settings = session_.subconto().listForAccount(accountId);
  if (settings) {
    for (const AccountSubconto & s : settings.value()) {
      const auto kind = session_.subconto().findKind(s.subcontoKindId);
      const QString label = QStringLiteral("%1 — %2").arg(s.lineNo).arg(kind ? kind.value().nameUa
                                                                             : s.subcontoKindId);
      lineCb_->addItem(label, s.lineNo);
      lineCb_->setItemData(lineCb_->count() - 1, kind ? kind.value().nameUa : s.subcontoKindId,
                           Qt::UserRole + 1);
    }
  }
  lineCb_->blockSignals(false);
  if (lineCb_->count() > 0) {
    lineCb_->setCurrentIndex(0);
    kindHint_->setText(lineCb_->currentData(Qt::UserRole + 1).toString());
  }
}

void SubcontoTrialBalancePage::setAccountAndPeriod(AccountId accountId, QDate from, QDate to,
                                                   int lineNo)
{
  reloadAccounts();
  const int index =
      accountCb_->findData(QVariant::fromValue(static_cast<qulonglong>(accountId.value)));
  if (index >= 0)
    accountCb_->setCurrentIndex(index);
  reloadLineNos();
  const int lineIndex = lineCb_->findData(lineNo);
  if (lineIndex >= 0)
    lineCb_->setCurrentIndex(lineIndex);
  from_->setDate(from);
  to_->setDate(to);
  rebuild();
}

void SubcontoTrialBalancePage::rebuild()
{
  if (accountCb_->currentIndex() < 0 || lineCb_->currentIndex() < 0) {
    model_->setReport({});
    return;
  }
  const AccountId accountId{static_cast<std::int64_t>(accountCb_->currentData().toULongLong())};
  const int lineNo = lineCb_->currentData().toInt();
  const Result<SubcontoTrialBalance> report =
      session_.subcontoTrialBalance().build(accountId, lineNo, from_->date(), to_->date());
  if (!report) {
    ui::showError(this, report.error());
    return;
  }
  model_->setReport(report.value());
  view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
}

} // namespace fin
