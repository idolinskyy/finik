#pragma once

// Рядок Головної книги (Дт/Кт, сума в копійках, аналітика).

#include "fin/core/Ids.h"
#include "fin/core/Money.h"
#include "fin/domain/Subconto.h"

#include <QDate>
#include <QString>

namespace fin {

struct LedgerEntry
{
  LedgerEntryId id;
  DocumentId documentId; // первинний документ, що породив цей запис
  QDate date; // дата проводки (може ≠ даті документа для ПН)
  AccountId debitAccountId;
  AccountId creditAccountId;
  Money amount = Money::zero();
  CounterpartyId counterpartyId; // сумісність / швидка фільтрація в картці рахунку
  SubcontoSet debitSubconto; // багатовимірна аналітика дебету (до 3 субконто)
  SubcontoSet creditSubconto; // багатовимірна аналітика кредиту (до 3 субконто)
  QString currencyCode; // код валюти ("USD", "EUR"; порожній = "UAH")
  Money currencyAmount = Money::zero(); // сума в інвалюті (центи інвалюти)
  std::int64_t currencyRateMicros = 0;  // курс НБУ в мікрогривнях
  QString description; // береться з рядка документа або коментаря заголовку
};

} // namespace fin
