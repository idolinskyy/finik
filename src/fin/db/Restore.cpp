#include "fin/db/Restore.h"

#include "fin/session/LedgerSession.h"

#include <QFile>
#include <QFileInfo>

namespace fin {
namespace {

void removeSqliteSidecars(const QString & path)
{
  QFile::remove(path);
  QFile::remove(path + QStringLiteral("-wal"));
  QFile::remove(path + QStringLiteral("-shm"));
}

bool copyReplace(const QString & from, const QString & to)
{
  QFile::remove(to);
  return QFile::copy(from, to);
}

} // namespace

QString restoreIncomingPath(const QString & livePath)
{
  return livePath + QStringLiteral(".incoming");
}

QString restoreSafetyPath(const QString & livePath)
{
  return livePath + QStringLiteral(".before-restore");
}

QString restoreMarkerPath(const QString & livePath)
{
  return livePath + QStringLiteral(".restore-pending");
}

Status applyStagedRestore(const QString & livePath)
{
  if (livePath.trimmed().isEmpty() || livePath == QLatin1String(":memory:")) {
    return Status::ok();
  }
  const QString marker = restoreMarkerPath(livePath);
  if (!QFileInfo::exists(marker)) {
    return Status::ok();
  }
  const QString incoming = restoreIncomingPath(livePath);
  if (!QFileInfo::exists(incoming)) {
    QFile::remove(marker);
    return Status::fail(Error::database(QStringLiteral("немає файлу відновлення поруч із базою")));
  }
  removeSqliteSidecars(livePath);
  const QString incomingWal = incoming + QStringLiteral("-wal");
  const QString incomingShm = incoming + QStringLiteral("-shm");
  if (QFile::rename(incoming, livePath)) {
    QFile::rename(incomingWal, livePath + QStringLiteral("-wal"));
    QFile::rename(incomingShm, livePath + QStringLiteral("-shm"));
  } else if (copyReplace(incoming, livePath)) {
    QFile::remove(incoming);
    QFile::remove(incomingWal);
    QFile::remove(incomingShm);
  } else {
    return Status::fail(Error::database(QStringLiteral("не вдалося підставити відновлену базу")));
  }
  QFile::remove(marker);
  return Status::ok();
}

Status stageRestoreFromBackup(const QString & livePath, const QString & backupPath,
                              const QString & safetySnapshotPath)
{
  if (livePath.trimmed().isEmpty() || livePath == QLatin1String(":memory:")) {
    return Status::fail(
        Error::validation(QStringLiteral("відновлення можливе лише для файлової бази")));
  }
  const QFileInfo backup(backupPath);
  if (!backup.exists() || !backup.isFile()) {
    return Status::fail(Error::notFound(QStringLiteral("файл резервної копії не знайдено")));
  }
  if (backup.absoluteFilePath() == QFileInfo(livePath).absoluteFilePath()) {
    return Status::fail(
        Error::invalidArgument(QStringLiteral("не можна відновлювати базу саму в себе")));
  }

  const QString incoming = restoreIncomingPath(livePath);
  QFile::remove(incoming);
  if (!QFile::copy(backup.absoluteFilePath(), incoming)) {
    return Status::fail(Error::database(QStringLiteral("не вдалося скопіювати резервну копію")));
  }

  {
    Result<LedgerSession> check = LedgerSession::openFile(incoming);
    if (!check) {
      QFile::remove(incoming);
      return Status::fail(Error::database(
          QStringLiteral("копія пошкоджена або не є базою fin: %1").arg(check.message())));
    }
    const Status verified = check.value().verify();
    if (!verified) {
      QFile::remove(incoming);
      return verified;
    }
  }

  if (safetySnapshotPath.trimmed().isEmpty() || !QFileInfo::exists(safetySnapshotPath)) {
    QFile::remove(incoming);
    return Status::fail(
        Error::database(QStringLiteral("немає знімка поточної бази перед відновленням")));
  }

  QFile marker(restoreMarkerPath(livePath));
  if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QFile::remove(incoming);
    return Status::fail(Error::database(QStringLiteral("не вдалося позначити відновлення")));
  }
  marker.close();
  return Status::ok();
}

} // namespace fin
