#pragma once

// Перевірки: Дт=Кт, склад↔281, ОЗ/НМА/МШП↔субрахунки.

#include "fin/core/Result.h"

#include <cstdint>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class IntegrityChecker
{
 public:
  explicit IntegrityChecker(QSqlDatabase db);

  // Перевіряє глобальний баланс Головної книги (Σ Дт = Σ Кт).
  // Виконується після кожного post() та unpost().
  Status verifyEngine() const;

  // Перевіряє баланс по окремому документу.
  // Корисна для точнішого повідомлення про помилку (вказує конкретний документ).
  Status verifyPostedDocument(std::int64_t documentId) const;

  // Відхиляє проведення, якщо будь-яка позиція номенклатури має від'ємний залишок.
  // Перевіряється через накопичений підсумок рухів за датою.
  Status rejectNegativeStock() const;

 private:
  // Виконує SQL і перевіряє, що результат не містить рядків.
  // Якщо рядки є — повертає помилку з повідомленням message.
  Status queryHasNoRows(const QString & sql, const QString & message) const;

  QSqlDatabase db_;
};

} // namespace fin
