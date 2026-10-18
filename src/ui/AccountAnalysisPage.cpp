#include "ui/AccountAnalysisPage.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/db/AccountRepository.h"
#include "fin/reports/Reports.h"

#include <QComboBox>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

namespace fin {

AccountAnalysisPage::AccountAnalysisPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  auto * toolbar = new QHBoxLayout;

  toolbar->addWidget(new QLabel(QStringLiteral("Рахунок:")));
  accountCb_ = new QComboBox;
  toolbar->addWidget(accountCb_);

  toolbar->addWidget(new QLabel(QStringLiteral("Період з:")));
  from_ = new QDateEdit(QDate::currentDate().addDays(-QDate::currentDate().day() + 1));
  from_->setCalendarPopup(true);
  toolbar->addWidget(from_);

  toolbar->addWidget(new QLabel(QStringLiteral("по:")));
  to_ = new QDateEdit(QDate::currentDate());
  to_->setCalendarPopup(true);
  toolbar->addWidget(to_);

  auto * buildBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                    QStringLiteral("Сформувати"));
  auto * exportBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                     QStringLiteral("Експорт CSV"));
  toolbar->addWidget(buildBtn);
  toolbar->addWidget(exportBtn);
  toolbar->addStretch();
  layout->addLayout(toolbar);

  model_ = new AccountAnalysisModel(this);
  view_ = new QTableView(this);
  view_->setModel(model_);
  view_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  view_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  view_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->verticalHeader()->setVisible(false);
  layout->addWidget(view_);

  // Заповнення списку рахунків (лише проводкові, без папок)
  AccountRepository repo(session_.handle());
  const auto accountsRes = repo.listAll();
  if (accountsRes) {
    for (const auto & acc : accountsRes.value()) {
      if (!acc.isFolder) {
        accountCb_->addItem(acc.code + QStringLiteral(" - ") + acc.name,
                            QVariant::fromValue(static_cast<qulonglong>(acc.id.value)));
      }
    }
  }

  connect(buildBtn, &QPushButton::clicked, this, &AccountAnalysisPage::rebuild);
  connect(exportBtn, &QPushButton::clicked, this, [this]() {
    // Простий CSV-експорт через модель
    QString csv;
    for (int r = 0; r < model_->rowCount(); ++r) {
      QStringList cells;
      for (int c = 0; c < model_->columnCount(); ++c) {
        cells << model_->data(model_->index(r, c)).toString();
      }
      csv += cells.join(QLatin1Char(';')) + QLatin1Char('\n');
    }
    ui::saveCsv(this, QStringLiteral("analiz.csv"), csv);
  });
}

void AccountAnalysisPage::setAccountAndPeriod(AccountId accountId, QDate from, QDate to)
{
  const int index =
      accountCb_->findData(QVariant::fromValue(static_cast<qulonglong>(accountId.value)));
  if (index >= 0)
    accountCb_->setCurrentIndex(index);
  from_->setDate(from);
  to_->setDate(to);
  rebuild();
}

void AccountAnalysisPage::rebuild()
{
  if (accountCb_->currentIndex() < 0)
    return;

  const AccountId accountId{static_cast<int64_t>(accountCb_->currentData().toULongLong())};
  AccountAnalysisReport report(session_.handle());
  const auto res = report.build(accountId, from_->date(), to_->date());
  if (!res) {
    ui::showError(this, res.error());
    return;
  }
  model_->setReport(res.value());
}

} // namespace fin
