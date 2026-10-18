#include "ui/SearchDialog.h"

#include "ui/HelpBrowser.h"
#include "ui/UiSupport.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

namespace fin {

SearchDialog::SearchDialog(LedgerSession & session, QWidget * parent)
    : QDialog(parent), session_(session)
{
  setWindowTitle(QStringLiteral("Пошук"));
  resize(720, 480);
  setWhatsThis(QStringLiteral("Шукає документи, контрагентів, номенклатуру, "
                              "рахунки, ОЗ, НМА, МШП і працівників."));

  query_ = new QLineEdit;
  query_->setPlaceholderText(QStringLiteral("Номер, назва, ЄДРПОУ, артикул, інв. номер…"));
  query_->setClearButtonEnabled(true);
  results_ = new QListWidget;
  results_->setSelectionMode(QAbstractItemView::SingleSelection);
  status_ = new QLabel(QStringLiteral("Введіть щонайменше 2 символи."));
  status_->setWordWrap(true);
  debounce_ = new QTimer(this);
  debounce_->setSingleShot(true);
  debounce_->setInterval(180);

  auto * open = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                QStringLiteral("Відкрити"));
  auto * help = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/search.svg")),
                                QStringLiteral("Довідка"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  buttons->addWidget(help);
  buttons->addStretch();
  buttons->addWidget(open);
  buttons->addWidget(close);

  auto * layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(QStringLiteral("Знайти (Ctrl+F)")));
  layout->addWidget(query_);
  layout->addWidget(results_, 1);
  layout->addWidget(status_);
  layout->addLayout(buttons);

  connect(query_, &QLineEdit::textChanged, this, &SearchDialog::scheduleSearch);
  connect(query_, &QLineEdit::returnPressed, this, [this]() {
    debounce_->stop();
    runSearch();
    if (results_->count() > 0) {
      acceptCurrent();
    }
  });
  connect(debounce_, &QTimer::timeout, this, &SearchDialog::runSearch);
  connect(results_, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem *) { acceptCurrent(); });
  connect(open, &QPushButton::clicked, this, &SearchDialog::acceptCurrent);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  connect(help, &QPushButton::clicked, this, &SearchDialog::openHelp);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &SearchDialog::openHelp);
  query_->setFocus();
}

void SearchDialog::prime(const QString & query)
{
  query_->setText(query);
  debounce_->stop();
  runSearch();
}

void SearchDialog::scheduleSearch()
{
  debounce_->start();
}

void SearchDialog::runSearch()
{
  results_->clear();
  selected_ = {};
  const QString text = query_->text().trimmed();
  if (text.size() < 2) {
    status_->setText(QStringLiteral("Введіть щонайменше 2 символи."));
    return;
  }
  const Result<std::vector<SearchHit>> hits = GlobalSearch(session_.handle()).find(text);
  if (!hits) {
    status_->setText(hits.error().message);
    return;
  }
  if (hits.value().empty()) {
    status_->setText(QStringLiteral("Нічого не знайдено."));
    return;
  }
  int index = 0;
  for (const SearchHit & hit : hits.value()) {
    QString line = QStringLiteral("%1  ·  %2").arg(searchKindLabelUk(hit.kind), hit.title);
    if (!hit.subtitle.trimmed().isEmpty()) {
      line += QStringLiteral("  —  ") + hit.subtitle;
    }
    auto * item = new QListWidgetItem(line);
    item->setData(Qt::UserRole, index);
    item->setData(Qt::UserRole + 1, static_cast<qlonglong>(hit.objectId));
    item->setData(Qt::UserRole + 2, static_cast<int>(hit.kind));
    item->setData(Qt::UserRole + 3, hit.accountIsFolder);
    item->setToolTip(hit.subtitle);
    results_->addItem(item);
    ++index;
  }
  results_->setCurrentRow(0);
  status_->setText(QStringLiteral("Знайдено: %1").arg(results_->count()));
}

void SearchDialog::acceptCurrent()
{
  QListWidgetItem * item = results_->currentItem();
  if (item == nullptr) {
    return;
  }
  selected_.kind = static_cast<SearchKind>(item->data(Qt::UserRole + 2).toInt());
  selected_.objectId = item->data(Qt::UserRole + 1).toLongLong();
  selected_.accountIsFolder = item->data(Qt::UserRole + 3).toBool();
  selected_.title = item->text();
  accept();
}

void SearchDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("search.html"));
}

} // namespace fin
