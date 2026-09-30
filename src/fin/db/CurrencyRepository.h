#pragma once

// Валюти та курси в БД.

#include "fin/core/Result.h"
#include "fin/domain/Currency.h"

#include <vector>

#include <QDate>
#include <QSqlDatabase>
#include <QString>

namespace fin {

class CurrencyRepository
{
 public:
  explicit CurrencyRepository(QSqlDatabase db);

  Result<std::vector<Currency>> listAll(bool activeOnly = false) const;
  Result<Currency> findByCode(const QString & code) const;
  Status insert(const Currency & currency);
  Status update(const Currency & currency);
  Status remove(const QString & code);

  // Курс на дату або найближчий попередній; UAH завжди 1:1.
  Result<CurrencyRate> getRate(const QString & currencyCode, QDate date) const;

  // Перевірка наявності курсів у базі на конкретну дату
  bool hasRatesOnDate(QDate date) const;
  bool hasRateOnDate(const QString & currencyCode, QDate date) const;

  Result<std::vector<CurrencyRate>> listRates(const QString & currencyCode, QDate from = {},
                                              QDate to = {}) const;

  // Встановлення або оновлення курсу на дату (INSERT OR REPLACE)
  Status setRate(const CurrencyRate & rate);

  Status removeRate(const QString & currencyCode, QDate date);

 private:
  QSqlDatabase db_;
};

} // namespace fin
