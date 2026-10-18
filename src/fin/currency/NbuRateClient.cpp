#include "fin/currency/NbuRateClient.h"

#include "fin/core/Checked.h"
#include "fin/db/CurrencyRepository.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace fin {
namespace {

Result<std::int64_t> microsFromDecimalRate(QStringView text)
{
  const QStringView trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return Result<std::int64_t>::fail(Error::validation(QStringLiteral("порожній курс НБУ")));
  }
  const qsizetype dot = trimmed.indexOf(QLatin1Char('.'));
  const QStringView intView = dot < 0 ? trimmed : trimmed.left(dot);
  const QStringView fracView = dot < 0 ? QStringView() : trimmed.mid(dot + 1);
  if (intView.isEmpty()) {
    return Result<std::int64_t>::fail(Error::validation(QStringLiteral("некоректний курс НБУ")));
  }
  for (const QChar ch : intView) {
    if (!ch.isDigit()) {
      return Result<std::int64_t>::fail(Error::validation(QStringLiteral("некоректний курс НБУ")));
    }
  }
  for (const QChar ch : fracView) {
    if (!ch.isDigit()) {
      return Result<std::int64_t>::fail(Error::validation(QStringLiteral("некоректний курс НБУ")));
    }
  }
  bool ok = false;
  const qlonglong uah = intView.toLongLong(&ok);
  if (!ok || uah < 0) {
    return Result<std::int64_t>::fail(Error::validation(QStringLiteral("некоректний курс НБУ")));
  }
  QString frac = fracView.left(6).toString();
  while (frac.size() < 6) {
    frac.append(QLatin1Char('0'));
  }
  const qlonglong fracMicros = frac.toLongLong(&ok);
  if (!ok) {
    return Result<std::int64_t>::fail(Error::validation(QStringLiteral("некоректний курс НБУ")));
  }
  std::int64_t micros = 0;
  Status st = addTo(micros, uah, QStringLiteral("курс НБУ"));
  if (!st) {
    return Result<std::int64_t>::fail(st.error());
  }
  for (int i = 0; i < 6; ++i) {
    if (addOverflows(micros, micros)) {
      return Result<std::int64_t>::fail(Error::overflow(QStringLiteral("курс НБУ")));
    }
    micros *= 10;
  }
  st = addTo(micros, fracMicros, QStringLiteral("курс НБУ"));
  if (!st) {
    return Result<std::int64_t>::fail(st.error());
  }
  if (fracView.size() > 6 && fracView.at(6).digitValue() >= 5) {
    st = addTo(micros, 1, QStringLiteral("курс НБУ"));
    if (!st) {
      return Result<std::int64_t>::fail(st.error());
    }
  }
  if (micros <= 0) {
    return Result<std::int64_t>::fail(Error::validation(QStringLiteral("некоректний курс НБУ")));
  }
  return Result<std::int64_t>::ok(micros);
}

} // namespace

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

  (void)doc.array();
  const QString jsonText = QString::fromUtf8(jsonData);
  const QRegularExpression objRe(QStringLiteral("\\{[^{}]*\\}"));
  const QRegularExpression ccRe(QStringLiteral("\"cc\"\\s*:\\s*\"([^\"]+)\""));
  const QRegularExpression rateRe(QStringLiteral("\"rate\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?)"));
  const QRegularExpression dateRe(QStringLiteral("\"exchangedate\"\\s*:\\s*\"([^\"]*)\""));

  std::vector<CurrencyRate> rates;
  auto it = objRe.globalMatch(jsonText);
  while (it.hasNext()) {
    const QString obj = it.next().captured();
    const QRegularExpressionMatch ccMatch = ccRe.match(obj);
    const QRegularExpressionMatch rateMatch = rateRe.match(obj);
    if (!ccMatch.hasMatch() || !rateMatch.hasMatch()) {
      continue;
    }
    const QString code = ccMatch.captured(1).trimmed().toUpper();
    if (code.isEmpty()) {
      continue;
    }
    const Result<std::int64_t> micros = microsFromDecimalRate(rateMatch.capturedView(1));
    if (!micros || micros.value() <= 0) {
      continue;
    }
    QDate rateDate = fallbackDate;
    const QRegularExpressionMatch dateMatch = dateRe.match(obj);
    if (dateMatch.hasMatch()) {
      const QDate parsedDate =
          QDate::fromString(dateMatch.captured(1).trimmed(), QStringLiteral("dd.MM.yyyy"));
      if (parsedDate.isValid()) {
        rateDate = parsedDate;
      }
    }
    CurrencyRate r;
    r.currencyCode = code;
    r.rateDate = rateDate;
    r.nominal = 1;
    r.rateMicros = micros.value();
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
