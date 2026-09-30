#pragma once

// Резервне копіювання та поетапне відновлення БД.

#include "fin/core/Result.h"

#include <QString>

namespace fin {

QString restoreIncomingPath(const QString & livePath);
QString restoreSafetyPath(const QString & livePath);
QString restoreMarkerPath(const QString & livePath);

// Застосовує відкладене відновлення, замінюючи файл бази даних перевіреним знімком
Status applyStagedRestore(const QString & livePath);

// Готує відновлення з бекапу, створюючи страхувальну копію поточної бази
Status stageRestoreFromBackup(const QString & livePath, const QString & backupPath,
                              const QString & safetySnapshotPath);

} // namespace fin
