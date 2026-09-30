#include "ui/AccountCardPage.h"
#include "ui/AuthDialogs.h"
#include "ui/CashExpenseDialogs.h"
#include "ui/CompanyDialog.h"
#include "ui/ConsolidatedTaxPage.h"
#include "ui/FaDialogs.h"
#include "ui/HrOrderDialog.h"
#include "ui/IaDialogs.h"
#include "ui/MainWindow.h"
#include "ui/MsDialogs.h"
#include "ui/PayrollDialog.h"
#include "ui/PeriodDialog.h"
#include "ui/ReconciliationPage.h"
#include "ui/SalePurchaseDialogs.h"
#include "ui/SearchDialog.h"
#include "ui/SickLeaveDialog.h"
#include "ui/SubcontoTrialBalancePage.h"
#include "ui/TaxInvoiceDialog.h"
#include "ui/TypedDialogs.h"
#include "ui/VacationDialog.h"

#include "fin/db/UserRepository.h"
#include "fin/demo/DemoSeed.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/Document.h"
#include "fin/domain/User.h"
#include "fin/session/LedgerSession.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFont>
#include <QPixmap>
#include <QSettings>
#include <QSize>
#include <QStyle>
#include <QStyleFactory>
#include <QTextStream>
#include <QTreeWidget>
#include <QWidget>

namespace {

void flushUi()
{
  QApplication::processEvents();
  QApplication::sendPostedEvents(nullptr, 0);
}

bool saveShot(QWidget & widget, const QDir & dir, const QString & name)
{
  flushUi();
  widget.repaint();
  flushUi();
  QPixmap shot = widget.grab();
  if (shot.isNull()) {
    QTextStream(stderr) << QStringLiteral("empty grab: %1\n").arg(name);
    return false;
  }
  if (shot.width() > 1100) {
    shot = shot.scaledToWidth(1100, Qt::SmoothTransformation);
  }
  const QString path = dir.filePath(name);
  if (!shot.save(path, "PNG")) {
    QTextStream(stderr) << QStringLiteral("cannot write %1\n").arg(path);
    return false;
  }
  QTextStream(stdout) << path << QLatin1Char('\n');
  return true;
}

fin::Result<fin::Document> firstOf(fin::LedgerSession & session, fin::DocumentType type,
                                   bool preferPosted)
{
  const int year = QDate::currentDate().year();
  const fin::Result<std::vector<fin::DocumentHeader>> list =
      session.documents().list(QDate(year, 1, 1), QDate(year, 12, 31), type);
  if (!list) {
    return fin::Result<fin::Document>::fail(list.error());
  }
  const fin::Document * fallback = nullptr;
  fin::Document fallbackDoc;
  for (const fin::DocumentHeader & header : list.value()) {
    const fin::Result<fin::Document> document = session.documents().findById(header.id);
    if (!document) {
      continue;
    }
    if (preferPosted && header.posted) {
      return document;
    }
    if (!fallback) {
      fallbackDoc = document.value();
      fallback = &fallbackDoc;
    }
  }
  if (fallback != nullptr) {
    return fin::Result<fin::Document>::ok(*fallback);
  }
  return fin::Result<fin::Document>::fail(
      fin::Error::notFound(QStringLiteral("немає документа для знімка")));
}

fin::Document blankDoc(fin::DocumentType type, const QString & number)
{
  fin::Document document;
  document.type = type;
  document.number = number;
  document.date = QDate::currentDate();
  return document;
}

void selectAccountByCode(QWidget & root, const char * code)
{
  const QString needle = QString::fromLatin1(code);
  for (QComboBox * combo : root.findChildren<QComboBox *>()) {
    for (int i = 0; i < combo->count(); ++i) {
      if (combo->itemText(i).startsWith(needle)) {
        combo->setCurrentIndex(i);
        return;
      }
    }
  }
}

} // namespace

int main(int argc, char * argv[])
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }

  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("fin"));
  QCoreApplication::setApplicationName(QStringLiteral("fin-shots"));
  {
    QSettings settings;
    settings.setValue(QStringLiteral("ui/restore_tabs"), false);
    settings.setValue(QStringLiteral("ui/confirm_exit"), false);
    settings.setValue(QStringLiteral("security/suggest_backup"), false);
    settings.setValue(QStringLiteral("automation/sync_nbu"), false);
  }
  if (QStyle * fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
    QApplication::setStyle(fusion);
  }
  QFont font(QStringLiteral("Noto Sans"));
  if (font.family() != QStringLiteral("Noto Sans")) {
    font = QFont(QStringLiteral("DejaVu Sans"));
  }
  font.setPointSize(10);
  QApplication::setFont(font);

  const QString outPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("help/shots");
  QDir dir;
  if (!dir.mkpath(outPath) || !dir.cd(outPath)) {
    QTextStream(stderr) << QStringLiteral("cannot create %1\n").arg(outPath);
    return 1;
  }

  fin::Result<fin::LedgerSession> session = fin::LedgerSession::openMemory();
  if (!session) {
    QTextStream(stderr) << session.message() << QLatin1Char('\n');
    return 1;
  }
  const fin::Status seeded = fin::seedDemoData(session.value());
  if (!seeded) {
    QTextStream(stderr) << seeded.message() << QLatin1Char('\n');
    return 1;
  }

  fin::MainWindow window(session.value());
  window.show();
  window.resize(1280, 800);
  flushUi();

  bool ok = true;
  auto pageShot = [&](int navId, const QString & name) {
    window.selectNav(navId);
    flushUi();
    ok = saveShot(window, dir, name) && ok;
  };

  pageShot(23, QStringLiteral("dashboard.png"));
  pageShot(10, QStringLiteral("journal.png"));
  pageShot(0, QStringLiteral("accounts.png"));
  pageShot(1, QStringLiteral("counterparties.png"));
  pageShot(26, QStringLiteral("contracts.png"));
  pageShot(2, QStringLiteral("nomenclature.png"));
  pageShot(3, QStringLiteral("bom.png"));
  pageShot(4, QStringLiteral("warehouses.png"));
  pageShot(5, QStringLiteral("assets.png"));
  pageShot(6, QStringLiteral("intangibles.png"));
  pageShot(7, QStringLiteral("low-value.png"));
  pageShot(8, QStringLiteral("employees.png"));
  pageShot(9, QStringLiteral("currencies.png"));
  pageShot(11, QStringLiteral("osv.png"));

  window.selectNav(12);
  if (fin::AccountCardPage * card = window.findChild<fin::AccountCardPage *>()) {
    const fin::Result<fin::Account> cash =
        session.value().accounts().findByCode(QString::fromLatin1(fin::chart::kCash));
    if (cash) {
      card->showAccount(cash.value().id);
    } else {
      selectAccountByCode(*card, fin::chart::kCash);
      card->rebuild();
    }
  }
  flushUi();
  ok = saveShot(window, dir, QStringLiteral("card.png")) && ok;

  pageShot(13, QStringLiteral("counterparty-turnovers.png"));
  pageShot(14, QStringLiteral("stock.png"));
  pageShot(15, QStringLiteral("cashbook.png"));
  pageShot(16, QStringLiteral("bankbook.png"));
  pageShot(17, QStringLiteral("statements.png"));
  pageShot(28, QStringLiteral("cashflow.png"));
  pageShot(18, QStringLiteral("incomebook.png"));
  pageShot(19, QStringLiteral("vat.png"));
  pageShot(20, QStringLiteral("reconciliation.png"));
  pageShot(21, QStringLiteral("timesheet.png"));

  window.selectNav(22);
  if (fin::ConsolidatedTaxPage * tax = window.findChild<fin::ConsolidatedTaxPage *>()) {
    tax->rebuild();
  }
  flushUi();
  ok = saveShot(window, dir, QStringLiteral("consolidated-tax.png")) && ok;

  pageShot(24, QStringLiteral("account-analysis.png"));

  window.selectNav(27);
  if (fin::SubcontoTrialBalancePage * sub = window.findChild<fin::SubcontoTrialBalancePage *>()) {
    const fin::Result<fin::Account> customers =
        session.value().accounts().findByCode(QString::fromLatin1(fin::chart::kCustomers));
    if (customers) {
      sub->setAccountAndPeriod(customers.value().id, QDate(QDate::currentDate().year(), 1, 1),
                               QDate::currentDate(), 1);
    } else {
      selectAccountByCode(*sub, fin::chart::kCustomers);
      sub->rebuild();
    }
  }
  flushUi();
  ok = saveShot(window, dir, QStringLiteral("subconto-osv.png")) && ok;

  auto dialogFromDoc = [&](fin::DocumentType type, bool preferPosted, const QString & name,
                           auto open) {
    const fin::Result<fin::Document> document = firstOf(session.value(), type, preferPosted);
    if (!document) {
      QTextStream(stderr) << QStringLiteral("no document for %1\n").arg(name);
      ok = false;
      return;
    }
    open(document.value());
  };

  dialogFromDoc(fin::DocumentType::Sale, true, QStringLiteral("sale.png"),
                [&](const fin::Document & document) {
                  fin::GoodsDocumentDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("sale.png")) && ok;
                });
  dialogFromDoc(fin::DocumentType::CustomerInvoice, false, QStringLiteral("invoice.png"),
                [&](const fin::Document & document) {
                  fin::GoodsDocumentDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("invoice.png")) && ok;
                });
  dialogFromDoc(fin::DocumentType::ServicePurchase, true, QStringLiteral("service-purchase.png"),
                [&](const fin::Document & document) {
                  fin::ServicePurchaseDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("service-purchase.png")) && ok;
                });

  {
    fin::GoodsDocumentDialog dialog(
        session.value(), blankDoc(fin::DocumentType::KitAssembly, QStringLiteral("КМ-демо")));
    dialog.show();
    ok = saveShot(dialog, dir, QStringLiteral("kit-assembly.png")) && ok;
  }

  dialogFromDoc(fin::DocumentType::CashOut, true, QStringLiteral("cash.png"),
                [&](const fin::Document & document) {
                  fin::CashDocumentDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("cash.png")) && ok;
                });
  dialogFromDoc(fin::DocumentType::BankPayment, true, QStringLiteral("bank.png"),
                [&](const fin::Document & document) {
                  fin::BankDocumentDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("bank.png")) && ok;
                });

  dialogFromDoc(fin::DocumentType::FaCommission, true, QStringLiteral("fa-commission.png"),
                [&](const fin::Document & document) {
                  fin::FaCommissionDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("fa-commission.png")) && ok;
                });
  dialogFromDoc(fin::DocumentType::FaDepreciation, true, QStringLiteral("fa-depreciation.png"),
                [&](const fin::Document & document) {
                  fin::FaDepreciationDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("fa-depreciation.png")) && ok;
                });
  dialogFromDoc(fin::DocumentType::FaWriteOff, true, QStringLiteral("fa-write-off.png"),
                [&](const fin::Document & document) {
                  fin::FaWriteOffDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("fa-write-off.png")) && ok;
                });

  dialogFromDoc(fin::DocumentType::IaCommission, true, QStringLiteral("ia-commission.png"),
                [&](const fin::Document & document) {
                  fin::IaCommissionDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("ia-commission.png")) && ok;
                });
  dialogFromDoc(fin::DocumentType::IaDepreciation, true, QStringLiteral("ia-depreciation.png"),
                [&](const fin::Document & document) {
                  fin::IaDepreciationDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("ia-depreciation.png")) && ok;
                });
  dialogFromDoc(fin::DocumentType::IaWriteOff, true, QStringLiteral("ia-write-off.png"),
                [&](const fin::Document & document) {
                  fin::IaWriteOffDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("ia-write-off.png")) && ok;
                });

  dialogFromDoc(fin::DocumentType::MsReceipt, true, QStringLiteral("ms-receipt.png"),
                [&](const fin::Document & document) {
                  fin::MsDocumentDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("ms-receipt.png")) && ok;
                });
  dialogFromDoc(fin::DocumentType::MsIssue, true, QStringLiteral("ms-issue.png"),
                [&](const fin::Document & document) {
                  fin::MsDocumentDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("ms-issue.png")) && ok;
                });

  dialogFromDoc(fin::DocumentType::TaxInvoice, true, QStringLiteral("tax-invoice.png"),
                [&](const fin::Document & document) {
                  fin::TaxInvoiceDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("tax-invoice.png")) && ok;
                });
  dialogFromDoc(fin::DocumentType::Payroll, true, QStringLiteral("payroll.png"),
                [&](const fin::Document & document) {
                  fin::PayrollDialog dialog(session.value(), document);
                  dialog.show();
                  ok = saveShot(dialog, dir, QStringLiteral("payroll.png")) && ok;
                });

  {
    fin::HrOrderDialog dialog(session.value(),
                              blankDoc(fin::DocumentType::Manual, QStringLiteral("КН-демо")));
    dialog.show();
    ok = saveShot(dialog, dir, QStringLiteral("hr-order.png")) && ok;
  }
  {
    fin::VacationDialog dialog(
        session.value(), blankDoc(fin::DocumentType::VacationPayroll, QStringLiteral("ВІД-демо")));
    dialog.show();
    ok = saveShot(dialog, dir, QStringLiteral("vacation.png")) && ok;
  }
  {
    fin::SickLeaveDialog dialog(
        session.value(), blankDoc(fin::DocumentType::SickLeavePayroll, QStringLiteral("ЛК-демо")));
    dialog.show();
    ok = saveShot(dialog, dir, QStringLiteral("sick-leave.png")) && ok;
  }

  {
    fin::CompanyDialog company(session.value());
    company.show();
    ok = saveShot(company, dir, QStringLiteral("company.png")) && ok;
  }
  {
    fin::PeriodDialog period(session.value());
    period.show();
    ok = saveShot(period, dir, QStringLiteral("period.png")) && ok;
  }
  {
    fin::PasswordDialog password(session.value());
    password.show();
    ok = saveShot(password, dir, QStringLiteral("password.png")) && ok;
  }
  {
    fin::UserRepository users(session.value().handle());
    fin::User cashier;
    cashier.login = QStringLiteral("kasa");
    cashier.displayName = QStringLiteral("Касир");
    cashier.role = fin::UserRole::Cashier;
    (void)users.insert(cashier);
    fin::User viewer;
    viewer.login = QStringLiteral("oglyad");
    viewer.displayName = QStringLiteral("Перегляд");
    viewer.role = fin::UserRole::Viewer;
    (void)users.insert(viewer);
    fin::UsersDialog usersDialog(session.value());
    usersDialog.show();
    ok = saveShot(usersDialog, dir, QStringLiteral("users.png")) && ok;
  }
  {
    fin::AuditDialog audit(session.value());
    audit.show();
    ok = saveShot(audit, dir, QStringLiteral("audit.png")) && ok;
  }
  {
    fin::AboutDialog about(session.value());
    about.show();
    ok = saveShot(about, dir, QStringLiteral("about.png")) && ok;
  }
  {
    fin::SearchDialog search(session.value());
    search.prime(QStringLiteral("кава"));
    search.show();
    flushUi();
    QApplication::processEvents();
    ok = saveShot(search, dir, QStringLiteral("search.png")) && ok;
  }

  return ok ? 0 : 1;
}
