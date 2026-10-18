#pragma once

// Залік авансів (371/681) при купівлі/продажу.

#include "fin/core/Result.h"
#include "fin/domain/Account.h"
#include "fin/domain/Document.h"

#include <vector>

#include <QSqlDatabase>

namespace fin {

// Повертає true, якщо рядок є автоматично згенерованим рядком заліку авансу
// (а не введеним користувачем вручну).
bool isAdvanceOffsetLine(const std::vector<Account> & accounts, DocumentType type,
                         const DocumentLine & line);

// Обчислює суму поточного заліку авансу за рядками документа.
// Використовується для попереднього відображення суми в UI.
Money advanceOffsetAmount(const std::vector<Account> & accounts, DocumentType type,
                          const std::vector<DocumentLine> & lines);

// Видаляє з document.lines всі рядки заліку авансів перед повторним проведенням.
// Викликається і при post(), і при unpost() для відновлення «чистого» стану рядків.
void stripAdvanceOffsetLines(const std::vector<Account> & accounts, Document & document);

// Обчислює й додає рядки заліку авансів до document.lines перед записом у БД.
// Читає поточне сальдо 681 (отримані аванси) або 371 (видані аванси) з БД,
// порівнює з сумою документа і формує рядок на мінімум із двох сум.
Status applyAdvanceOffset(QSqlDatabase db, const std::vector<Account> & accounts,
                          Document & document);

} // namespace fin
