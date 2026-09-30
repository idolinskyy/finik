#pragma once

// Читання/запис ledger_entries.

#include "fin/core/Result.h"
#include "fin/domain/LedgerEntry.h"

#include <vector>

#include <QSqlDatabase>

namespace fin {

class LedgerRepository
{
 public:
  explicit LedgerRepository(QSqlDatabase db);

  // Замінює всі проводки вказаного документа новими валідованими записами
  Status replaceForDocument(DocumentId documentId, const std::vector<LedgerEntry> & entries);

  // Видаляє всі проводки документа (при знятті з проведення / unpost)
  Status removeForDocument(DocumentId documentId);

  // Отримує список сформованих проводок Головної книги для конкретного документа
  Result<std::vector<LedgerEntry>> listForDocument(DocumentId documentId) const;

  // Повертає сальдо по рахунку станом на вказану дату (включно)
  Result<Money> balance(AccountId accountId, const QDate & date) const;

 private:
  QSqlDatabase db_;
};

} // namespace fin
