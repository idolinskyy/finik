#include "support/Session.h"
#include <QSettings>

#include "fin/db/UserRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/User.h"

using namespace fin;
using namespace fin::test;

TEST_CASE("chart of accounts is seeded with postable leaves")
{
  LedgerSession session = openSession();
  REQUIRE(account(session, chart::kCash).isValid());
  REQUIRE(account(session, chart::kBank).isValid());
  REQUIRE(account(session, chart::kAuxiliary).isValid());
  REQUIRE(account(session, chart::kServiceRevenue).isValid());
  REQUIRE(account(session, chart::kRevenue).isValid());

  const Result<Account> folder = session.accounts().findByCode(QStringLiteral("30"));
  REQUIRE(folder);
  REQUIRE(folder.value().isFolder);
  REQUIRE_FALSE(folder.value().postable());
}

TEST_CASE("posted document is immutable until unposted")
{
  LedgerSession session = openSession();
  Document document = makeDoc(DocumentType::Manual, QDate(2026, 1, 10), QStringLiteral("БО-1"));
  document.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 10000));
  const DocumentId id = saveAndPost(session, document);

  document.id = id;
  document.comment = QStringLiteral("зміна");
  REQUIRE_FALSE(session.documents().update(document));
  REQUIRE_FALSE(session.documents().remove(id));
  REQUIRE_FALSE(session.documents().replaceLines(id, document.lines));
  REQUIRE_FALSE(session.documents().replaceLines(id, document.lines, true));
  REQUIRE_FALSE(session.documents().updateLineAmounts(id, document.lines));

  REQUIRE(session.posting().unpost(id));
  REQUIRE(session.documents().update(document));
  REQUIRE(session.documents().replaceLines(id, document.lines));
  REQUIRE(session.documents().remove(id));
}

TEST_CASE("viewer cannot mutate document lines or posted flag")
{
  LedgerSession session = openSession();
  Document document = makeDoc(DocumentType::Manual, QDate(2026, 1, 11), QStringLiteral("БО-rbac"));
  document.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 2500));
  const Result<DocumentId> id = session.documents().insert(document);
  REQUIRE(id);

  UserRepository users(session.handle());
  User viewer;
  viewer.login = QStringLiteral("oglyad-doc");
  viewer.displayName = viewer.login;
  viewer.role = UserRole::Viewer;
  const Result<UserId> viewerId = users.insert(viewer);
  REQUIRE(viewerId);
  REQUIRE(users.setCurrentUser(viewerId.value()));

  REQUIRE_FALSE(session.documents().replaceLines(id.value(), document.lines));
  REQUIRE_FALSE(session.documents().setPosted(id.value(), true, false));
  REQUIRE_FALSE(session.documents().setEdoPayload(id.value(), QStringLiteral("<x/>"),
                                                  QStringLiteral("abc"), false));
}

TEST_CASE("cannot post twice or unpost a draft")
{
  LedgerSession session = openSession();
  Document document = makeDoc(DocumentType::Manual, QDate(2026, 1, 10), QStringLiteral("БО-2"));
  document.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 500));
  const Result<DocumentId> id = session.documents().insert(document);
  REQUIRE(id);
  REQUIRE_FALSE(session.posting().unpost(id.value()));
  REQUIRE(session.posting().post(id.value()));
  REQUIRE_FALSE(session.posting().post(id.value()));
}

TEST_CASE("document type rules reject illegal postings")
{
  LedgerSession session = openSession();
  const AccountId cash = account(session, chart::kCash);
  const AccountId revenue = account(session, chart::kRevenue);
  const AccountId aux = account(session, chart::kAuxiliary);
  const AccountId folder = session.accounts().findByCode(QStringLiteral("30")).value().id;

  SECTION("folder account")
  {
    Document document =
        makeDoc(DocumentType::Manual, QDate(2026, 1, 1), QStringLiteral("БО-bad-folder"));
    document.lines.push_back(line(folder, revenue, 100));
    const Result<DocumentId> id = session.documents().insert(document);
    REQUIRE(id);
    REQUIRE_FALSE(session.posting().post(id.value()));
  }

  SECTION("00 outside opening balance")
  {
    Document document = makeDoc(DocumentType::Manual, QDate(2026, 1, 1), QStringLiteral("БО-aux"));
    document.lines.push_back(line(cash, aux, 100));
    const Result<DocumentId> id = session.documents().insert(document);
    REQUIRE(id);
    REQUIRE_FALSE(session.posting().post(id.value()));
  }

  SECTION("cash in must debit 301")
  {
    Document document = makeDoc(DocumentType::CashIn, QDate(2026, 1, 1), QStringLiteral("ПКО-bad"));
    document.lines.push_back(line(revenue, cash, 100));
    const Result<DocumentId> id = session.documents().insert(document);
    REQUIRE(id);
    REQUIRE_FALSE(session.posting().post(id.value()));
  }

  SECTION("sale requires counterparty")
  {
    Document document = makeDoc(DocumentType::Sale, QDate(2026, 1, 1), QStringLiteral("РТ-bad"));
    document.lines.push_back(line(account(session, chart::kCustomers), revenue, 100));
    const Result<DocumentId> id = session.documents().insert(document);
    REQUIRE(id);
    REQUIRE_FALSE(session.posting().post(id.value()));
  }

  SECTION("same debit and credit")
  {
    Document document = makeDoc(DocumentType::Manual, QDate(2026, 1, 1), QStringLiteral("БО-same"));
    document.lines.push_back(line(cash, cash, 100));
    const Result<DocumentId> id = session.documents().insert(document);
    REQUIRE(id);
    REQUIRE_FALSE(session.posting().post(id.value()));
  }

  SECTION("service sale requires counterparty")
  {
    Document document =
        makeDoc(DocumentType::ServiceSale, QDate(2026, 1, 1), QStringLiteral("РП-bad"));
    document.lines.push_back(
        line(account(session, chart::kCustomers), account(session, chart::kServiceRevenue), 100));
    const Result<DocumentId> id = session.documents().insert(document);
    REQUIRE(id);
    REQUIRE_FALSE(session.posting().post(id.value()));
  }

  SECTION("bank 361 requires counterparty")
  {
    Document document =
        makeDoc(DocumentType::BankPayment, QDate(2026, 1, 1), QStringLiteral("РР-cp"));
    document.lines.push_back(
        line(account(session, chart::kBank), account(session, chart::kCustomers), 100));
    const Result<DocumentId> id = session.documents().insert(document);
    REQUIRE(id);
    REQUIRE_FALSE(session.posting().post(id.value()));
  }

  SECTION("bank fee on 92 posts without counterparty")
  {
    Document document =
        makeDoc(DocumentType::BankPayment, QDate(2026, 1, 1), QStringLiteral("РР-fee"));
    document.lines.push_back(
        line(account(session, chart::kAdminExpense), account(session, chart::kBank), 100));
    saveAndPost(session, document);
  }

  SECTION("bank correspondence cannot be 00")
  {
    Document document =
        makeDoc(DocumentType::BankPayment, QDate(2026, 1, 1), QStringLiteral("РР-00"));
    document.lines.push_back(line(account(session, chart::kBank), aux, 100));
    const Result<DocumentId> id = session.documents().insert(document);
    REQUIRE(id);
    REQUIRE_FALSE(session.posting().post(id.value()));
  }

  SECTION("bank cannot mix incoming and outgoing")
  {
    Document document =
        makeDoc(DocumentType::BankPayment, QDate(2026, 1, 1), QStringLiteral("РР-mix"));
    document.counterpartyId = customer(session, QStringLiteral("Змішаний"));
    document.lines.push_back(
        line(account(session, chart::kBank), account(session, chart::kCustomers), 100));
    document.lines.push_back(
        line(account(session, chart::kSuppliers), account(session, chart::kBank), 50));
    const Result<DocumentId> id = session.documents().insert(document);
    REQUIRE(id);
    REQUIRE_FALSE(session.posting().post(id.value()));
  }
}

TEST_CASE("allocateNumber is monotonic per type")
{
  LedgerSession session = openSession();
  const Result<QString> first = session.documents().allocateNumber(DocumentType::CashIn);
  const Result<QString> second = session.documents().allocateNumber(DocumentType::CashIn);
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(first.value() == QStringLiteral("ПКО-000001"));
  REQUIRE(second.value() == QStringLiteral("ПКО-000002"));
}

TEST_CASE("allocateNumber respects settings for prefix strategy")
{
  struct SettingsGuard
  {
    ~SettingsGuard()
    {
      QSettings s;
      s.remove(QStringLiteral("business/prefix_strategy"));
      s.remove(QStringLiteral("business/doc_prefix"));
    }
  } guard;

  LedgerSession session = openSession();
  QSettings settings;

  // Test global prefix
  settings.setValue(QStringLiteral("business/prefix_strategy"), QStringLiteral("global"));
  settings.setValue(QStringLiteral("business/doc_prefix"), QStringLiteral("MY-"));
  const Result<QString> gNum = session.documents().allocateNumber(DocumentType::Purchase);
  REQUIRE(gNum);
  REQUIRE(gNum.value() == QStringLiteral("MY-000001"));

  // Test per-type prefix
  settings.setValue(QStringLiteral("business/prefix_strategy"), QStringLiteral("per_type"));
  const Result<QString> pNum = session.documents().allocateNumber(DocumentType::Purchase);
  REQUIRE(pNum);
  REQUIRE(pNum.value() ==
          QStringLiteral("MY-НП-000002")); // НП is prefix for Purchase, sequence is 2
}

TEST_CASE("document journal lists saved documents in period")
{
  LedgerSession session = openSession();
  Document document = makeDoc(DocumentType::Manual, QDate(2026, 5, 10), QStringLiteral("БО-list"));
  document.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 12345));
  const DocumentId id = saveAndPost(session, document);

  const Result<std::vector<DocumentHeader>> list =
      session.documents().list(QDate(2026, 5, 1), QDate(2026, 5, 31));
  REQUIRE(list);
  REQUIRE(list.value().size() == 1);
  REQUIRE(list.value().front().id == id);
  REQUIRE(list.value().front().posted);
  REQUIRE(list.value().front().total.cents() == 12345);
}

TEST_CASE("every document type has a stable id, label and prefix")
{
  const DocumentType types[] = {
      DocumentType::OpeningBalance, DocumentType::Manual,          DocumentType::CashIn,
      DocumentType::CashOut,        DocumentType::Purchase,        DocumentType::Sale,
      DocumentType::BankPayment,    DocumentType::SaleReturn,      DocumentType::PurchaseReturn,
      DocumentType::WriteOff,       DocumentType::Stocktake,       DocumentType::PeriodResult,
      DocumentType::ServiceSale,    DocumentType::ServicePurchase, DocumentType::CustomerInvoice,
      DocumentType::FaCommission,   DocumentType::FaDepreciation,  DocumentType::FaWriteOff,
      DocumentType::MsReceipt,      DocumentType::MsIssue,         DocumentType::IaCommission,
      DocumentType::IaDepreciation, DocumentType::IaWriteOff,      DocumentType::TaxInvoice,
      DocumentType::Payroll};
  for (DocumentType type : types) {
    const QString id = documentTypeToString(type);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(documentTypeFromString(id) == type);
    REQUIRE_FALSE(documentTypeLabelUk(type).isEmpty());
    REQUIRE_FALSE(documentTypePrefix(type).isEmpty());
  }
  REQUIRE(documentTypePrefix(DocumentType::ServiceSale) == QStringLiteral("РП"));
  REQUIRE(documentTypePrefix(DocumentType::ServicePurchase) == QStringLiteral("ПУ"));
  REQUIRE(documentTypePrefix(DocumentType::CustomerInvoice) == QStringLiteral("РХ"));
  REQUIRE(documentTypePrefix(DocumentType::FaCommission) == QStringLiteral("ОЗ"));
  REQUIRE(documentTypePrefix(DocumentType::FaDepreciation) == QStringLiteral("АМ"));
  REQUIRE(documentTypePrefix(DocumentType::FaWriteOff) == QStringLiteral("СЗ"));
  REQUIRE(documentTypePrefix(DocumentType::MsReceipt) == QStringLiteral("МШ"));
  REQUIRE(documentTypePrefix(DocumentType::MsIssue) == QStringLiteral("МЕ"));
  REQUIRE(documentTypePrefix(DocumentType::IaCommission) == QStringLiteral("НМ"));
  REQUIRE(documentTypePrefix(DocumentType::IaDepreciation) == QStringLiteral("АН"));
  REQUIRE(documentTypePrefix(DocumentType::IaWriteOff) == QStringLiteral("СН"));
  REQUIRE(documentTypePrefix(DocumentType::TaxInvoice) == QStringLiteral("ПН"));
  REQUIRE(documentTypePrefix(DocumentType::Payroll) == QStringLiteral("ЗП"));
}
