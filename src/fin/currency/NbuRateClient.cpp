#include "fin/currency/NbuRateClient.h"

#include "fin/db/CurrencyRepository.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace fin {

NbuRateClient::NbuRateClient(QObject * parent) : QObject(parent) {}

QUrl NbuRateClient::buildUrl(QDate date)
{
  QUrl url(QStringLiteral("https://bank.gov.ua/NBUStatService/v1/statdirectory/exchange"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("json"), QString());
  if (date.isValid()) {
    query.addQueryItem(QStringLiteral("date"), date.toString(QStringLiteral("yyyyMMdd")));
  }
  url.setQuery(query);
  return url;
}

Result<std::vector<CurrencyRate>> NbuRateClient::parseNbuJson(const QByteArray & jsonData,
                                                              QDate fallbackDate)
{
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
    return Result<std::vector<CurrencyRate>>::fail(Error::validation(
        QStringLiteral("некоректний JSON від API НБУ: %1").arg(parseError.errorString())));
  }

  const QJsonArray array = doc.array();
  std::vector<CurrencyRate> rates;
  rates.reserve(static_cast<std::size_t>(array.size()));

  for (const QJsonValue & val : array) {
    if (!val.isObject()) {
      continue;
    }
    const QJsonObject obj = val.toObject();
    const QString code = obj.value(QStringLiteral("cc")).toString().trimmed().toUpper();
    if (code.isEmpty()) {
      continue;
    }

    // Парсинг дати з формату "DD.MM.YYYY"
    QDate rateDate = fallbackDate;
    const QString dateStr = obj.value(QStringLiteral("exchangedate")).toString().trimmed();
    if (!dateStr.isEmpty()) {
      const QDate parsedDate = QDate::fromString(dateStr, QStringLiteral("dd.MM.yyyy"));
      if (parsedDate.isValid()) {
        rateDate = parsedDate;
      }
    }

    // Точний парсинг курсу без дрейфу floating-point:
    const double rawRate = obj.value(QStringLiteral("rate")).toDouble();
    if (rawRate <= 0.0) {
      continue;
    }
    const QString rateStr = QString::number(rawRate, 'f', 4);
    const QStringList parts = rateStr.split(QLatin1Char('.'));
    const std::int64_t intPart = parts[0].toLongLong();
    const std::int64_t fracPart =
        parts.size() > 1 ? parts[1].left(4).rightJustified(4, QLatin1Char('0')).toLongLong() : 0;
    const std::int64_t rateMicros = intPart * ExchangeRate::kMicrosPerUah + fracPart * 100;

    CurrencyRate r;
    r.currencyCode = code;
    r.rateDate = rateDate;
    r.nominal = 1;
    r.rateMicros = rateMicros;
    rates.push_back(r);
  }

  return Result<std::vector<CurrencyRate>>::ok(std::move(rates));
}

void NbuRateClient::fetchRates(QDate date,
                               std::function<void(Result<std::vector<CurrencyRate>>)> onComplete)
{
  if (!onComplete) {
    return;
  }
  const QUrl url = buildUrl(date);
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("Finik Accounting/1.0 (Linux/Qt6)"));

  QNetworkReply * reply = net_.get(request);
  const QDate targetDate = date.isValid() ? date : QDate::currentDate();

  connect(reply, &QNetworkReply::finished, this, [reply, targetDate, onComplete]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      onComplete(Result<std::vector<CurrencyRate>>::fail(Error::database(
          QStringLiteral("помилка мережі при запиті до НБУ: %1").arg(reply->errorString()))));
      return;
    }
    const QByteArray data = reply->readAll();
    onComplete(parseNbuJson(data, targetDate));
  });
}

Result<std::vector<CurrencyRate>> NbuRateClient::fetchRatesSync(QDate date, int timeoutMs)
{
  QEventLoop loop;
  Result<std::vector<CurrencyRate>> result =
      Result<std::vector<CurrencyRate>>::fail(Error::database(QStringLiteral("тайм-аут запиту")));

  QTimer timer;
  timer.setSingleShot(true);
  connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

  fetchRates(date, [&loop, &result, &timer](Result<std::vector<CurrencyRate>> res) {
    result = std::move(res);
    timer.stop();
    loop.quit();
  });

  timer.start(timeoutMs > 0 ? timeoutMs : 5000);
  loop.exec();

  return result;
}

void NbuRateClient::syncRatesToRepository(QSqlDatabase db, QDate date,
                                          std::function<void(Status)> onComplete)
{
  fetchRates(date, [db, onComplete](Result<std::vector<CurrencyRate>> res) {
    if (!res) {
      if (onComplete) {
        onComplete(res.asStatus());
      }
      return;
    }
    CurrencyRepository repo(db);
    for (const auto & rate : res.value()) {
      (void)repo.setRate(rate);
    }
    if (onComplete) {
      onComplete(Status::ok());
    }
  });
}

} // namespace fin
