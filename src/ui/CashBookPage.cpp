#include "ui/CashBookPage.h"

#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/bank/MonobankClient.h"
#include "fin/bank/PaymentExport.h"
#include "fin/bank/StatementImport.h"
#include "fin/domain/ChartCodes.h"
#include "fin/print/PrintForms.h"
#include "ui/DocumentDialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDateEdit>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIODevice>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace fin {
namespace {

QTableWidgetItem * moneyItem(Money value, bool bold)
{
  auto * item = new QTableWidgetItem(value.isZero() ? QString() : value.toDisplay());
  item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  if (bold) {
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
  }
  return item;
}

} // namespace

CashBookPage::CashBookPage(LedgerSession & session, const char * accountCode, const QString & title,
                           QWidget * parent)
    : QWidget(parent), session_(session), accountCode_(accountCode), title_(title)
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
  auto * print = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                                 QStringLiteral("Друк / PDF / CSV"));
  bar->addWidget(new QLabel(QStringLiteral("Період")));
  bar->addWidget(from_);
  bar->addWidget(to_);
  bar->addWidget(build);
  bar->addWidget(print);
  if (QLatin1String(accountCode_) == QLatin1String(chart::kCash)) {
    auto * journal = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                     QStringLiteral("Журнал КО-3"));
    bar->addWidget(journal);
    connect(journal, &QPushButton::clicked, this, &CashBookPage::exportJournal);
  }
  if (QLatin1String(accountCode_) == QLatin1String(chart::kBank)) {
    importBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                 QStringLiteral("Імпорт виписки"));
    bar->addWidget(importBtn_);
    connect(importBtn_, &QPushButton::clicked, this, &CashBookPage::importStatement);
    exportPaymentsBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                                         QStringLiteral("Експорт у клієнт-банк"));
    bar->addWidget(exportPaymentsBtn_);
    connect(exportPaymentsBtn_, &QPushButton::clicked, this, &CashBookPage::exportPayments);
    monobankBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                   QStringLiteral("Завантажити з Monobank"));
    bar->addWidget(monobankBtn_);
    connect(monobankBtn_, &QPushButton::clicked, this, &CashBookPage::fetchFromMonobank);
    const Status importOk = Access::requireImportBank(session_.handle());
    importBtn_->setEnabled(bool(importOk));
    importBtn_->setToolTip(importOk ? QString() : importOk.message());
    exportPaymentsBtn_->setEnabled(bool(importOk));
    exportPaymentsBtn_->setToolTip(importOk ? QString() : importOk.message());
    monobankBtn_->setEnabled(bool(importOk));
    monobankBtn_->setToolTip(importOk ? QString() : importOk.message());
  }
  bar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(6);
  table_->setHorizontalHeaderLabels({QStringLiteral("Дата"), QStringLiteral("Номер"),
                                     QStringLiteral("Документ"), QStringLiteral("Надійшло"),
                                     QStringLiteral("Вибуло"), QStringLiteral("Залишок")});
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  layout->addLayout(bar);
  layout->addWidget(table_);
  connect(build, &QPushButton::clicked, this, &CashBookPage::rebuild);
  connect(print, &QPushButton::clicked, this, &CashBookPage::exportBook);

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

  rebuild();
}

void CashBookPage::rebuild()
{
  if (from_->date() > to_->date()) {
    ui::showError(this, QStringLiteral("дата початку не може бути пізніше дати кінця"));
    return;
  }
  const Result<CashBook> book =
      session_.cashBook().buildFor(accountCode_, from_->date(), to_->date());
  if (!book) {
    ui::showError(this, book.error());
    return;
  }
  table_->setRowCount(static_cast<int>(book.value().rows.size()) + 2);
  auto * openLabel = new QTableWidgetItem(QStringLiteral("Залишок на початок"));
  QFont bold = openLabel->font();
  bold.setBold(true);
  openLabel->setFont(bold);
  table_->setItem(0, 0, openLabel);
  table_->setItem(0, 5, moneyItem(book.value().opening, true));
  int row = 1;
  for (const CashBookRow & line : book.value().rows) {
    auto * dateItem = new QTableWidgetItem(line.date.toString(QStringLiteral("dd.MM.yyyy")));
    if (line.documentId) {
      dateItem->setData(Qt::UserRole, QVariant::fromValue(line.documentId.value));
    }
    table_->setItem(row, 0, dateItem);
    table_->setItem(row, 1, new QTableWidgetItem(line.number));
    auto * typeItem = new QTableWidgetItem(line.typeLabel);
    if (line.dayTotal) {
      typeItem->setFont(bold);
    }
    table_->setItem(row, 2, typeItem);
    table_->setItem(row, 3, moneyItem(line.incoming, line.dayTotal));
    table_->setItem(row, 4, moneyItem(line.outgoing, line.dayTotal));
    table_->setItem(row, 5, moneyItem(line.balance, line.dayTotal));
    ++row;
  }
  auto * closeLabel = new QTableWidgetItem(QStringLiteral("Разом / залишок"));
  closeLabel->setFont(bold);
  table_->setItem(row, 0, closeLabel);
  table_->setItem(row, 3, moneyItem(book.value().incoming, true));
  table_->setItem(row, 4, moneyItem(book.value().outgoing, true));
  table_->setItem(row, 5, moneyItem(book.value().closing, true));
}

void CashBookPage::exportBook()
{
  if (from_->date() > to_->date()) {
    ui::showError(this, QStringLiteral("дата початку не може бути пізніше дати кінця"));
    return;
  }
  const Result<CashBook> book =
      session_.cashBook().buildFor(accountCode_, from_->date(), to_->date());
  if (!book) {
    ui::showError(this, book.error());
    return;
  }
  PrintForms forms(session_.handle());
  const Result<QString> html = forms.cashBookHtml(book.value(), title_);
  const Result<QString> csv = forms.cashBookCsv(book.value());
  if (!html || !csv) {
    ui::showError(this, html ? csv.error() : html.error());
    return;
  }
  ui::exportReport(this, title_, QStringLiteral("vypyska.pdf"), html.value(),
                   QStringLiteral("vypyska.csv"), csv.value());
}

void CashBookPage::exportJournal()
{
  if (from_->date() > to_->date()) {
    ui::showError(this, QStringLiteral("дата початку не може бути пізніше дати кінця"));
    return;
  }
  const Result<CashJournal> journal =
      CashJournalReport(session_.handle()).build(from_->date(), to_->date());
  if (!journal) {
    ui::showError(this, journal.error());
    return;
  }
  PrintForms forms(session_.handle());
  const Result<QString> html = forms.cashJournalHtml(journal.value());
  const Result<QString> csv = forms.cashJournalCsv(journal.value());
  if (!html || !csv) {
    ui::showError(this, html ? csv.error() : html.error());
    return;
  }
  ui::exportReport(this, QStringLiteral("Журнал касових документів"), QStringLiteral("ko3.pdf"),
                   html.value(), QStringLiteral("ko3.csv"), csv.value());
}

void CashBookPage::importStatement()
{
  const QString path =
      QFileDialog::getOpenFileName(this, QStringLiteral("Імпорт банківської виписки"), QString(),
                                   QStringLiteral("Виписка (*.csv *.xml *.txt);;Усі файли (*)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    ui::showError(this, QStringLiteral("не вдалося відкрити файл виписки"));
    return;
  }
  const QString text = QString::fromUtf8(file.readAll());
  Result<StatementParse> parsed = parseBankStatement(text, path);
  if (!parsed) {
    ui::showError(this, parsed.error());
    return;
  }
  const Result<int> created = importBankStatement(session_, parsed.value());
  if (!created) {
    ui::showError(this, created.error());
    return;
  }
  int skipped = 0;
  QStringList reasons;
  for (const StatementRow & row : parsed.value().rows) {
    if (!row.error.isEmpty() || row.duplicate) {
      ++skipped;
      if (reasons.size() < 8) {
        const QString why = row.duplicate ? QStringLiteral("дубль") : row.error;
        reasons.push_back(
            QStringLiteral("%1 %2 — %3")
                .arg(row.date.toString(QStringLiteral("dd.MM.yyyy")), row.amount.toDisplay(), why));
      }
    }
  }
  QString textResult = QStringLiteral("Створено чернеток: %1.").arg(created.value());
  if (skipped > 0) {
    textResult += QStringLiteral("\nПропущено: %1.").arg(skipped);
    if (!reasons.isEmpty()) {
      textResult += QLatin1Char('\n') + reasons.join(QLatin1Char('\n'));
    }
  }
  QMessageBox::information(this, QStringLiteral("Імпорт виписки"), textResult);
  rebuild();
}

void CashBookPage::exportPayments()
{
  const Result<PaymentExportResult> exported =
      exportOutgoingPaymentsCsv(session_, from_->date(), to_->date());
  if (!exported) {
    ui::showError(this, exported.error());
    return;
  }
  const QString suggested =
      QStringLiteral("payments_%1_%2.csv")
          .arg(from_->date().toString(QStringLiteral("yyyyMMdd")),
               to_->date().toString(QStringLiteral("yyyyMMdd")));
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("Експорт платежів у клієнт-банк"), suggested,
      QStringLiteral("CSV клієнт-банк (*.csv);;Усі файли (*)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    ui::showError(this, QStringLiteral("не вдалося зберегти файл платежів"));
    return;
  }
  if (file.write(exported.value().csv.toUtf8()) < 0) {
    ui::showError(this, QStringLiteral("помилка запису файлу платежів"));
    return;
  }
  QString textResult =
      QStringLiteral("Експортовано платежів: %1.").arg(exported.value().exported);
  if (!exported.value().skipped.empty()) {
    textResult += QStringLiteral("\nПропущено: %1.").arg(exported.value().skipped.size());
    QStringList reasons;
    for (const PaymentExportSkip & skip : exported.value().skipped) {
      if (reasons.size() >= 8) {
        break;
      }
      reasons.push_back(QStringLiteral("%1 %2 — %3")
                            .arg(skip.date.toString(QStringLiteral("dd.MM.yyyy")), skip.number,
                                 skip.reason));
    }
    if (!reasons.isEmpty()) {
      textResult += QLatin1Char('\n') + reasons.join(QLatin1Char('\n'));
    }
  }
  QMessageBox::information(this, QStringLiteral("Експорт у клієнт-банк"), textResult);
}

void CashBookPage::fetchFromMonobank()
{
  const Result<CompanyProfile> company = session_.company().load();
  if (!company) {
    ui::showError(this, company.error());
    return;
  }
  const QString token = company.value().monobankToken;
  if (token.trimmed().isEmpty()) {
    ui::showError(this, Error::validation(QStringLiteral(
                            "Не вказано токен доступу до Monobank в картці підприємства.")));
    return;
  }

  QApplication::setOverrideCursor(Qt::WaitCursor);

  MonobankClient client;
  Result<StatementParse> parsed = client.fetchStatementSync(token, from_->date(), to_->date());

  QApplication::restoreOverrideCursor();

  if (!parsed) {
    ui::showError(this, parsed.error());
    return;
  }

  if (parsed.value().rows.empty()) {
    QMessageBox::information(this, QStringLiteral("Monobank"),
                             QStringLiteral("За вибраний період операцій не знайдено."));
    return;
  }

  const Result<int> created = importBankStatement(session_, parsed.value());
  if (!created) {
    ui::showError(this, created.error());
    return;
  }
  int skipped = 0;
  QStringList reasons;
  for (const StatementRow & row : parsed.value().rows) {
    if (!row.error.isEmpty() || row.duplicate) {
      ++skipped;
      if (reasons.size() < 8) {
        const QString why = row.duplicate ? QStringLiteral("дубль") : row.error;
        reasons.push_back(
            QStringLiteral("%1 %2 — %3")
                .arg(row.date.toString(QStringLiteral("dd.MM.yyyy")), row.amount.toDisplay(), why));
      }
    }
  }
  QString textResult = QStringLiteral("Створено чернеток: %1.").arg(created.value());
  if (skipped > 0) {
    textResult += QStringLiteral("\nПропущено: %1.").arg(skipped);
    if (!reasons.isEmpty()) {
      textResult += QLatin1Char('\n') + reasons.join(QLatin1Char('\n'));
    }
  }
  QMessageBox::information(this, QStringLiteral("Monobank"), textResult);
  rebuild();
}

} // namespace fin
