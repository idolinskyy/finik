#pragma once

// Імпорт банківських виписок (CSV / XML / Monobank).

#include "fin/core/Money.h"
#include "fin/core/Result.h"
#include "fin/domain/Document.h"

#include <vector>

#include <QDate>
#include <QString>

namespace fin {

// Один розпарсений рядок банківської виписки.
struct StatementRow
{
  QDate date;
  Money amount = Money::zero();
  bool incoming = true; // true = надходження (Дт 311), false = списання (Кт 311)
  QString purpose;      // призначення платежу з виписки
  QString edrpou; // ЄДРПОУ/ІПН контрагента (для автозіставлення)
  QString partyName; // найменування контрагента
  QString error; // опис помилки парсингу; порожній = рядок коректний
  bool duplicate = false; // рядок вже є в журналі
};

struct StatementParse
{
  std::vector<StatementRow> rows;
};

// Розбирає текст виписки у структурований список рядків.
// fileName вказується для вибору правильного парсера за розширенням файлу
// (наприклад, .csv від ПриватБанку vs .txt від Ощадбанку).
Result<StatementParse> parseBankStatement(const QString & text, const QString & fileName);

// Для кожного рядка шукає контрагента в БД за ЄДРПОУ та визначає
// бухгалтерську кореспонденцію. Змінює parsed у місці.
void classifyBankStatement(class LedgerSession & session, StatementParse & parsed);

// Зберігає підтверджені рядки (без помилок і не-дублікати) у БД
// як окремі документи типу BankPayment.
// Повертає кількість успішно імпортованих рядків.
Result<int> importBankStatement(class LedgerSession & session, StatementParse & parsed);

} // namespace fin
