#include "fin/session/LedgerSession.h"

#include "fin/auth/Access.h"
#include "fin/db/Restore.h"
#include "fin/integrity/IntegrityChecker.h"

#include <QFile>
#include <utility>

namespace fin {

LedgerSession::LedgerSession(Database db)
    : db_(std::move(db)), accounts_(db_.handle()), counterparties_(db_.handle()),
      warehouses_(db_.handle()), contracts_(db_.handle()), currencies_(db_.handle()),
      bom_(db_.handle()), documents_(db_.handle()), nomenclature_(db_.handle()),
      fixedAssets_(db_.handle()), intangibles_(db_.handle()), lowValue_(db_.handle()),
      employees_(db_.handle()), hrOrders_(db_.handle()), timesheets_(db_.handle()),
      subconto_(db_.handle()), ledger_(db_.handle()), inventory_(db_.handle()),
      period_(db_.handle()), company_(db_.handle()), posting_(db_.handle()),
      trialBalance_(db_.handle()), accountCard_(db_.handle()), counterpartyTurnover_(db_.handle()),
      subcontoTrialBalance_(db_.handle()), accountAnalysis_(db_.handle()), stock_(db_.handle()),
      cashBook_(db_.handle())
{
}

Result<LedgerSession> LedgerSession::openFile(const QString & path)
{
  Result<Database> db = Database::openFile(path);
  if (!db) {
    return Result<LedgerSession>::fail(db.error());
  }
  LedgerSession session(std::move(db.value()));
  Status seed = session.accounts_.seedChartIfEmpty();
  if (!seed) {
    return Result<LedgerSession>::fail(seed.error());
  }
  Status verified = session.verify();
  if (!verified) {
    return Result<LedgerSession>::fail(verified.error());
  }
  return Result<LedgerSession>::ok(std::move(session));
}

Result<LedgerSession> LedgerSession::openMemory()
{
  Result<Database> db = Database::openMemory();
  if (!db) {
    return Result<LedgerSession>::fail(db.error());
  }
  LedgerSession session(std::move(db.value()));
  Status seed = session.accounts_.seedChartIfEmpty();
  if (!seed) {
    return Result<LedgerSession>::fail(seed.error());
  }
  Status verified = session.verify();
  if (!verified) {
    return Result<LedgerSession>::fail(verified.error());
  }
  return Result<LedgerSession>::ok(std::move(session));
}

Status LedgerSession::verify() const
{
  return IntegrityChecker(db_.handle()).verifyEngine();
}

Result<QString> LedgerSession::backupTo(const QString & destinationPath) const
{
  Status access = Access::requireBackup(db_.handle());
  if (!access) {
    return Result<QString>::fail(access.error());
  }
  return db_.backupTo(destinationPath);
}

Status LedgerSession::stageRestoreFrom(const QString & backupPath) const
{
  Status access = Access::requireRestore(db_.handle());
  if (!access) {
    return access;
  }
  const QString live = db_.databaseName();
  const QString safety = restoreSafetyPath(live);
  QFile::remove(safety);
  const Result<QString> snapshot = db_.backupTo(safety);
  if (!snapshot) {
    return snapshot.asStatus();
  }
  const Status staged = stageRestoreFromBackup(live, backupPath, snapshot.value());
  if (!staged) {
    QFile::remove(safety);
  }
  return staged;
}

} // namespace fin
