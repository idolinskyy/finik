#include "ui/HelpBrowser.h"

#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"
#include "fin/domain/FixedAsset.h"
#include "fin/domain/IntangibleAsset.h"
#include "fin/domain/LowValueItem.h"
#include "fin/domain/Vat.h"

#include <iterator>

#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTextBrowser>
#include <QUrl>

namespace fin {
namespace {

struct Topic
{
  const char * file;
  const char * title;
};

constexpr Topic kTopics[] = {
    {"index.html", "З чого почати"},
    {"dashboard.html", "Стартова сторінка"},
    {"from-1c.html", "Якщо ви з 1С"},
    {"journal.html", "Журнал документів"},
    {"goods.html", "Надходження і реалізація"},
    {"production.html", "Виробництво і специфікації"},
    {"expense-report.html", "Авансові звіти"},
    {"cash.html", "Каса, ПКО, ВКО і касова книга"},
    {"bank.html", "Банк і виписка 311"},
    {"currency.html", "Валюти та курси"},
    {"posting.html", "Проведення"},
    {"period.html", "Закритий період"},
    {"reports.html", "ОСВ, баланс, склад"},
    {"settlements.html", "Аванси і акт звірки"},
    {"assets.html", "Основні засоби"},
    {"intangibles.html", "Нематеріальні активи"},
    {"low-value.html", "МШП"},
    {"payroll.html", "Зарплата"},
    {"company.html", "Картка підприємства"},
    {"tax.html", "Податки: книга доходів і ПДВ"},
    {"edo.html", "ЕДО / XML податкових накладних"},
    {"prro.html", "ПРРО (Checkbox)"},
    {"security.html", "Користувачі і журнал змін"},
    {"settings.html", "Налаштування програми"},
    {"backup.html", "Резервна копія і відновлення"},
    {"search.html", "Глобальний пошук"},
};

} // namespace

QStringList HelpBrowser::topicFiles()
{
  QStringList files;
  files.reserve(static_cast<int>(std::size(kTopics)));
  for (const Topic & topic : kTopics) {
    files.push_back(QString::fromLatin1(topic.file));
  }
  return files;
}

HelpBrowser::HelpBrowser(QWidget * parent) : QDialog(parent)
{
  setWindowTitle(QStringLiteral("Довідка — Фінік"));
  setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
  resize(960, 640);

  topics_ = new QListWidget;
  topics_->setFixedWidth(240);
  for (const Topic & topic : kTopics) {
    auto * item = new QListWidgetItem(QString::fromUtf8(topic.title));
    item->setData(Qt::UserRole, QString::fromLatin1(topic.file));
    topics_->addItem(item);
  }

  browser_ = new QTextBrowser;
  browser_->setOpenExternalLinks(true);
  browser_->setSearchPaths({QStringLiteral(":/help")});

  auto * layout = new QHBoxLayout(this);
  layout->addWidget(topics_);
  layout->addWidget(browser_, 1);

  connect(topics_, &QListWidget::currentRowChanged, this, [this](int) { loadSelected(); });
  topics_->setCurrentRow(0);
}

void HelpBrowser::openTopic(QWidget * parent, const QString & page)
{
  auto * dialog = new HelpBrowser(parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(false);
  dialog->showPage(page);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

QString HelpBrowser::topicForDocument(DocumentType type)
{
  if (type == DocumentType::PeriodResult) {
    return QStringLiteral("period.html");
  }
  if (usesFaForm(type)) {
    return QStringLiteral("assets.html");
  }
  if (usesIaForm(type)) {
    return QStringLiteral("intangibles.html");
  }
  if (usesMsForm(type)) {
    return QStringLiteral("low-value.html");
  }
  if (usesGoodsForm(type) || usesServicePurchaseForm(type) || type == DocumentType::Sale ||
      type == DocumentType::Purchase) {
    if (type == DocumentType::MaterialIssue || type == DocumentType::ProductionReport) {
      return QStringLiteral("production.html");
    }
    return QStringLiteral("goods.html");
  }
  if (usesExpenseReportForm(type)) {
    return QStringLiteral("expense-report.html");
  }
  if (usesCashForm(type)) {
    return QStringLiteral("cash.html");
  }
  if (usesBankForm(type)) {
    return QStringLiteral("bank.html");
  }
  if (usesOpeningForm(type)) {
    return QStringLiteral("journal.html");
  }
  if (usesTaxInvoiceForm(type)) {
    return QStringLiteral("tax.html");
  }
  if (isPayrollFamily(type)) {
    return QStringLiteral("payroll.html");
  }
  return QStringLiteral("journal.html");
}

QString HelpBrowser::topicForNavRow(int row)
{
  // Ідентифікатори збігаються з UserRole пунктів навігації MainWindow.
  switch (row) {
  case 0: // План рахунків
    return QStringLiteral("posting.html");
  case 1: // Контрагенти
    return QStringLiteral("settlements.html");
  case 26: // Договори
    return QStringLiteral("goods.html");
  case 2: // Номенклатура
  case 4: // Склади
    return QStringLiteral("goods.html");
  case 3: // Специфікації (BOM)
    return QStringLiteral("production.html");
  case 5: // Основні засоби
    return QStringLiteral("assets.html");
  case 6: // Нематеріальні активи
    return QStringLiteral("intangibles.html");
  case 7: // МШП
    return QStringLiteral("low-value.html");
  case 8:  // Працівники
  case 21: // Табель (П-5)
    return QStringLiteral("payroll.html");
  case 9: // Валюти та курси
    return QStringLiteral("currency.html");
  case 10: // Журнал документів
  case 25: // Майстер залишків
    return QStringLiteral("journal.html");
  case 11: // ОСВ
  case 12: // Картка рахунку
  case 13: // Обороти з контрагентами
  case 14: // Залишки товарів
  case 17: // Баланс і фінрезультат
  case 24: // Аналіз рахунку
  case 27: // ОСВ по субконто
  case 28: // Рух коштів
    return QStringLiteral("reports.html");
  case 15: // Касова книга
    return QStringLiteral("cash.html");
  case 16: // Виписка банку
    return QStringLiteral("bank.html");
  case 18: // Книга доходів
  case 19: // ПДВ
    return QStringLiteral("tax.html");
  case 20: // Акт звірки
    return QStringLiteral("settlements.html");
  case 22: // Об'єднана звітність
    return QStringLiteral("edo.html");
  case 23: // Dashboard
    return QStringLiteral("dashboard.html");
  default:
    return QStringLiteral("index.html");
  }
}

void HelpBrowser::showPage(const QString & page)
{
  for (int i = 0; i < topics_->count(); ++i) {
    if (topics_->item(i)->data(Qt::UserRole).toString() == page) {
      topics_->setCurrentRow(i);
      break;
    }
  }
  browser_->setSource(QUrl(QStringLiteral("qrc:/help/%1").arg(page)));
}

void HelpBrowser::loadSelected()
{
  QListWidgetItem * item = topics_->currentItem();
  if (item == nullptr) {
    return;
  }
  browser_->setSource(
      QUrl(QStringLiteral("qrc:/help/%1").arg(item->data(Qt::UserRole).toString())));
}

} // namespace fin
