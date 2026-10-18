#include "ui/DashboardPage.h"

#include <QDate>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include "fin/core/Money.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/CurrencyRepository.h"
#include "fin/db/DocumentRepository.h"
#include "fin/db/LedgerRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/Document.h"
#include "fin/reports/Reports.h"
#include "ui/UiSupport.h"

#include <QLabel>

namespace fin {

DashboardPage::DashboardPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);

  auto * title = new QLabel(QStringLiteral("Головна"));
  QFont font = title->font();
  font.setPointSize(16);
  font.setBold(true);
  title->setFont(font);

  auto * topRow = new QHBoxLayout;

  auto * ratesBox = new QGroupBox(QStringLiteral("Курси НБУ (на сьогодні)"));
  auto * ratesLayout = new QVBoxLayout(ratesBox);
  usdLabel_ = new QLabel(QStringLiteral("USD: ..."));
  eurLabel_ = new QLabel(QStringLiteral("EUR: ..."));
  plnLabel_ = new QLabel(QStringLiteral("PLN: ..."));
  ratesLayout->addWidget(usdLabel_);
  ratesLayout->addWidget(eurLabel_);
  ratesLayout->addWidget(plnLabel_);

  auto * balanceBox = new QGroupBox(QStringLiteral("Залишки коштів"));
  auto * balanceLayout = new QVBoxLayout(balanceBox);
  cashLabel_ = new QLabel(QStringLiteral("Каса (301+302): ..."));
  bankLabel_ = new QLabel(QStringLiteral("Банк (311+312): ..."));
  cashFlowLabel_ = new QLabel(QStringLiteral("Рух коштів (місяць): ..."));
  marginLabel_ = new QLabel(QStringLiteral("Рентабельність (місяць): ..."));
  balanceLayout->addWidget(cashLabel_);
  balanceLayout->addWidget(bankLabel_);
  balanceLayout->addWidget(cashFlowLabel_);
  balanceLayout->addWidget(marginLabel_);

  topRow->addWidget(ratesBox, 1);
  topRow->addWidget(balanceBox, 1);

  auto * recentBox = new QGroupBox(QStringLiteral("Останні проведені документи"));
  auto * recentLayout = new QVBoxLayout(recentBox);
  recentTable_ = new QTableWidget(0, 3);
  recentTable_->horizontalHeader()->setVisible(false);
  recentTable_->verticalHeader()->setVisible(false);
  recentTable_->setSelectionMode(QAbstractItemView::NoSelection);
  recentTable_->setFocusPolicy(Qt::NoFocus);
  recentTable_->setShowGrid(false);
  recentTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  recentTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  recentTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  recentTable_->setStyleSheet(
      QStringLiteral("QTableWidget { border: none; background-color: "
                     "transparent; } QTableWidget::item { padding: 4px; }"));
  recentLayout->addWidget(recentTable_);

  layout->addWidget(title);
  layout->addLayout(topRow);
  layout->addWidget(recentBox, 1);
  layout->addStretch();

  reload();
}

void DashboardPage::reload()
{
  const QDate today = QDate::currentDate();

  // NBU Rates
  auto usd = session_.currencies().getRate(QStringLiteral("USD"), today);
  auto eur = session_.currencies().getRate(QStringLiteral("EUR"), today);
  auto pln = session_.currencies().getRate(QStringLiteral("PLN"), today);

  auto fmtRate = [](const Result<CurrencyRate> & r) {
    if (!r)
      return QStringLiteral("немає даних");
    return QStringLiteral("%1 UAH за %2")
        .arg(QString::number(static_cast<double>(r.value().rateMicros) / 1000000.0, 'f', 4))
        .arg(r.value().nominal);
  };

  usdLabel_->setText(QStringLiteral("USD: ") + fmtRate(usd));
  eurLabel_->setText(QStringLiteral("EUR: ") + fmtRate(eur));
  plnLabel_->setText(QStringLiteral("PLN: ") + fmtRate(pln));

  // Залишки коштів: як і у звіті «Рух коштів» — разом з валютними субрахунками
  const auto moneyBalance = [&](const char * uahCode, const char * fxCode) -> Result<Money> {
    Money total = Money::zero();
    for (const char * code : {uahCode, fxCode}) {
      const Result<Account> acc = session_.accounts().findByCode(QString::fromLatin1(code));
      if (!acc) {
        return Result<Money>::fail(acc.error());
      }
      const Result<Money> bal = session_.ledger().balance(acc.value().id, today);
      if (!bal) {
        return bal;
      }
      const Result<Money> sum = total.plus(bal.value());
      if (!sum) {
        return sum;
      }
      total = sum.value();
    }
    return Result<Money>::ok(total);
  };

  const auto moneyText = [](const QString & caption, const Result<Money> & value) {
    return value ? caption + value.value().toDisplay() + QStringLiteral(" грн")
                 : caption + QStringLiteral("—");
  };

  cashLabel_->setText(moneyText(QStringLiteral("Каса (301+302): "),
                                moneyBalance(chart::kCash, chart::kForeignCash)));
  bankLabel_->setText(moneyText(QStringLiteral("Банк (311+312): "),
                                moneyBalance(chart::kBank, chart::kForeignBank)));

  const QDate monthFrom(today.year(), today.month(), 1);
  CashFlowReport cf(session_.handle());
  const Result<CashFlowStatement> flow = cf.build(monthFrom, today);
  if (flow) {
    const Money net =
        Money::fromCents(flow.value().incoming.cents() - flow.value().outgoing.cents());
    cashFlowLabel_->setText(QStringLiteral("Рух коштів (місяць): %1 грн").arg(net.toDisplay()));
  } else {
    cashFlowLabel_->setText(QStringLiteral("Рух коштів (місяць): —"));
  }
  const Result<Profitability> margin = cf.profitability(monthFrom, today);
  if (margin) {
    const int bp = margin.value().marginBp;
    const bool neg = bp < 0;
    const int abs = neg ? -bp : bp;
    marginLabel_->setText(QStringLiteral("Рентабельність (місяць): %1%2,%3%")
                              .arg(neg ? QStringLiteral("-") : QString())
                              .arg(abs / 100)
                              .arg(abs % 100, 2, 10, QLatin1Char('0')));
  } else {
    marginLabel_->setText(QStringLiteral("Рентабельність (місяць): —"));
  }

  const Result<std::vector<DocumentHeader>> docs = session_.documents().listRecent(5);

  recentTable_->setRowCount(0);
  if (docs && !docs.value().empty()) {
    int count = 0;
    for (const auto & it : docs.value()) {
      if (it.posted) {
        recentTable_->insertRow(count);

        auto * typeItem =
            new QTableWidgetItem(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                 documentTypeLabelUk(it.type));

        auto * numItem = new QTableWidgetItem(
            QStringLiteral("№%1 від %2")
                .arg(it.number, it.date.toString(QStringLiteral("dd.MM.yyyy"))));
        numItem->setForeground(QColor("#555555"));

        auto * sumItem = new QTableWidgetItem(it.total.toDisplay() + QStringLiteral(" грн"));
        sumItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        QFont f = sumItem->font();
        f.setBold(true);
        sumItem->setFont(f);

        recentTable_->setItem(count, 0, typeItem);
        recentTable_->setItem(count, 1, numItem);
        recentTable_->setItem(count, 2, sumItem);

        count++;
      }
    }
    if (count == 0) {
      recentTable_->insertRow(0);
      recentTable_->setItem(0, 1,
                            new QTableWidgetItem(QStringLiteral("Немає проведених документів")));
    }
  } else {
    recentTable_->insertRow(0);
    recentTable_->setItem(0, 1, new QTableWidgetItem(QStringLiteral("Документи відсутні")));
  }
}

} // namespace fin
