#include "ui/PeriodDialog.h"

#include "ui/DocumentDialog.h"
#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/MonthClose.h"

#include <QComboBox>
#include <QDate>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace fin {

PeriodDialog::PeriodDialog(LedgerSession & session, QWidget * parent)
    : QDialog(parent), session_(session)
{
  setWindowTitle(QStringLiteral("Закритий період"));
  setModal(true);
  resize(420, 180);

  status_ = new QLabel;
  status_->setWordWrap(true);
  year_ = new QSpinBox;
  year_->setRange(1991, 2100);
  year_->setValue(QDate::currentDate().year());
  month_ = new QComboBox;
  const QString months[] = {
      QStringLiteral("січень"),  QStringLiteral("лютий"),    QStringLiteral("березень"),
      QStringLiteral("квітень"), QStringLiteral("травень"),  QStringLiteral("червень"),
      QStringLiteral("липень"),  QStringLiteral("серпень"),  QStringLiteral("вересень"),
      QStringLiteral("жовтень"), QStringLiteral("листопад"), QStringLiteral("грудень")};
  int m = 0;
  for (const QString & name : months) {
    month_->addItem(name, ++m);
  }
  month_->setCurrentIndex(QDate::currentDate().month() - 1);

  closeBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/clock.svg")),
                              QStringLiteral("Закрити місяць"));
  resultBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/balance.svg")),
                               QStringLiteral("Фінрезультат"));
  reopenBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                               QStringLiteral("Відкрити останній місяць"));
  auto * done = new QPushButton(QStringLiteral("Закрити вікно"));

  auto * pick = new QHBoxLayout;
  pick->addWidget(new QLabel(QStringLiteral("Місяць")));
  pick->addWidget(month_, 1);
  pick->addWidget(year_);

  auto * buttons = new QHBoxLayout;
  buttons->addWidget(resultBtn_);
  buttons->addWidget(closeBtn_);
  buttons->addWidget(reopenBtn_);
  buttons->addStretch();
  buttons->addWidget(done);

  auto * layout = new QVBoxLayout(this);
  layout->addWidget(status_);
  layout->addLayout(pick);
  layout->addStretch();
  layout->addLayout(buttons);

  connect(closeBtn_, &QPushButton::clicked, this, &PeriodDialog::closeMonth);
  connect(resultBtn_, &QPushButton::clicked, this, &PeriodDialog::postResult);
  connect(reopenBtn_, &QPushButton::clicked, this, &PeriodDialog::reopenMonth);
  connect(year_, &QSpinBox::valueChanged, this, &PeriodDialog::refresh);
  connect(month_, &QComboBox::currentIndexChanged, this, &PeriodDialog::refresh);
  connect(done, &QPushButton::clicked, this, &QDialog::accept);
  refresh();
}

void PeriodDialog::refresh()
{
  const Result<QDate> closed = session_.period().closedThrough();
  if (!closed) {
    status_->setText(closed.message());
    closeBtn_->setEnabled(false);
    resultBtn_->setEnabled(false);
    reopenBtn_->setEnabled(false);
    return;
  }
  const Status access = Access::requireClosePeriod(session_.handle());
  const Status canClose =
      session_.period().checkCloseThroughMonth(year_->value(), month_->currentData().toInt());
  closeBtn_->setEnabled(bool(access) && bool(canClose));
  closeBtn_->setToolTip(!access ? access.message() : (canClose ? QString() : canClose.message()));
  resultBtn_->setEnabled(bool(access));
  resultBtn_->setToolTip(
      !access ? access.message()
              : QStringLiteral("Закриває 701, 71, 902, 92 через 79 на 44 за вибраний місяць"));
  reopenBtn_->setEnabled(bool(access) && closed.value().isValid());
  reopenBtn_->setToolTip(
      !access ? access.message()
              : (closed.value().isValid() ? QString() : QStringLiteral("немає закритого періоду")));
  QString text;
  if (!closed.value().isValid()) {
    text = QStringLiteral("Жоден місяць не закрито.");
  } else {
    text = QStringLiteral("Закрито по %1 включно.")
               .arg(closed.value().toString(QStringLiteral("dd.MM.yyyy")));
  }
  if (!access) {
    text += QLatin1Char('\n') + access.message();
  } else if (!canClose) {
    text += QLatin1Char('\n') + canClose.message();
  }
  status_->setText(text);
}

void PeriodDialog::postResult()
{
  const Result<Document> built =
      buildPeriodResultDocument(session_.handle(), year_->value(), month_->currentData().toInt());
  if (!built) {
    ui::showError(this, built.error());
    return;
  }
  if (DocumentDialog::openDocument(session_, built.value(), this)) {
    refresh();
  }
}

void PeriodDialog::closeMonth()
{
  const Status status =
      session_.period().closeThroughMonth(year_->value(), month_->currentData().toInt());
  if (!status) {
    ui::showError(this, status);
    return;
  }
  refresh();
}

void PeriodDialog::reopenMonth()
{
  const Status status = session_.period().reopenLastMonth();
  if (!status) {
    ui::showError(this, status);
    return;
  }
  refresh();
}

} // namespace fin
