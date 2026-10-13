#include "support/Session.h"

#include "fin/db/Database.h"
#include "ui/HelpBrowser.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QRegularExpression>
#include <QSet>
#include <QSqlQuery>

using namespace fin;
using namespace fin::test;

namespace {

QString helpRoot()
{
  return QDir(QString::fromUtf8(FIN_SOURCE_DIR)).filePath(QStringLiteral("help"));
}

QString readFile(const QString & path)
{
  QFile file(path);
  REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
  return QString::fromUtf8(file.readAll());
}

} // namespace

TEST_CASE("schema is at the current version")
{
  LedgerSession session = openSession();
  QSqlQuery query(session.handle());
  REQUIRE(query.exec(QStringLiteral("SELECT MAX(version) FROM schema_migrations")));
  REQUIRE(query.next());
  REQUIRE(query.value(0).toInt() == Database::kSchemaVersion);
}

TEST_CASE("help pages in qrc exist, link to each other, and name the product")
{
  const QString root = helpRoot();
  const QString qrc = readFile(QDir(root).filePath(QStringLiteral("help.qrc")));
  QRegularExpression fileRe(QStringLiteral("<file>([^<]+)</file>"));
  auto it = fileRe.globalMatch(qrc);
  QStringList files;
  while (it.hasNext()) {
    files.push_back(it.next().captured(1));
  }
  REQUIRE(files.contains(QStringLiteral("index.html")));
  REQUIRE(files.contains(QStringLiteral("goods.html")));
  REQUIRE(files.contains(QStringLiteral("tax.html")));
  REQUIRE(files.contains(QStringLiteral("security.html")));
  REQUIRE(files.contains(QStringLiteral("assets.html")));
  REQUIRE(files.contains(QStringLiteral("intangibles.html")));
  REQUIRE(files.contains(QStringLiteral("low-value.html")));
  REQUIRE(files.contains(QStringLiteral("search.html")));
  REQUIRE(files.contains(QStringLiteral("edo.html")));
  REQUIRE(files.contains(QStringLiteral("payroll.html")));
  REQUIRE(files.contains(QStringLiteral("settings.html")));
  REQUIRE(files.contains(QStringLiteral("shots/journal.png")));
  REQUIRE(files.contains(QStringLiteral("shots/search.png")));
  REQUIRE(files.contains(QStringLiteral("shots/sale.png")));
  REQUIRE(files.contains(QStringLiteral("shots/vat.png")));
  REQUIRE(files.contains(QStringLiteral("shots/dashboard.png")));
  REQUIRE(files.contains(QStringLiteral("shots/subconto-osv.png")));
  REQUIRE(files.contains(QStringLiteral("shots/consolidated-tax.png")));
  REQUIRE(files.contains(QStringLiteral("shots/kit-assembly.png")));
  REQUIRE(files.contains(QStringLiteral("shots/bankbook.png")));
  QSet<QString> present;
  for (const QString & name : files) {
    const QString path = QDir(root).filePath(name);
    REQUIRE(QFile::exists(path));
    present.insert(name);
  }

  QRegularExpression hrefRe(QStringLiteral("(?:href|src)=\"([^\"]+)\""));
  for (const QString & name : files) {
    if (!name.endsWith(QStringLiteral(".html"))) {
      continue;
    }
    const QString html = readFile(QDir(root).filePath(name));
    auto links = hrefRe.globalMatch(html);
    while (links.hasNext()) {
      const QString href = links.next().captured(1);
      if (href.startsWith(QStringLiteral("http")) || href.startsWith(QLatin1Char('#'))) {
        continue;
      }
      REQUIRE(present.contains(href));
    }
  }

  const struct
  {
    const char * file;
    const char * needle;
  } required[] = {
      {"index.html", "Демо-дані"},
      {"index.html", "Баланс"},
      {"index.html", "Фінік"},
      {"index.html", "logo.png"},
      {"index.html", "Ctrl+F"},
      {"index.html", "payroll.html"},
      {"index.html", "edo.html"},
      {"index.html", "shots/journal.png"},
      {"from-1c.html", "Рахунок на оплату"},
      {"from-1c.html", "Надходження послуг"},
      {"from-1c.html", "Основні засоби"},
      {"from-1c.html", "Нематеріальні активи"},
      {"from-1c.html", "Податкова накладна"},
      {"from-1c.html", "зарплати"},
      {"from-1c.html", "M.E.doc"},
      {"from-1c.html", "захист від створення дублікатів"},
      {"currency.html", "десятковий рядок"},
      {"journal.html", "703"},
      {"journal.html", "На підставі"},
      {"journal.html", "Податкова накладна"},
      {"journal.html", "Нарахування зарплати"},
      {"goods.html", "Дт 361 Кт 703"},
      {"goods.html", "Дт 92/152/154 Кт 631"},
      {"goods.html", "не проводиться"},
      {"goods.html", "shots/sale.png"},
      {"goods.html", "shots/service-purchase.png"},
      {"goods.html", "shots/invoice.png"},
      {"cash.html", "КО-1"},
      {"cash.html", "КО-4"},
      {"cash.html", "shots/cash.png"},
      {"bank.html", "кореспонденцію"},
      {"bank.html", "ЄДРПОУ"},
      {"bank.html", "shots/bank.png"},
      {"bank.html", "163"},
      {"posting.html", "Рахунок на оплату"},
      {"posting.html", "Головній книзі"},
      {"posting.html", "введенням залишків"},
      {"posting.html", "бухгалтер"},
      {"posting.html", "Договір"},
      {"from-1c.html", "Договір» і «Працівник"},
      {"period.html", "703"},
      {"period.html", "661"},
      {"period.html", "shots/period.png"},
      {"period.html", "643"},
      {"reports.html", "ОСВ"},
      {"reports.html", "shots/osv.png"},
      {"reports.html", "shots/subconto-osv.png"},
      {"reports.html", "shots/cashflow.png"},
      {"reports.html", "Рух коштів"},
      {"reports.html", "рентабельність"},
      {"reports.html", "301/302"},
      {"reports.html", "311/312"},
      {"reports.html", "714"},
      {"reports.html", "944"},
      {"dashboard.html", "301 + 302"},
      {"dashboard.html", "311 + 312"},
      {"company.html", "C_STI_ORIG"},
      {"reports.html", "shots/vat.png"},
      {"reports.html", "shots/counterparty-turnovers.png"},
      {"reports.html", "281"},
      {"dashboard.html", "shots/dashboard.png"},
      {"goods.html", "shots/kit-assembly.png"},
      {"bank.html", "shots/bankbook.png"},
      {"bank.html", "клієнт-банк"},
      {"tax.html", "shots/consolidated-tax.png"},
      {"tax.html", "Д5"},
      {"settlements.html", "681"},
      {"settlements.html", "звірки"},
      {"settlements.html", "shots/reconciliation.png"},
      {"settlements.html", "371"},
      {"assets.html", "104"},
      {"assets.html", "131"},
      {"assets.html", "НП(С)БО 7"},
      {"assets.html", "shots/assets.png"},
      {"assets.html", "shots/fa-commission.png"},
      {"assets.html", "shots/fa-write-off.png"},
      {"assets.html", "shots/fa-depreciation.png"},
      {"assets.html", "списання"},
      {"intangibles.html", "125"},
      {"intangibles.html", "133"},
      {"intangibles.html", "НП(С)БО 8"},
      {"intangibles.html", "shots/intangibles.png"},
      {"intangibles.html", "shots/ia-commission.png"},
      {"intangibles.html", "shots/ia-write-off.png"},
      {"intangibles.html", "shots/ia-depreciation.png"},
      {"intangibles.html", "списання"},
      {"low-value.html", "22"},
      {"low-value.html", "Дт 92 Кт 22"},
      {"low-value.html", "shots/low-value.png"},
      {"low-value.html", "shots/ms-receipt.png"},
      {"low-value.html", "shots/ms-issue.png"},
      {"company.html", "єдиного податку"},
      {"company.html", "Платник ПДВ"},
      {"company.html", "shots/company.png"},
      {"company.html", "IBAN"},
      {"company.html", "МФО"},
      {"tax.html", "701"},
      {"tax.html", "643"},
      {"tax.html", "Перша подія"},
      {"tax.html", "shots/incomebook.png"},
      {"tax.html", "shots/vat.png"},
      {"tax.html", "shots/tax-invoice.png"},
      {"edo.html", "M.E.doc"},
      {"edo.html", "КЕП"},
      {"edo.html", "SHA-256"},
      {"edo.html", "ЄРПН"},
      {"edo.html", "вбудованої перевірки XSD"},
      {"payroll.html", "661"},
      {"payroll.html", "ПДФО"},
      {"payroll.html", "ПСП"},
      {"payroll.html", "П-6"},
      {"payroll.html", "shots/employees.png"},
      {"payroll.html", "shots/payroll.png"},
      {"payroll.html", "651"},
      {"payroll.html", "645"},
      {"payroll.html", "підрозділ"},
      {"payroll.html", "Відпускні"},
      {"payroll.html", "поза 2026 роком блокується"},
      {"journal.html", "Звіт виробництва"},
      {"journal.html", "Вимога-накладна"},
      {"reports.html", "Обороти по контрагенту"},
      {"reports.html", "переповнення int64"},
      {"reports.html", "Друк / PDF"},
      {"production.html", "специфікацією"},
      {"goods.html", "Договори"},
      {"goods.html", "Статуси замовлення"},
      {"production.html", "специфікацію"},
      {"security.html", "Пароль"},
      {"security.html", "Касир"},
      {"security.html", "субконто"},
      {"security.html", "shots/users.png"},
      {"security.html", "shots/about.png"},
      {"security.html", "shots/password.png"},
      {"security.html", "хешування"},
      {"search.html", "Ctrl+F"},
      {"search.html", "shots/search.png"},
      {"search.html", "працівник"},
      {"index.html", "Про Qt"},
      {"index.html", "одне робоче місце"},
      {"from-1c.html", "одне робоче місце"},
      {"security.html", "не клієнт"},
      {"security.html", "finik-secrets.key"},
      {"backup.html", "finik-secrets.key"},
      {"settings.html", "одночасний запис"},
      {"backup.html", "одночасний запис"},
      {"edo.html", "не файл для подачі"},
      {"tax.html", "не замінює подачу"},
      {"backup.html", ".local/share/Finik/Finik"},
      {"backup.html", "finik.sqlite.before-restore"},
      {"settings.html", ".local/share/Finik/Finik"},
      {"settings.html", "Налаштування"},
  };
  for (const auto & row : required) {
    const QString html = readFile(QDir(root).filePath(QString::fromLatin1(row.file)));
    REQUIRE(html.contains(QString::fromUtf8(row.needle)));
  }
}

TEST_CASE("help sidebar lists every page and keeps the interface Ukrainian")
{
  const QString root = helpRoot();
  const QStringList topics = HelpBrowser::topicFiles();
  REQUIRE_FALSE(topics.isEmpty());

  const QStringList pages =
      QDir(root).entryList({QStringLiteral("*.html")}, QDir::Files, QDir::Name);
  REQUIRE_FALSE(pages.isEmpty());
  for (const QString & page : pages) {
    INFO(page.toStdString());
    REQUIRE(topics.contains(page));
    REQUIRE(QFile::exists(QDir(root).filePath(page)));
  }
  for (const QString & topic : topics) {
    INFO(topic.toStdString());
    REQUIRE(pages.contains(topic));
  }

  // Заголовки довідки — українською, без англійських дублів
  const char * banned[] = {"Dashboard", "Expense Report"};
  for (const QString & page : pages) {
    const QString html = readFile(QDir(root).filePath(page));
    for (const char * word : banned) {
      INFO(page.toStdString() + " / " + word);
      REQUIRE_FALSE(html.contains(QString::fromLatin1(word)));
    }
  }
}

TEST_CASE("F1 topicForNavRow matches MainWindow nav ids")
{
  REQUIRE(HelpBrowser::topicForNavRow(0) == QStringLiteral("posting.html"));
  REQUIRE(HelpBrowser::topicForNavRow(3) == QStringLiteral("production.html"));
  REQUIRE(HelpBrowser::topicForNavRow(5) == QStringLiteral("assets.html"));
  REQUIRE(HelpBrowser::topicForNavRow(8) == QStringLiteral("payroll.html"));
  REQUIRE(HelpBrowser::topicForNavRow(10) == QStringLiteral("journal.html"));
  REQUIRE(HelpBrowser::topicForNavRow(15) == QStringLiteral("cash.html"));
  REQUIRE(HelpBrowser::topicForNavRow(18) == QStringLiteral("tax.html"));
  REQUIRE(HelpBrowser::topicForNavRow(23) == QStringLiteral("dashboard.html"));
  REQUIRE(HelpBrowser::topicForNavRow(26) == QStringLiteral("goods.html"));
  REQUIRE(HelpBrowser::topicForNavRow(27) == QStringLiteral("reports.html"));
  REQUIRE(HelpBrowser::topicForNavRow(28) == QStringLiteral("reports.html"));
  REQUIRE(HelpBrowser::topicForDocument(DocumentType::MaterialIssue) ==
          QStringLiteral("production.html"));
  REQUIRE(HelpBrowser::topicForDocument(DocumentType::ExpenseReport) ==
          QStringLiteral("expense-report.html"));
}
