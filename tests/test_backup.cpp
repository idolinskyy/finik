#include "support/Session.h"

#include "fin/db/AppPaths.h"
#include "fin/db/Restore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>

using namespace fin;
using namespace fin::test;

namespace {

void removeDbTree(const QString & path)
{
  QFile::remove(path);
  QFile::remove(path + QStringLiteral("-wal"));
  QFile::remove(path + QStringLiteral("-shm"));
  QFile::remove(path + QStringLiteral(".incoming"));
  QFile::remove(path + QStringLiteral(".incoming-wal"));
  QFile::remove(path + QStringLiteral(".incoming-shm"));
  QFile::remove(path + QStringLiteral(".restore-pending"));
  QFile::remove(path + QStringLiteral(".before-restore"));
}

} // namespace

TEST_CASE("backup restores posted documents into a new session")
{
  const QString source = QDir::current().absoluteFilePath(QStringLiteral("fin_backup_src.sqlite"));
  const QString copy = QDir::current().absoluteFilePath(QStringLiteral("fin_backup_dst.sqlite"));
  QFile::remove(source);
  QFile::remove(copy);

  DocumentId postedId;
  {
    Result<LedgerSession> session = LedgerSession::openFile(source);
    REQUIRE(session);
    Document document = makeDoc(DocumentType::Manual, QDate(2026, 9, 1), QStringLiteral("БО-bak"));
    document.lines.push_back(line(account(session.value(), chart::kCash),
                                  account(session.value(), chart::kRevenue), 4242));
    postedId = saveAndPost(session.value(), document);
    const Result<QString> saved = session.value().backupTo(copy);
    REQUIRE(saved);
    REQUIRE(QFileInfo::exists(saved.value()));
  }

  Result<LedgerSession> restored = LedgerSession::openFile(copy);
  REQUIRE(restored);
  const Result<Document> document = restored.value().documents().findById(postedId);
  REQUIRE(document);
  REQUIRE(document.value().posted);
  REQUIRE(document.value().total().cents() == 4242);
  REQUIRE(restored.value().verify());

  QFile::remove(source);
  QFile::remove(copy);
}

TEST_CASE("backup refuses to overwrite an existing file")
{
  const QString source = QDir::current().absoluteFilePath(QStringLiteral("fin_backup_src2.sqlite"));
  const QString copy = QDir::current().absoluteFilePath(QStringLiteral("fin_backup_dst2.sqlite"));
  QFile::remove(source);
  QFile::remove(copy);

  Result<LedgerSession> session = LedgerSession::openFile(source);
  REQUIRE(session);
  REQUIRE(session.value().backupTo(copy));
  REQUIRE_FALSE(session.value().backupTo(copy));

  QFile::remove(source);
  QFile::remove(copy);
}

TEST_CASE("staged restore replaces the live file after restart")
{
  const QString live = QDir::current().absoluteFilePath(QStringLiteral("fin_restore_live.sqlite"));
  const QString source = QDir::current().absoluteFilePath(QStringLiteral("fin_restore_src.sqlite"));
  const QString copy = QDir::current().absoluteFilePath(QStringLiteral("fin_restore_copy.sqlite"));
  removeDbTree(live);
  removeDbTree(source);
  QFile::remove(copy);

  {
    Result<LedgerSession> sourceSession = LedgerSession::openFile(source);
    REQUIRE(sourceSession);
    CompanyProfile profile;
    profile.name = QStringLiteral("Відновлена фірма");
    REQUIRE(sourceSession.value().company().save(profile));
    Document document =
        makeDoc(DocumentType::Manual, QDate(2026, 9, 2), QStringLiteral("БО-from-backup"));
    document.lines.push_back(line(account(sourceSession.value(), chart::kCash),
                                  account(sourceSession.value(), chart::kRevenue), 1111));
    saveAndPost(sourceSession.value(), document);
    REQUIRE(sourceSession.value().backupTo(copy));
  }

  {
    Result<LedgerSession> liveSession = LedgerSession::openFile(live);
    REQUIRE(liveSession);
    Document document =
        makeDoc(DocumentType::Manual, QDate(2026, 9, 3), QStringLiteral("БО-live-only"));
    document.lines.push_back(line(account(liveSession.value(), chart::kCash),
                                  account(liveSession.value(), chart::kRevenue), 2222));
    saveAndPost(liveSession.value(), document);
    REQUIRE(liveSession.value().stageRestoreFrom(copy));
    REQUIRE(QFileInfo::exists(restoreMarkerPath(live)));
    REQUIRE(QFileInfo::exists(restoreSafetyPath(live)));
  }

  REQUIRE(applyStagedRestore(live));
  REQUIRE_FALSE(QFileInfo::exists(restoreMarkerPath(live)));

  Result<LedgerSession> restored = LedgerSession::openFile(live);
  REQUIRE(restored);
  const Result<std::vector<DocumentHeader>> list =
      restored.value().documents().list(QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE(list);
  REQUIRE(list.value().size() == 1);
  REQUIRE(list.value()[0].number == QStringLiteral("БО-from-backup"));
  const Result<CompanyProfile> company = restored.value().company().load();
  REQUIRE(company);
  REQUIRE(company.value().name == QStringLiteral("Відновлена фірма"));
  REQUIRE(restored.value().verify());

  removeDbTree(live);
  removeDbTree(source);
  QFile::remove(copy);
}

TEST_CASE("staged restore rejects a damaged file and leaves the live base")
{
  const QString live = QDir::current().absoluteFilePath(QStringLiteral("fin_restore_live2.sqlite"));
  const QString junk = QDir::current().absoluteFilePath(QStringLiteral("fin_restore_junk.sqlite"));
  removeDbTree(live);
  QFile::remove(junk);

  Result<LedgerSession> liveSession = LedgerSession::openFile(live);
  REQUIRE(liveSession);
  Document document = makeDoc(DocumentType::Manual, QDate(2026, 9, 4), QStringLiteral("БО-keep"));
  document.lines.push_back(line(account(liveSession.value(), chart::kCash),
                                account(liveSession.value(), chart::kRevenue), 3333));
  saveAndPost(liveSession.value(), document);

  QFile junkFile(junk);
  REQUIRE(junkFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
  REQUIRE(junkFile.write("not a sqlite database") > 0);
  junkFile.close();

  REQUIRE_FALSE(liveSession.value().stageRestoreFrom(junk));
  REQUIRE_FALSE(QFileInfo::exists(restoreMarkerPath(live)));

  const Result<std::vector<DocumentHeader>> list =
      liveSession.value().documents().list(QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE(list);
  REQUIRE(list.value().size() == 1);
  REQUIRE(list.value()[0].number == QStringLiteral("БО-keep"));

  REQUIRE(applyStagedRestore(live));

  removeDbTree(live);
  QFile::remove(junk);
}

TEST_CASE("default database path uses Qt AppDataLocation Finik/Finik")
{
  const QString oldOrg = QCoreApplication::organizationName();
  const QString oldApp = QCoreApplication::applicationName();
  QCoreApplication::setOrganizationName(QStringLiteral("Finik"));
  QCoreApplication::setApplicationName(QStringLiteral("Finik"));

  const QString file = QDir::fromNativeSeparators(defaultDatabaseFilePath());
  REQUIRE(file.endsWith(QStringLiteral("/Finik/Finik/finik.sqlite")));

  QCoreApplication::setOrganizationName(oldOrg);
  QCoreApplication::setApplicationName(oldApp);
}
