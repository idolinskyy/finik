#pragma once

// Клієнт Checkbox ПРРО (api.checkbox.ua): каса, зміна, продаж / повернення / анулювання.

#include "fin/core/Result.h"
#include "fin/domain/Document.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Nomenclature.h"

#include <functional>
#include <vector>

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

namespace fin {

struct CheckboxCredentials
{
  QString licenseKey;
  QString login;
  QString pin;
};

struct CheckboxReceiptInfo
{
  QString id;
  QString fiscalCode;
  QString status;
};

struct CheckboxSellLine
{
  QString code;
  QString name;
  std::int64_t unitPriceCents = 0;
  std::int64_t quantityMilli = 0;
};

class CheckboxClient : public QObject
{
  Q_OBJECT

 public:
  explicit CheckboxClient(QObject * parent = nullptr);

  // JSON тіла чека (quantity у тисячних, price у копійках).
  // Повернення: isReturn=true, relatedReceiptId = UUID чека продажу (за наявності).
  static Result<QByteArray> buildSellPayload(const QString & receiptUuid,
                                             const std::vector<CheckboxSellLine> & goods,
                                             const QString & paymentType, bool isReturn = false,
                                             const QString & relatedReceiptId = {});

  static Result<CheckboxReceiptInfo> parseReceiptResponse(const QByteArray & json);

  // Рядки чека з Sale / ServiceSale / SaleReturn.
  static Result<std::vector<CheckboxSellLine>>
  sellLinesFromDocument(const Document & document, const std::vector<Account> & accounts,
                        const std::function<Result<Nomenclature>(NomenclatureId)> & loadNom);

  Result<QString> signInSync(const CheckboxCredentials & creds, int timeoutMs = 15000);
  Status ensureOpenShiftSync(const QString & accessToken, int timeoutMs = 15000);
  Result<CheckboxReceiptInfo> createSellReceiptSync(const QString & accessToken,
                                                    const QByteArray & sellJson,
                                                    int timeoutMs = 20000);
  // Анулювання останнього чека зміни (Checkbox: лише last + DONE + відкрита зміна).
  Status cancelReceiptSync(const QString & accessToken, const QString & receiptId,
                           int timeoutMs = 20000);

 private:
  Result<QByteArray> requestSync(const QString & method, const QUrl & url, const QByteArray & body,
                                 const QByteArray & authBearer, const QByteArray & licenseKey,
                                 int timeoutMs);

  QNetworkAccessManager net_;
};

// Фіскалізує Sale / ServiceSale / SaleReturn, якщо ПРРО увімкнено.
Status tryFiscalizeSaleDocument(QSqlDatabase db, DocumentId id);

// Анулює фіскальний чек у Checkbox і очищає поля документа (перед розпроведенням).
Status tryVoidFiscalReceipt(QSqlDatabase db, DocumentId id);

} // namespace fin
