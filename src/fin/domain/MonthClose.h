#pragma once

// Закриття місяця / фінрезультат (7/9 → 79).

#include "fin/core/Result.h"
#include "fin/domain/Document.h"

#include <QDate>
#include <QSqlDatabase>

namespace fin {

// Формує проводки закриття 701/703/71/902/92 -> 79 -> 44 за вказаний діапазон дат
Result<std::vector<DocumentLine>> composePeriodResult(QSqlDatabase db, QDate from, QDate to);

// Перевіряє, що всі рахунки класів 7 та 9 закриті до нуля перед фіксацією періоду
Status requireIncomeExpenseClosed(QSqlDatabase db, QDate through);

// Створює документ закриття фінансового результату за вказаний місяць
Result<Document> buildPeriodResultDocument(QSqlDatabase db, int year, int month);

} // namespace fin
