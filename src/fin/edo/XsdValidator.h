#pragma once

#include "fin/core/Result.h"

#include <QString>
#include <QStringList>

namespace fin {

// Чи зібрано з libxml2 (без нього XSD не перевіряється).
bool xsdValidationAvailable();

// Каталоги пошуку: $FINIK_XSD_DIR, AppData/xsd, поруч із бінарником, vendor/xsd.
QStringList xsdSearchDirectories();

// Повний шлях до basename (наприклад J0200125.xsd) або порожній рядок.
QString findDeclarXsd(const QString & basename);

// Якщо файл XSD знайдено — валідація libxml2; якщо немає файлу — ok (каркас без схеми).
Status validateAgainstXsdIfPresent(const QString & xml, const QString & xsdBasename);

} // namespace fin
