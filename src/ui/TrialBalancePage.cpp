#include "ui/TrialBalancePage.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/print/PrintForms.h"

#include <QAction>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

namespace fin {

TrialBalancePage::TrialBalancePage(LedgerSession & session, QWidget * parent)
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
                               QStringLiteral("Друк / PDF / CSV"));

  bar->addWidget(new QLabel(QStringLiteral("Період")));
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(build);
  bar->addWidget(exp);
  bar->addStretch();

  model_ = new TrialBalanceModel(this);
  view_ = new QTableView(this);
  view_->setModel(model_);
  view_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  for (int c = 2; c < TrialBalanceModel::kColumns; ++c)
    view_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->verticalHeader()->setVisible(false);
  view_->setContextMenuPolicy(Qt::CustomContextMenu);

  layout->addLayout(bar);
  layout->addWidget(view_);

  connect(view_, &QWidget::customContextMenuRequested, this, &TrialBalancePage::showContextMenu);
  connect(build, &QPushButton::clicked, this, &TrialBalancePage::rebuild);
  connect(exp, &QPushButton::clicked, this, [this]() {
    const Result<TrialBalance> report = session_.trialBalance().build(from_->date(), to_->date());
    if (!report) {
      ui::showError(this, report.error());
      return;
    }
    PrintForms forms(session_.handle());
    const Result<QString> html = forms.trialBalanceHtml(report.value());
    const Result<QString> csv = forms.trialBalanceCsv(report.value());
    if (!html || !csv) {
      ui::showError(this, html ? csv.error() : html.error());
      return;
    }
    ui::exportReport(this, QStringLiteral("ОСВ"), QStringLiteral("osv.pdf"), html.value(),
                     QStringLiteral("osv.csv"), csv.value());
  });

  rebuild();
}

void TrialBalancePage::showContextMenu(const QPoint & pos)
{
  const QModelIndex index = view_->indexAt(pos);
  if (!index.isValid())
    return;

  // Підсумковий рядок не є рахунком — контекстне меню неактуальне
  const AccountId accId = model_->accountIdAt(index.row());
  if (!accId)
    return;

  // Перевіряємо, що рахунок не є папкою
  const Result<Account> found = session_.accounts().findById(accId);
  if (!found || found.value().isFolder)
    return;

  const QDate from = from_->date();
  const QDate to = to_->date();

  QMenu menu(this);
  QAction * cardAction = menu.addAction(QStringLiteral("Картка рахунку"));
  QAction * analysisAction = menu.addAction(QStringLiteral("Аналіз рахунку"));
  QAction * subcontoAction = nullptr;
  const auto settings = session_.subconto().listForAccount(accId);
  if (settings && !settings.value().empty()) {
    subcontoAction = menu.addAction(QStringLiteral("ОСВ по субконто"));
  }

  QAction * selected = menu.exec(view_->viewport()->mapToGlobal(pos));
  if (selected == cardAction) {
    emit openAccountCard(accId, from, to);
  } else if (selected == analysisAction) {
    emit openAccountAnalysis(accId, from, to);
  } else if (subcontoAction != nullptr && selected == subcontoAction) {
    emit openSubcontoTrialBalance(accId, from, to);
  }
}

void TrialBalancePage::rebuild()
{
  const Result<TrialBalance> report = session_.trialBalance().build(from_->date(), to_->date());
  if (!report) {
    ui::showError(this, report.error());
    return;
  }
  model_->setReport(report.value());
}

} // namespace fin
