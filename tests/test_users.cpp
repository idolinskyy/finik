#include "support/Session.h"

#include "fin/auth/Access.h"
#include "fin/db/AuditLog.h"
#include "fin/db/UserRepository.h"
#include "fin/demo/DemoSeed.h"

#include <QSqlQuery>

using namespace fin;
using namespace fin::test;

TEST_CASE("password is optional until set and then authenticates")
{
  LedgerSession session = openSession();
  UserRepository users(session.handle());
  const Result<bool> required = users.passwordRequired();
  REQUIRE(required);
  REQUIRE_FALSE(required.value());
  const Result<User> admin = users.findByLogin(QStringLiteral("admin"));
  REQUIRE(admin);
  REQUIRE_FALSE(admin.value().hasPassword);

  REQUIRE_FALSE(users.setPassword(admin.value().id, QStringLiteral("123")));
  REQUIRE(users.setPassword(admin.value().id, QStringLiteral("secret1")));
  const Result<bool> nowRequired = users.passwordRequired();
  REQUIRE(nowRequired);
  REQUIRE(nowRequired.value());

  REQUIRE_FALSE(users.authenticate(QStringLiteral("admin"), QStringLiteral("wrong")));
  const Result<User> ok = users.authenticate(QStringLiteral("admin"), QStringLiteral("secret1"));
  REQUIRE(ok);
  REQUIRE(ok.value().id == admin.value().id);

  QSqlQuery hashQuery(session.handle());
  REQUIRE(hashQuery.exec(QStringLiteral("SELECT password_hash FROM users WHERE login = 'admin'")));
  REQUIRE(hashQuery.next());
  const QByteArray stored = hashQuery.value(0).toByteArray();
  REQUIRE_FALSE(stored.isEmpty());
  REQUIRE_FALSE(QString::fromUtf8(stored).contains(QStringLiteral("secret1")));

  // Адміну не можна зняти пароль; порожній хеш більше не пускає в систему.
  REQUIRE_FALSE(users.setPassword(admin.value().id, QString()));
  REQUIRE_FALSE(users.authenticate(QStringLiteral("admin"), QString()));
  REQUIRE(users.authenticate(QStringLiteral("admin"), QStringLiteral("secret1")));
}

TEST_CASE("clearCurrentUser drops session until setCurrentUser")
{
  LedgerSession session = openSession();
  UserRepository users(session.handle());
  const Result<User> admin = users.findByLogin(QStringLiteral("admin"));
  REQUIRE(admin);
  REQUIRE(users.setPassword(admin.value().id, QStringLiteral("secret1")));
  REQUIRE(users.setCurrentUser(admin.value().id));
  REQUIRE(users.currentUser());

  REQUIRE(users.clearCurrentUser());
  REQUIRE_FALSE(users.currentUser());
  REQUIRE(users.setCurrentUser(admin.value().id));
  REQUIRE(users.currentUser());
}

TEST_CASE("empty password hash never authenticates; multi-user without passwords stays open")
{
  LedgerSession session = openSession();
  UserRepository users(session.handle());
  const Result<User> admin = users.findByLogin(QStringLiteral("admin"));
  REQUIRE(admin);
  REQUIRE_FALSE(users.authenticate(QStringLiteral("admin"), QStringLiteral("any")));

  User row;
  row.login = QStringLiteral("viewer1");
  row.displayName = QStringLiteral("Viewer");
  row.role = UserRole::Viewer;
  row.isActive = true;
  const Result<UserId> vid = users.insert(row);
  REQUIRE(vid);
  const Result<bool> required = users.passwordRequired();
  REQUIRE(required);
  REQUIRE_FALSE(required.value());
}

TEST_CASE("demo seed leaves app login-capable without passwords")
{
  LedgerSession session = openSession();
  UserRepository users(session.handle());
  REQUIRE(seedDemoData(session, 42));
  const Result<bool> required = users.passwordRequired();
  REQUIRE(required);
  REQUIRE_FALSE(required.value());
  REQUIRE(users.findByLogin(QStringLiteral("buhgalter")));
  REQUIRE(users.findByLogin(QStringLiteral("kasa")));
}

TEST_CASE("document write is recorded in the audit log")
{
  LedgerSession session = openSession();
  Document cash = makeDoc(DocumentType::Manual, QDate(2026, 5, 1), QStringLiteral("БО-audit"));
  cash.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 100));
  saveAndPost(session, cash);
  const Result<std::vector<AuditEntry>> log = AuditLog(session.handle()).listRecent(20);
  REQUIRE(log);
  bool sawInsert = false;
  bool sawPost = false;
  for (const AuditEntry & row : log.value()) {
    if (row.action == QStringLiteral("insert") && row.entity == QStringLiteral("document")) {
      sawInsert = true;
    }
    if (row.action == QStringLiteral("post")) {
      sawPost = true;
    }
  }
  REQUIRE(sawInsert);
  REQUIRE(sawPost);
}

TEST_CASE("company and period changes are audited")
{
  LedgerSession session = openSession();
  CompanyProfile profile;
  profile.name = QStringLiteral("ТОВ Аудит");
  REQUIRE(session.company().save(profile));
  REQUIRE(session.period().closeThroughMonth(2026, 2));
  const Result<std::vector<AuditEntry>> log = AuditLog(session.handle()).listRecent(50);
  REQUIRE(log);
  bool sawCompany = false;
  bool sawClose = false;
  for (const AuditEntry & row : log.value()) {
    if (row.action == QStringLiteral("update") && row.entity == QStringLiteral("company")) {
      sawCompany = true;
    }
    if (row.action == QStringLiteral("close") && row.entity == QStringLiteral("period")) {
      sawClose = true;
    }
  }
  REQUIRE(sawCompany);
  REQUIRE(sawClose);
}

namespace {

UserId addUser(UserRepository & users, const QString & login, UserRole role)
{
  User row;
  row.login = login;
  row.displayName = login;
  row.role = role;
  row.isActive = true;
  const Result<UserId> id = users.insert(row);
  REQUIRE(id);
  return id.value();
}

} // namespace

TEST_CASE("default admin has the admin role")
{
  LedgerSession session = openSession();
  UserRepository users(session.handle());
  const Result<User> admin = users.findByLogin(QStringLiteral("admin"));
  REQUIRE(admin);
  REQUIRE(admin.value().role == UserRole::Admin);
  REQUIRE(Access::canWriteDocument(admin.value().role, DocumentType::Sale));
  REQUIRE(Access::canManageUsers(admin.value().role));
}

TEST_CASE("roles deny writes the UI would hide")
{
  LedgerSession session = openSession();
  UserRepository users(session.handle());
  const Result<User> admin = users.findByLogin(QStringLiteral("admin"));
  REQUIRE(admin);
  const UserId cashierId = addUser(users, QStringLiteral("kasa"), UserRole::Cashier);
  const UserId viewerId = addUser(users, QStringLiteral("oglyad"), UserRole::Viewer);
  const UserId accountantId = addUser(users, QStringLiteral("buh"), UserRole::Accountant);

  SECTION("cannot demote the last admin")
  {
    User demoted = admin.value();
    demoted.role = UserRole::Accountant;
    const Status status = users.update(demoted);
    REQUIRE_FALSE(status);
    REQUIRE(status.message().contains(QStringLiteral("останнього")));
  }

  SECTION("accountant cannot manage users")
  {
    REQUIRE(users.setCurrentUser(accountantId));
    User extra;
    extra.login = QStringLiteral("extra");
    extra.displayName = QStringLiteral("extra");
    extra.role = UserRole::Viewer;
    REQUIRE_FALSE(users.insert(extra));
    REQUIRE(users.setCurrentUser(admin.value().id));
  }

  SECTION("cashier can post cash in but not a sale")
  {
    REQUIRE(users.setCurrentUser(cashierId));
    Document sale = makeDoc(DocumentType::Sale, QDate(2026, 5, 1), QStringLiteral("РТ-deny"));
    sale.lines.push_back(
        line(account(session, chart::kCustomers), account(session, chart::kRevenue), 100));
    const Result<DocumentId> saleId = session.documents().insert(sale);
    REQUIRE_FALSE(saleId);
    REQUIRE(saleId.message().contains(QStringLiteral("касир")));

    Document fa = makeDoc(DocumentType::FaCommission, QDate(2026, 5, 1), QStringLiteral("ОЗ-deny"));
    fa.lines.push_back(
        line(account(session, chart::kMachinery), account(session, chart::kSuppliers), 100));
    REQUIRE_FALSE(session.documents().insert(fa));

    Document cash = makeDoc(DocumentType::CashIn, QDate(2026, 5, 1), QStringLiteral("ПКО-ok"));
    cash.lines.push_back(
        line(account(session, chart::kCash), account(session, chart::kRevenue), 100));
    const Result<DocumentId> cashId = session.documents().insert(cash);
    REQUIRE(cashId);
    REQUIRE(session.posting().post(cashId.value()));
    REQUIRE(users.setCurrentUser(admin.value().id));
  }

  SECTION("viewer cannot post or change company")
  {
    Document cash = makeDoc(DocumentType::CashIn, QDate(2026, 5, 2), QStringLiteral("ПКО-view"));
    cash.lines.push_back(
        line(account(session, chart::kCash), account(session, chart::kRevenue), 200));
    const Result<DocumentId> cashId = session.documents().insert(cash);
    REQUIRE(cashId);
    REQUIRE(users.setCurrentUser(viewerId));
    REQUIRE_FALSE(session.posting().post(cashId.value()));
    CompanyProfile profile;
    profile.name = QStringLiteral("ТОВ Заборона");
    REQUIRE_FALSE(session.company().save(profile));
    Counterparty party;
    party.name = QStringLiteral("Не можна");
    REQUIRE_FALSE(session.counterparties().insert(party));
    REQUIRE(users.setCurrentUser(admin.value().id));
  }

  SECTION("accountant can edit company; cashier cannot import bank")
  {
    REQUIRE(users.setCurrentUser(accountantId));
    CompanyProfile profile;
    profile.name = QStringLiteral("ТОВ Бухгалтер");
    REQUIRE(session.company().save(profile));
    REQUIRE(users.setCurrentUser(cashierId));
    REQUIRE_FALSE(Access::requireImportBank(session.handle()));
    REQUIRE_FALSE(Access::requireSeedDemo(session.handle()));
    REQUIRE_FALSE(Access::requireRestore(session.handle()));
    REQUIRE(users.setCurrentUser(admin.value().id));
  }
}
