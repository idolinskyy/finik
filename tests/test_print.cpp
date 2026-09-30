#include "support/Session.h"

#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"
#include "fin/print/AmountInWords.h"
#include "fin/print/PrintForms.h"
#include "fin/reports/Reports.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include <QRegularExpression>

using namespace fin;
using namespace fin::test;

TEST_CASE("Ukrainian money in words")
{
  const Result<QString> one = moneyToUkrainianWords(Money::fromCents(100));
  REQUIRE(one);
  REQUIRE(one.value().contains(QString::fromUtf8("гривня")));
  REQUIRE(one.value().startsWith(QString::fromUtf8("Одна")));

  const Result<QString> two = moneyToUkrainianWords(Money::fromCents(200));
  REQUIRE(two);
  REQUIRE(two.value().contains(QString::fromUtf8("гривні")));

  const Result<QString> five = moneyToUkrainianWords(Money::fromCents(500));
  REQUIRE(five);
  REQUIRE(five.value().contains(QString::fromUtf8("гривень")));

  const Result<QString> kop = moneyToUkrainianWords(Money::fromCents(101));
  REQUIRE(kop);
  REQUIRE(kop.value().contains(QString::fromUtf8("01 копійка")));

  const Result<QString> thousand = moneyToUkrainianWords(Money::fromCents(100000));
  REQUIRE(thousand);
  REQUIRE(thousand.value().contains(QString::fromUtf8("тисяча")));

  REQUIRE_FALSE(moneyToUkrainianWords(Money::fromCents(-1)));
}

TEST_CASE("cash receipt and delivery note html contain number and amount")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId sugar = item(session, QStringLiteral("Цукор"));
  const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Друкар"));

  Document cash = makeDoc(DocumentType::CashIn, QDate(2026, 9, 10), QStringLiteral("ПКО-print"));
  cash.counterpartyId = buyer;
  CashLine cashRow;
  cashRow.correspondenceId = account(session, chart::kCustomers);
  cashRow.amount = Money::fromCents(12345);
  cashRow.description = QStringLiteral("оплата рахунку");
  const Result<std::vector<DocumentLine>> cashLines = composeCashIn(accounts.value(), {cashRow});
  REQUIRE(cashLines);
  cash.lines = cashLines.value();
  saveAndPost(session, cash);

  PrintForms forms(session.handle());
  const Result<QString> pko = forms.cashReceiptHtml(cash);
  REQUIRE(pko);
  REQUIRE(pko.value().contains(QStringLiteral("ПКО-print")));
  REQUIRE(pko.value().contains(QStringLiteral("Прибутковий касовий ордер")));
  REQUIRE(pko.value().contains(QStringLiteral("ТОВ Друкар")));
  REQUIRE(pko.value().contains(QStringLiteral("Підприємство")));
  REQUIRE(pko.value().contains(htmlEscape(Money::fromCents(12345).toDisplay())));
  REQUIRE(pko.value().contains(QStringLiteral("content=\"landscape\"")));
  REQUIRE(pko.value().contains(QStringLiteral("width=\"100%\"")));
  REQUIRE(pko.value().contains(QStringLiteral("КО-1")));
  REQUIRE(pko.value().contains(QStringLiteral("Квитанція до ПКО")));

  Document cashOut =
      makeDoc(DocumentType::CashOut, QDate(2026, 9, 12), QStringLiteral("ВКО-print"));
  cashOut.counterpartyId = buyer;
  cashOut.receivedBy = QStringLiteral("Іваненко І.І.");
  cashOut.identityDoc = QStringLiteral("паспорт АА 123456");
  CashLine outRow;
  outRow.correspondenceId = account(session, chart::kAdminExpense);
  outRow.amount = Money::fromCents(777);
  outRow.description = QStringLiteral("канцтовари");
  const Result<std::vector<DocumentLine>> outLines = composeCashOut(accounts.value(), {outRow});
  REQUIRE(outLines);
  cashOut.lines = outLines.value();
  saveAndPost(session, cashOut);
  const Result<QString> vko = forms.cashVoucherHtml(cashOut);
  REQUIRE(vko);
  REQUIRE(vko.value().contains(QStringLiteral("ВКО-print")));
  REQUIRE(vko.value().contains(QStringLiteral("Видатковий касовий ордер")));
  REQUIRE(vko.value().contains(QStringLiteral("канцтовари")));
  REQUIRE(vko.value().contains(htmlEscape(Money::fromCents(777).toDisplay())));
  REQUIRE(vko.value().contains(QStringLiteral("Іваненко І.І.")));
  REQUIRE(vko.value().contains(QStringLiteral("паспорт АА 123456")));
  REQUIRE(vko.value().contains(QStringLiteral("content=\"portrait\"")));
  REQUIRE(forms.cashOrderHtml(cashOut));

  GoodsLine buy;
  buy.nomenclatureId = sugar;
  buy.quantity = Quantity::fromMilli(1000);
  buy.unitPrice = Money::fromCents(5000);
  buy.amount = Money::fromCents(5000);
  Document purchase =
      makeDoc(DocumentType::Purchase, QDate(2026, 9, 8), QStringLiteral("НП-print"));
  purchase.counterpartyId = buyer;
  const Result<std::vector<DocumentLine>> buyLines = composePurchase(accounts.value(), {buy});
  REQUIRE(buyLines);
  purchase.lines = buyLines.value();
  saveAndPost(session, purchase);

  GoodsLine sell = buy;
  sell.unitPrice = Money::fromCents(8000);
  sell.amount = Money::fromCents(8000);
  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 9, 11), QStringLiteral("РТ-print"));
  sale.counterpartyId = buyer;
  const Result<std::vector<DocumentLine>> sellLines = composeSale(accounts.value(), {sell});
  REQUIRE(sellLines);
  sale.lines = sellLines.value();
  const DocumentId saleId = saveAndPost(session, sale);
  const Result<Document> posted = session.documents().findById(saleId);
  REQUIRE(posted);
  const Result<QString> note = forms.deliveryNoteHtml(posted.value());
  REQUIRE(note);
  REQUIRE(note.value().contains(QStringLiteral("РТ-print")));
  REQUIRE(note.value().contains(QStringLiteral("Видаткова накладна")));
  REQUIRE(note.value().contains(QStringLiteral("Цукор")));
  REQUIRE(note.value().contains(htmlEscape(Money::fromCents(8000).toDisplay())));
  REQUIRE_FALSE(note.value().contains(htmlEscape(Money::fromCents(5000).toDisplay())));
}

TEST_CASE("purchase note and bank payment html")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId sugar = item(session, QStringLiteral("Цукор-html"));
  const CounterpartyId mill = customer(session, QStringLiteral("ТОВ Млин"));
  Counterparty millRow;
  millRow.id = mill;
  const Result<Counterparty> loadedMill = session.counterparties().findById(mill);
  REQUIRE(loadedMill);
  millRow = loadedMill.value();
  millRow.iban = QStringLiteral("UA783003350000000000000000001");
  millRow.bankName = QStringLiteral("ПриватБанк");
  REQUIRE(session.counterparties().update(millRow));
  CompanyProfile company;
  company.name = QStringLiteral("ТОВ Платник");
  company.iban = QStringLiteral("UA953220010000000000000001234");
  company.bankName = QStringLiteral("Ощадбанк");
  REQUIRE(session.company().save(company));
  GoodsLine buy;
  buy.nomenclatureId = sugar;
  buy.quantity = Quantity::fromMilli(1000);
  buy.unitPrice = Money::fromCents(4000);
  buy.amount = Money::fromCents(4000);
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 9, 8), QStringLiteral("НП-html"));
  purchase.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> buyLines = composePurchase(accounts.value(), {buy});
  REQUIRE(buyLines);
  purchase.lines = buyLines.value();
  saveAndPost(session, purchase);
  PrintForms forms(session.handle());
  const Result<QString> inNote = forms.purchaseNoteHtml(purchase);
  REQUIRE(inNote);
  REQUIRE(inNote.value().contains(QStringLiteral("Прибуткова накладна")));
  REQUIRE(inNote.value().contains(QStringLiteral("НП-html")));

  Document bank = makeDoc(DocumentType::BankPayment, QDate(2026, 9, 9), QStringLiteral("РР-html"));
  bank.counterpartyId = mill;
  BankLine pay;
  pay.amount = Money::fromCents(4000);
  pay.description = QStringLiteral("оплата рахунку");
  const Result<std::vector<DocumentLine>> bankLines = composeBank(accounts.value(), false, {pay});
  REQUIRE(bankLines);
  bank.lines = bankLines.value();
  saveAndPost(session, bank);
  const Result<QString> slip = forms.bankPaymentHtml(bank);
  REQUIRE(slip);
  REQUIRE(slip.value().contains(QStringLiteral("Платіжна інструкція")));
  REQUIRE(slip.value().contains(QStringLiteral("РР-html")));
  REQUIRE(slip.value().contains(QStringLiteral("UA953220010000000000000001234")));
  REQUIRE(slip.value().contains(QStringLiteral("UA783003350000000000000000001")));
  REQUIRE(slip.value().contains(QStringLiteral("content=\"landscape\"")));
  REQUIRE(forms.documentHtml(bank));
}

TEST_CASE("service act and customer invoice print")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Друк-послуги"));
  GoodsLine service;
  service.description = QStringLiteral("аудит");
  service.amount = Money::fromCents(120000);
  Document act = makeDoc(DocumentType::ServiceSale, QDate(2026, 9, 11), QStringLiteral("РП-html"));
  act.counterpartyId = buyer;
  const Result<std::vector<DocumentLine>> actLines =
      composeServiceSale(accounts.value(), {service});
  REQUIRE(actLines);
  act.lines = actLines.value();
  saveAndPost(session, act);
  PrintForms forms(session.handle());
  const Result<QString> actHtml = forms.documentHtml(act);
  REQUIRE(actHtml);
  REQUIRE(actHtml.value().contains(QStringLiteral("Акт наданих послуг")));
  REQUIRE(actHtml.value().contains(QStringLiteral("аудит")));

  const NomenclatureId tea = item(session, QStringLiteral("Чай-рахунок-друк"));
  GoodsLine goods;
  goods.nomenclatureId = tea;
  goods.quantity = Quantity::fromMilli(2000);
  goods.unitPrice = Money::fromCents(5000);
  goods.amount = Money::fromCents(10000);
  Document invoice =
      makeDoc(DocumentType::CustomerInvoice, QDate(2026, 9, 11), QStringLiteral("РХ-html"));
  invoice.counterpartyId = buyer;
  const Result<std::vector<DocumentLine>> invLines =
      composeCustomerInvoice(accounts.value(), {goods});
  REQUIRE(invLines);
  invoice.lines = invLines.value();
  REQUIRE(session.documents().insert(invoice));
  const Result<QString> invHtml = forms.documentHtml(invoice);
  REQUIRE(invHtml);
  REQUIRE(invHtml.value().contains(QStringLiteral("Рахунок на оплату")));
  REQUIRE(invHtml.value().contains(QStringLiteral("не проводиться")));
}

TEST_CASE("supplier service act prints 92 and 631")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const CounterpartyId mill = customer(session, QStringLiteral("ТОВ Друк-оренда"));
  CashLine rent;
  rent.correspondenceId = account(session, chart::kAdminExpense);
  rent.amount = Money::fromCents(800000);
  rent.description = QStringLiteral("оренда офісу");
  Document act =
      makeDoc(DocumentType::ServicePurchase, QDate(2026, 5, 15), QStringLiteral("ПУ-html"));
  act.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> lines = composeServicePurchase(accounts.value(), {rent});
  REQUIRE(lines);
  act.lines = lines.value();
  saveAndPost(session, act);
  PrintForms forms(session.handle());
  const Result<QString> html = forms.documentHtml(act);
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("Акт отриманих послуг")));
  REQUIRE(html.value().contains(QStringLiteral("оренда офісу")));
  REQUIRE(html.value().contains(QStringLiteral("Дт 92, 152 або 154")));
  REQUIRE(html.value().contains(QStringLiteral("Кт 631")));
}

TEST_CASE("cash book journal write-off and statements print")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId tea = item(session, QStringLiteral("Чай-акт"));
  const CounterpartyId mill = customer(session, QStringLiteral("Склад-акт"));
  GoodsLine buy;
  buy.nomenclatureId = tea;
  buy.quantity = Quantity::fromMilli(1000);
  buy.unitPrice = Money::fromCents(4000);
  buy.amount = Money::fromCents(4000);
  Document purchase = makeDoc(DocumentType::Purchase, QDate(2026, 9, 1), QStringLiteral("НП-act"));
  purchase.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> buyLines = composePurchase(accounts.value(), {buy});
  REQUIRE(buyLines);
  purchase.lines = buyLines.value();
  saveAndPost(session, purchase);

  Document cash = makeDoc(DocumentType::CashIn, QDate(2026, 9, 2), QStringLiteral("ПКО-book"));
  CashLine cashRow;
  cashRow.correspondenceId = account(session, chart::kRevenue);
  cashRow.amount = Money::fromCents(500);
  const Result<std::vector<DocumentLine>> cashLines = composeCashIn(accounts.value(), {cashRow});
  REQUIRE(cashLines);
  cash.lines = cashLines.value();
  saveAndPost(session, cash);

  PrintForms forms(session.handle());
  const Result<CashBook> book = session.cashBook().build(QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE(book);
  const Result<QString> bookHtml = forms.cashBookHtml(book.value());
  REQUIRE(bookHtml);
  REQUIRE(bookHtml.value().contains(QStringLiteral("КО-4")));
  REQUIRE(bookHtml.value().contains(QStringLiteral("content=\"landscape\"")));

  const Result<CashJournal> journal =
      CashJournalReport(session.handle()).build(QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE(journal);
  const Result<QString> journalHtml = forms.cashJournalHtml(journal.value());
  REQUIRE(journalHtml);
  REQUIRE(journalHtml.value().contains(QStringLiteral("КО-3")));

  GoodsLine scrap = buy;
  Document writeOff = makeDoc(DocumentType::WriteOff, QDate(2026, 9, 3), QStringLiteral("СП-act"));
  const Result<std::vector<DocumentLine>> wo = composeWriteOff(accounts.value(), {scrap});
  REQUIRE(wo);
  writeOff.lines = wo.value();
  saveAndPost(session, writeOff);
  const Result<QString> act = forms.documentHtml(writeOff);
  REQUIRE(act);
  REQUIRE(act.value().contains(QStringLiteral("Акт списання")));

  FinancialStatementReport statements(session.handle());
  const Result<IncomeStatement> pl = statements.income(QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE(pl);
  const Result<QString> plHtml = forms.incomeStatementHtml(pl.value());
  REQUIRE(plHtml);
  REQUIRE(plHtml.value().contains(QStringLiteral("Звіт про фінансові результати")));
  const Result<BalanceSheet> balance = statements.balance(QDate(2026, 9, 30));
  REQUIRE(balance);
  const Result<QString> balHtml = forms.balanceSheetHtml(balance.value());
  REQUIRE(balHtml);
  REQUIRE(balHtml.value().contains(QStringLiteral("Баланс")));
  const Result<IncomeBook> income =
      IncomeBookReport(session.handle()).build(QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE(income);
  const Result<QString> incHtml = forms.incomeBookHtml(income.value());
  REQUIRE(incHtml);
  REQUIRE(incHtml.value().contains(QStringLiteral("Книга обліку доходів")));

  GoodsLine ret = buy;
  Document purchaseReturn =
      makeDoc(DocumentType::PurchaseReturn, QDate(2026, 9, 4), QStringLiteral("ПП-act"));
  purchaseReturn.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> pret = composePurchaseReturn(accounts.value(), {ret});
  REQUIRE(pret);
  purchaseReturn.lines = pret.value();
  const Result<QString> pretHtml = forms.documentHtml(purchaseReturn);
  REQUIRE(pretHtml);
  REQUIRE(pretHtml.value().contains(QStringLiteral("Повернення постачальнику")));
  REQUIRE(pretHtml.value().contains(QStringLiteral("Дт 631 Кт 281")));

  const Result<TrialBalance> osv =
      TrialBalanceReport(session.handle()).build(QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE(osv);
  const Result<QString> osvHtml = forms.trialBalanceHtml(osv.value());
  REQUIRE(osvHtml);
  REQUIRE(osvHtml.value().contains(QStringLiteral("content=\"landscape\"")));
  REQUIRE(osvHtml.value().contains(QStringLiteral("border-collapse:separate")));
  REQUIRE_FALSE(osvHtml.value().contains(QStringLiteral("border-collapse:collapse")));
}

TEST_CASE("prepareHtmlForPrint repeats table header rows", "[print][headers]")
{
  QString rows;
  rows += QStringLiteral("<tr><th>A</th><th>B</th></tr>");
  for (int i = 0; i < 80; ++i) {
    rows += QStringLiteral("<tr><td>%1</td><td>row text %1</td></tr>").arg(i);
  }
  const QString html = QStringLiteral("<!DOCTYPE html><html><head>"
                                      "<meta name=\"fin-page\" content=\"landscape\">"
                                      "<style>body{font-family:serif;font-size:9.5pt;}"
                                      "table{width:100%;border-collapse:separate;border-spacing:0;}"
                                      "th,td{border:1px solid #000;padding:3px 5px;}</style>"
                                      "</head><body>"
                                      "<h1>ОСВ</h1><h2>period</h2>"
                                      "<p class=\"meta\">Підприємство: Demo</p>"
                                      "<table width=\"100%\">%1</table></body></html>")
                           .arg(rows);

  ui::PrintConfig on;
  on.repeatTableHeaders = true;
  on.pageNumbers = true;
  // A4 landscape content area ~96 dpi, 8 mm margins.
  const QSizeF pageSize(1063.0, 732.0);
  const QString prepared = ui::prepareHtmlForPrint(html, on, pageSize);
  REQUIRE(prepared.count(QStringLiteral("<th>A</th>")) >= 2);
  REQUIRE(prepared.count(QStringLiteral("<table")) >= 2);

  // Регресія: після першої сторінки фрагменти не повинні бути по 1 рядку.
  {
    static const QRegularExpression tableRe(QStringLiteral("<table\\b[^>]*>[\\s\\S]*?</table>"),
                                            QRegularExpression::CaseInsensitiveOption);
    auto it = tableRe.globalMatch(prepared);
    REQUIRE(it.hasNext());
    it.next(); // first fragment
    REQUIRE(it.hasNext());
    const QString second = it.next().captured();
    const int trCount = static_cast<int>(second.count(QStringLiteral("<tr"), Qt::CaseInsensitive));
    REQUIRE(trCount >= 5);
  }

  ui::PrintConfig off;
  off.repeatTableHeaders = false;
  const QString untouched = ui::prepareHtmlForPrint(html, off, pageSize);
  REQUIRE(untouched.count(QStringLiteral("<th>A</th>")) == 1);

  QString sheetRows = QStringLiteral("<tr><th>H</th></tr>");
  for (int i = 0; i < 40; ++i) {
    sheetRows += QStringLiteral("<tr><td>%1</td></tr>").arg(i);
  }
  const QString sheetDoc =
      QStringLiteral("<html><head><meta name=\"fin-page\" content=\"landscape\">"
                     "<style>body{font-size:9.5pt;}</style></head><body>"
                     "<table class=\"sheet\">%1</table></body></html>")
          .arg(sheetRows);
  const QString sheetPrepared = ui::prepareHtmlForPrint(sheetDoc, on, pageSize);
  REQUIRE(sheetPrepared.count(QStringLiteral("<th>H</th>")) == 1);
}

TEST_CASE("prepareHtmlForPrint injects body font size", "[print][font]")
{
  const QString html =
      QStringLiteral("<!DOCTYPE html><html><head>"
                     "<meta name=\"fin-page\" content=\"portrait\">"
                     "<style>body{font-family:'Times New Roman',Times,serif;font-size:11pt;"
                     "color:#000;}"
                     "h1{font-size:14pt;}</style></head><body><p>x</p></body></html>");

  ui::PrintConfig cfg;
  cfg.repeatTableHeaders = false;
  cfg.fontPt = 13.0;
  const QString prepared = ui::prepareHtmlForPrint(html, cfg);
  REQUIRE(prepared.contains(QStringLiteral("body{font-family:'Times New Roman',"
                                           "Times,serif;font-size:13.0pt;")));
  REQUIRE(prepared.contains(QStringLiteral("h1{font-size:14pt;}")));

  cfg.fontPt = 11.0;
  const QString landscape =
      QStringLiteral("<html><head><meta name=\"fin-page\" content=\"landscape\">"
                     "<style>body{font-size:11pt;}</style></head><body></body></html>");
  const QString landPrepared = ui::prepareHtmlForPrint(landscape, cfg);
  REQUIRE(landPrepared.contains(QStringLiteral("font-size:9.5pt")));
}

TEST_CASE("vacation order and sick leave calc print html", "[print][payroll]")
{
  LedgerSession session = openSession();
  Employee emp;
  emp.taxId = QStringLiteral("9988776655");
  emp.name = QStringLiteral("Іваненко Іван");
  emp.hiredOn = QDate(2025, 1, 1);
  emp.salary = Money::fromCents(2000000);
  emp.position = QStringLiteral("Бухгалтер");
  emp.isActive = true;
  emp.id = session.employees().insert(emp).value();

  VacationCalc vac;
  vac.employeeId = emp.id;
  vac.vacationFrom = QDate(2026, 7, 1);
  vac.vacationTo = QDate(2026, 7, 14);
  vac.vacationDays = 14;
  vac.avgDailyCents = Money::fromCents(80000);
  vac.gross = Money::fromCents(1120000);
  Result<QString> vacHtml = PrintForms(session.handle()).vacationOrderHtml(vac, emp);
  REQUIRE(vacHtml);
  REQUIRE(vacHtml.value().contains(QStringLiteral("П-3")));
  REQUIRE(vacHtml.value().contains(QStringLiteral("Іваненко")));
  REQUIRE(vacHtml.value().contains(QStringLiteral("14")));

  SickLeaveCalc sick;
  sick.employeeId = emp.id;
  sick.sickFrom = QDate(2026, 8, 1);
  sick.sickTo = QDate(2026, 8, 10);
  sick.totalDays = 10;
  sick.employerDays = 5;
  sick.pfuDays = 5;
  sick.experienceYears = 8;
  sick.avgDailyCents = Money::fromCents(70000);
  sick.employerGross = Money::fromCents(350000);
  sick.taxes.ssc = Money::fromCents(77000);
  sick.taxes.pit = Money::fromCents(63000);
  sick.taxes.military = Money::fromCents(17500);
  Result<QString> sickHtml = PrintForms(session.handle()).sickLeaveCalcHtml(sick, emp);
  REQUIRE(sickHtml);
  REQUIRE(sickHtml.value().contains(QStringLiteral("лікарняних")));
  REQUIRE(sickHtml.value().contains(QStringLiteral("Іваненко")));
  REQUIRE(sickHtml.value().contains(QStringLiteral("ПФУ")));
}

TEST_CASE("orders transfer and production print titles")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Друк-замовлення"));
  const NomenclatureId tea = item(session, QStringLiteral("Чай-друк-замовлення"));
  GoodsLine goods;
  goods.nomenclatureId = tea;
  goods.quantity = Quantity::fromMilli(1000);
  goods.unitPrice = Money::fromCents(5000);
  goods.amount = Money::fromCents(5000);

  Document custOrder =
      makeDoc(DocumentType::CustomerOrder, QDate(2026, 9, 12), QStringLiteral("ЗК-html"));
  custOrder.counterpartyId = buyer;
  const Result<std::vector<DocumentLine>> custLines =
      composeCustomerOrder(accounts.value(), {goods});
  REQUIRE(custLines);
  custOrder.lines = custLines.value();
  REQUIRE(session.documents().insert(custOrder));

  Document suppOrder =
      makeDoc(DocumentType::SupplierOrder, QDate(2026, 9, 12), QStringLiteral("ЗП-html"));
  suppOrder.counterpartyId = buyer;
  const Result<std::vector<DocumentLine>> suppLines =
      composeSupplierOrder(accounts.value(), {goods});
  REQUIRE(suppLines);
  suppOrder.lines = suppLines.value();
  REQUIRE(session.documents().insert(suppOrder));

  Document transfer =
      makeDoc(DocumentType::StockTransfer, QDate(2026, 9, 12), QStringLiteral("ПМ-html"));
  const Result<std::vector<DocumentLine>> transferLines =
      composeStockTransfer(accounts.value(), {goods});
  REQUIRE(transferLines);
  transfer.lines = transferLines.value();
  REQUIRE(session.documents().insert(transfer));

  Document issue =
      makeDoc(DocumentType::MaterialIssue, QDate(2026, 9, 12), QStringLiteral("ВН-html"));
  const Result<std::vector<DocumentLine>> issueLines =
      composeMaterialIssue(accounts.value(), {goods});
  REQUIRE(issueLines);
  issue.lines = issueLines.value();
  REQUIRE(session.documents().insert(issue));

  Document release =
      makeDoc(DocumentType::ProductionReport, QDate(2026, 9, 12), QStringLiteral("ВП-html"));
  GoodsLine product = goods;
  product.description = QStringLiteral("виріб");
  const Result<std::vector<DocumentLine>> releaseLines =
      composeProductionReport(accounts.value(), {product});
  REQUIRE(releaseLines);
  release.lines = releaseLines.value();
  REQUIRE(session.documents().insert(release));

  PrintForms forms(session.handle());
  REQUIRE(canPrintDocument(DocumentType::SupplierOrder));
  REQUIRE(canPrintDocument(DocumentType::CustomerOrder));
  REQUIRE(canPrintDocument(DocumentType::StockTransfer));
  REQUIRE(canPrintDocument(DocumentType::MaterialIssue));
  REQUIRE(canPrintDocument(DocumentType::ProductionReport));

  const Result<QString> custHtml = forms.documentHtml(custOrder);
  REQUIRE(custHtml);
  REQUIRE(custHtml.value().contains(QStringLiteral("Замовлення покупця")));
  REQUIRE_FALSE(custHtml.value().contains(QStringLiteral("Рахунок на оплату")));

  const Result<QString> suppHtml = forms.documentHtml(suppOrder);
  REQUIRE(suppHtml);
  REQUIRE(suppHtml.value().contains(QStringLiteral("Замовлення постачальнику")));

  const Result<QString> transferHtml = forms.documentHtml(transfer);
  REQUIRE(transferHtml);
  REQUIRE(transferHtml.value().contains(QStringLiteral("внутрішнє переміщення")));

  const Result<QString> issueHtml = forms.documentHtml(issue);
  REQUIRE(issueHtml);
  REQUIRE(issueHtml.value().contains(QStringLiteral("Вимога-накладна")));

  const Result<QString> releaseHtml = forms.documentHtml(release);
  REQUIRE(releaseHtml);
  REQUIRE(releaseHtml.value().contains(QStringLiteral("Акт випуску")));
}

TEST_CASE("counterparty turnover print and vacation/sick canPrint")
{
  REQUIRE(canPrintDocument(DocumentType::VacationPayroll));
  REQUIRE(canPrintDocument(DocumentType::SickLeavePayroll));

  LedgerSession session = openSession();
  const Result<CounterpartyTurnover> report =
      session.counterpartyTurnover().build(account(session, chart::kCustomers),
                                           QDate(2026, 1, 1), QDate(2026, 12, 31));
  REQUIRE(report);
  const Result<QString> html =
      PrintForms(session.handle()).counterpartyTurnoverHtml(report.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("Обороти з контрагентами")));
  REQUIRE(html.value().contains(QStringLiteral("361")));
}

