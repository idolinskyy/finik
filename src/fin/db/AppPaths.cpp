#include "fin/db/AppPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace fin {

QString defaultDatabaseFilePath()
{
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         QStringLiteral("/finik.sqlite");
}

QString resolveDefaultDatabasePath()
{
  const QString nested = defaultDatabaseFilePath();
  if (QFileInfo::exists(nested)) {
    return nested;
  }

  // Короткий експеримент клав файл у ~/.local/share/Finik/ (без другого Finik).
  QDir parent(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
  if (parent.cdUp()) {
    const QString flat = parent.filePath(QStringLiteral("finik.sqlite"));
    if (QFileInfo::exists(flat)) {
      return flat;
    }
  }

  const QString generic = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  const QString ancient = QDir(generic).filePath(QStringLiteral("fin/fin/fin.sqlite"));
  if (QFileInfo::exists(ancient)) {
    return ancient;
  }
  return nested;
}

QString secretKeyFilePath()
{
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         QStringLiteral("/finik-secrets.key");
}

} // namespace fin
