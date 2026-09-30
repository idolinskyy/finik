#pragma once

// Типовий файл SQLite: Qt AppDataLocation = organization/application (Finik/Finik).

#include <QString>

namespace fin {

QString defaultDatabaseFilePath();
// Указаний шлях, або наявний файл зі старішого розташування.
QString resolveDefaultDatabasePath();

} // namespace fin
