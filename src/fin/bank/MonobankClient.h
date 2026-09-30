#pragma once

#include "fin/core/Result.h"
#include "fin/bank/StatementImport.h"

#include <functional>

#include <QByteArray>
#include <QDate>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

namespace fin {

class MonobankClient : public QObject
{
  Q_OBJECT

 public:
  explicit MonobankClient(QObject * parent = nullptr);
  ~MonobankClient() override = default;

  // Асинхронне завантаження виписки з Monobank
  void fetchStatement(const QString & token, QDate from, QDate to,
                      std::function<void(Result<StatementParse>)> onComplete);

  // Синхронне завантаження виписки з тайм-аутом
  Result<StatementParse> fetchStatementSync(const QString & token, QDate from, QDate to,
                                            int timeoutMs = 10000);

  // Парсинг JSON-відповіді Monobank у StatementParse
  static Result<StatementParse> parseJson(const QByteArray & jsonData);

 private:
  QNetworkAccessManager net_;
};

} // namespace fin
