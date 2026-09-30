#pragma once

// Клієнт курсів НБУ.

#include "fin/core/Result.h"
#include "fin/domain/Currency.h"

#include <functional>
#include <vector>

#include <QByteArray>
#include <QDate>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlDatabase>

namespace fin {

class NbuRateClient : public QObject
{
  Q_OBJECT

 public:
  explicit NbuRateClient(QObject * parent = nullptr);
  ~NbuRateClient() override = default;

  // Парсинг масиву курсів валют з JSON-відповіді API НБУ
  static Result<std::vector<CurrencyRate>> parseNbuJson(const QByteArray & jsonData,
                                                        QDate fallbackDate = QDate::currentDate());

  static QUrl buildUrl(QDate date);

  // Асинхронне завантаження курсів на дату
  void fetchRates(QDate date, std::function<void(Result<std::vector<CurrencyRate>>)> onComplete);

  // Синхронне завантаження курсів з тайм-аутом
  Result<std::vector<CurrencyRate>> fetchRatesSync(QDate date, int timeoutMs = 5000);

  // Фонове завантаження та автоматичне збереження курсів у базу даних
  void syncRatesToRepository(QSqlDatabase db, QDate date,
                             std::function<void(Status)> onComplete = nullptr);

 private:
  QNetworkAccessManager net_;
};

} // namespace fin
