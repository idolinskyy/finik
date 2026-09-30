#include "fin/bank/MonobankClient.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace fin {

MonobankClient::MonobankClient(QObject * parent) : QObject(parent) {}

Result<StatementParse> MonobankClient::parseJson(const QByteArray & jsonData)
{
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &err);
  if (err.error != QJsonParseError::NoError) {
    return Result<StatementParse>::fail(Error::validation(
        QStringLiteral("Помилка парсингу відповіді Monobank: %1").arg(err.errorString())));
  }

  if (!doc.isArray()) {
    if (doc.isObject()) {
      QJsonObject obj = doc.object();
      if (obj.contains(QStringLiteral("errorDescription"))) {
        return Result<StatementParse>::fail(
            Error::validation(QStringLiteral("Monobank API: %1")
                                  .arg(obj.value(QStringLiteral("errorDescription")).toString())));
      }
    }
    return Result<StatementParse>::fail(Error::validation(
        QStringLiteral("Неочікуваний формат відповіді Monobank (очікувався масив)")));
  }

  const QJsonArray array = doc.array();
  StatementParse parsed;

  for (const QJsonValue & val : array) {
    if (!val.isObject())
      continue;
    QJsonObject obj = val.toObject();

    StatementRow row;
    qint64 timeSec = obj.value(QStringLiteral("time")).toVariant().toLongLong();
    row.date = QDateTime::fromSecsSinceEpoch(timeSec).date();

    qint64 amountCents = obj.value(QStringLiteral("amount")).toVariant().toLongLong();
    row.incoming = amountCents >= 0;
    row.amount = Money::fromCents(std::abs(amountCents));

    row.purpose = obj.value(QStringLiteral("description")).toString();

    // В API Monobank немає ЄДРПОУ/МФО контрагента для фізичних карток,
    // але ми заповнюємо пусте значення або спеціальне, щоб воно було оброблено `importBankStatement`.
    // Якщо це ФОП-рахунок, там можуть бути поля "counterEdrpou", "counterName", "counterIban".
    if (obj.contains(QStringLiteral("counterEdrpou"))) {
      row.edrpou = obj.value(QStringLiteral("counterEdrpou")).toString();
    }
    if (obj.contains(QStringLiteral("counterName"))) {
      row.partyName = obj.value(QStringLiteral("counterName")).toString();
    }

    // Щоб рядок не відкидався через пустий ЄДРПОУ, задамо дефолтний для фізосіб (наприклад 00000000)
    if (row.edrpou.trimmed().isEmpty()) {
      row.edrpou = QStringLiteral("00000000"); // Заглушка для карток фізосіб
      if (row.partyName.trimmed().isEmpty()) {
        row.partyName = QStringLiteral("Клієнт Monobank");
      }
    }

    parsed.rows.push_back(row);
  }

  return Result<StatementParse>::ok(std::move(parsed));
}

void MonobankClient::fetchStatement(const QString & token, QDate from, QDate to,
                                    std::function<void(Result<StatementParse>)> onComplete)
{
  if (token.trimmed().isEmpty()) {
    QMetaObject::invokeMethod(
        this,
        [onComplete]() {
          onComplete(Result<StatementParse>::fail(
              Error::validation(QStringLiteral("Не вказано токен доступу Monobank"))));
        },
        Qt::QueuedConnection);
    return;
  }

  qint64 fromSec = QDateTime(from.startOfDay()).toSecsSinceEpoch();
  qint64 toSec = QDateTime(to.endOfDay()).toSecsSinceEpoch();

  // Рахунок 0 - це рахунок за замовчуванням
  QUrl url(QStringLiteral("https://api.monobank.ua/personal/statement/0/%1/%2")
               .arg(QString::number(fromSec), QString::number(toSec)));

  QNetworkRequest req(url);
  req.setRawHeader("X-Token", token.toUtf8());

  QNetworkReply * reply = net_.get(req);
  connect(reply, &QNetworkReply::finished, this, [reply, onComplete]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      onComplete(Result<StatementParse>::fail(Error::validation(
          QStringLiteral("Помилка мережі при запиті до Monobank: %1").arg(reply->errorString()))));
      return;
    }
    onComplete(parseJson(reply->readAll()));
  });
}

Result<StatementParse> MonobankClient::fetchStatementSync(const QString & token, QDate from,
                                                          QDate to, int timeoutMs)
{
  if (token.trimmed().isEmpty()) {
    return Result<StatementParse>::fail(
        Error::validation(QStringLiteral("Не вказано токен доступу Monobank")));
  }

  qint64 fromSec = QDateTime(from.startOfDay()).toSecsSinceEpoch();
  qint64 toSec = QDateTime(to.endOfDay()).toSecsSinceEpoch();

  QUrl url(QStringLiteral("https://api.monobank.ua/personal/statement/0/%1/%2")
               .arg(QString::number(fromSec), QString::number(toSec)));

  QNetworkRequest req(url);
  req.setRawHeader("X-Token", token.toUtf8());

  QNetworkReply * reply = net_.get(req);
  QEventLoop loop;
  connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  QTimer timer;
  timer.setSingleShot(true);
  connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(timeoutMs);

  loop.exec();

  if (timer.isActive()) {
    timer.stop();
  } else {
    reply->abort();
    reply->deleteLater();
    return Result<StatementParse>::fail(
        Error::validation(QStringLiteral("Тайм-аут при запиті до API Monobank")));
  }

  reply->deleteLater();
  if (reply->error() != QNetworkReply::NoError) {
    return Result<StatementParse>::fail(Error::validation(
        QStringLiteral("Помилка мережі при запиті до Monobank: %1").arg(reply->errorString())));
  }

  return parseJson(reply->readAll());
}

} // namespace fin
