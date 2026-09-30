#include "fin/prro/CheckboxClient.h"

#include "fin/core/Sql.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/CompanyRepository.h"
#include "fin/db/DocumentRepository.h"
#include "fin/db/NomenclatureRepository.h"
#include "fin/domain/ChartCodes.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace fin {
namespace {

const char * kApiHost = "https://api.checkbox.ua";

QString paymentTypeLabel(bool cash)
{
  return cash ? QStringLiteral("CASH") : QStringLiteral("CASHLESS");
}

bool isFiscalSaleType(DocumentType type)
{
  return type == DocumentType::Sale || type == DocumentType::ServiceSale ||
         type == DocumentType::SaleReturn;
}

Status authAndShift(CheckboxClient & client, const CheckboxCredentials & creds, QString * tokenOut)
{
  Result<QString> token = client.signInSync(creds);
  if (!token) {
    return token.asStatus();
  }
  Status shift = client.ensureOpenShiftSync(token.value());
  if (!shift) {
    return shift;
  }
  *tokenOut = token.value();
  return Status::ok();
}

} // namespace

CheckboxClient::CheckboxClient(QObject * parent) : QObject(parent) {}

Result<QByteArray> CheckboxClient::buildSellPayload(const QString & receiptUuid,
                                                    const std::vector<CheckboxSellLine> & goods,
                                                    const QString & paymentType, bool isReturn,
                                                    const QString & relatedReceiptId)
{
  if (receiptUuid.trimmed().isEmpty()) {
    return Result<QByteArray>::fail(Error::invalidArgument(QStringLiteral("порожній UUID чека")));
  }
  if (goods.empty()) {
    return Result<QByteArray>::fail(Error::validation(QStringLiteral("немає товарних рядків")));
  }
  QJsonArray goodsArr;
  std::int64_t total = 0;
  int rowNo = 0;
  for (const CheckboxSellLine & line : goods) {
    ++rowNo;
    if (line.name.trimmed().isEmpty()) {
      return Result<QByteArray>::fail(
          Error::validation(QStringLiteral("рядок %1: порожня назва").arg(rowNo)));
    }
    if (line.unitPriceCents <= 0 || line.quantityMilli <= 0) {
      return Result<QByteArray>::fail(
          Error::validation(QStringLiteral("рядок %1: ціна/кількість").arg(rowNo)));
    }
    const std::int64_t lineSum = (line.unitPriceCents * line.quantityMilli + 500) / 1000;
    total += lineSum;

    QJsonObject good;
    good.insert(QStringLiteral("code"),
                line.code.trimmed().isEmpty() ? QString::number(rowNo) : line.code.trimmed());
    good.insert(QStringLiteral("name"), line.name.trimmed());
    good.insert(QStringLiteral("price"), static_cast<double>(line.unitPriceCents));
    good.insert(QStringLiteral("tax"), QJsonArray());

    QJsonObject item;
    item.insert(QStringLiteral("good"), good);
    item.insert(QStringLiteral("quantity"), static_cast<double>(line.quantityMilli));
    if (isReturn) {
      item.insert(QStringLiteral("is_return"), true);
    }
    goodsArr.append(item);
  }

  QJsonObject payment;
  payment.insert(QStringLiteral("type"),
                 paymentType.trimmed().isEmpty() ? QStringLiteral("CASH") : paymentType.trimmed());
  payment.insert(QStringLiteral("value"), static_cast<double>(total));

  QJsonObject root;
  root.insert(QStringLiteral("id"), receiptUuid);
  root.insert(QStringLiteral("goods"), goodsArr);
  root.insert(QStringLiteral("payments"), QJsonArray{payment});
  if (isReturn && !relatedReceiptId.trimmed().isEmpty()) {
    root.insert(QStringLiteral("related_receipt_id"), relatedReceiptId.trimmed());
  }

  return Result<QByteArray>::ok(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

Result<CheckboxReceiptInfo> CheckboxClient::parseReceiptResponse(const QByteArray & json)
{
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return Result<CheckboxReceiptInfo>::fail(Error::validation(
        QStringLiteral("Помилка парсингу відповіді Checkbox: %1").arg(err.errorString())));
  }
  const QJsonObject obj = doc.object();
  if (obj.contains(QStringLiteral("message"))) {
    return Result<CheckboxReceiptInfo>::fail(Error::validation(
        QStringLiteral("Checkbox: %1").arg(obj.value(QStringLiteral("message")).toString())));
  }
  if (obj.contains(QStringLiteral("detail"))) {
    const QJsonValue detail = obj.value(QStringLiteral("detail"));
    QString text =
        detail.isString()
            ? detail.toString()
            : QString::fromUtf8(QJsonDocument(detail.toArray()).toJson(QJsonDocument::Compact));
    if (text.isEmpty() && detail.isObject()) {
      text = QString::fromUtf8(QJsonDocument(detail.toObject()).toJson(QJsonDocument::Compact));
    }
    return Result<CheckboxReceiptInfo>::fail(
        Error::validation(QStringLiteral("Checkbox: %1").arg(text)));
  }

  CheckboxReceiptInfo info;
  info.id = obj.value(QStringLiteral("id")).toString();
  info.fiscalCode = obj.value(QStringLiteral("fiscal_code")).toString();
  if (info.fiscalCode.isEmpty()) {
    info.fiscalCode = obj.value(QStringLiteral("fiscal_code")).toVariant().toString();
  }
  info.status = obj.value(QStringLiteral("status")).toString();
  if (info.id.isEmpty() && info.fiscalCode.isEmpty()) {
    return Result<CheckboxReceiptInfo>::fail(
        Error::validation(QStringLiteral("у відповіді Checkbox немає id/fiscal_code")));
  }
  return Result<CheckboxReceiptInfo>::ok(std::move(info));
}

Result<std::vector<CheckboxSellLine>> CheckboxClient::sellLinesFromDocument(
    const Document & document, const std::vector<Account> & accounts,
    const std::function<Result<Nomenclature>(NomenclatureId)> & loadNom)
{
  if (!isFiscalSaleType(document.type)) {
    return Result<std::vector<CheckboxSellLine>>::fail(
        Error::validation(QStringLiteral("ПРРО: тип документа не підтримується")));
  }
  const std::vector<GoodsLine> goods = extractGoodsLines(document.type, document.lines, accounts);
  std::vector<CheckboxSellLine> out;
  int rowNo = 0;
  for (const GoodsLine & g : goods) {
    ++rowNo;
    if (!g.nomenclatureId) {
      continue;
    }
    Result<Nomenclature> nom = loadNom(g.nomenclatureId);
    if (!nom) {
      return Result<std::vector<CheckboxSellLine>>::fail(nom.error());
    }
    CheckboxSellLine line;
    line.code = nom.value().sku.trimmed();
    if (line.code.isEmpty()) {
      line.code = QString::number(nom.value().id.value);
    }
    line.name = nom.value().name.trimmed();
    if (line.name.isEmpty()) {
      line.name = g.description.trimmed();
    }
    line.quantityMilli = g.quantity.milli();
    if (line.quantityMilli < 0) {
      line.quantityMilli = -line.quantityMilli;
    }
    if (g.unitPrice.isPositive()) {
      line.unitPriceCents = g.unitPrice.cents();
    } else if (g.quantity.isPositive() || g.quantity.milli() != 0) {
      const std::int64_t milli = g.quantity.milli() < 0 ? -g.quantity.milli() : g.quantity.milli();
      if (milli > 0) {
        const std::int64_t amt = g.amount.cents() < 0 ? -g.amount.cents() : g.amount.cents();
        line.unitPriceCents = (amt * 1000 + milli / 2) / milli;
      }
    }
    if (line.unitPriceCents <= 0 || line.quantityMilli <= 0) {
      return Result<std::vector<CheckboxSellLine>>::fail(Error::validation(
          QStringLiteral("рядок %1: некоректна ціна/кількість для ПРРО").arg(rowNo)));
    }
    out.push_back(line);
  }
  if (out.empty()) {
    return Result<std::vector<CheckboxSellLine>>::fail(
        Error::validation(QStringLiteral("немає товарних рядків для фіскального чека")));
  }
  return Result<std::vector<CheckboxSellLine>>::ok(std::move(out));
}

Result<QByteArray> CheckboxClient::requestSync(const QString & method, const QUrl & url,
                                               const QByteArray & body,
                                               const QByteArray & authBearer,
                                               const QByteArray & licenseKey, int timeoutMs)
{
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  req.setRawHeader("Accept", "application/json");
  req.setRawHeader("X-Client-Name", "Finik");
  req.setRawHeader("X-Client-Version", "0.1");
  if (!licenseKey.isEmpty()) {
    req.setRawHeader("X-License-Key", licenseKey);
  }
  if (!authBearer.isEmpty()) {
    req.setRawHeader("Authorization", QByteArray("Bearer ") + authBearer);
  }

  QNetworkReply * reply = nullptr;
  if (method == QLatin1String("GET")) {
    reply = net_.get(req);
  } else if (method == QLatin1String("POST")) {
    reply = net_.post(req, body);
  } else {
    return Result<QByteArray>::fail(Error::invalidArgument(QStringLiteral("метод HTTP")));
  }

  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  timer.start(timeoutMs);
  loop.exec();

  if (!reply->isFinished()) {
    reply->abort();
    reply->deleteLater();
    return Result<QByteArray>::fail(Error::validation(QStringLiteral("тайм-аут Checkbox API")));
  }
  const QByteArray data = reply->readAll();
  const auto err = reply->error();
  const QString errStr = reply->errorString();
  const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  reply->deleteLater();

  if (err != QNetworkReply::NoError && data.isEmpty()) {
    return Result<QByteArray>::fail(
        Error::validation(QStringLiteral("мережа Checkbox: %1").arg(errStr)));
  }
  if (http == 404) {
    return Result<QByteArray>::fail(Error::notFound(QStringLiteral("Checkbox HTTP 404")));
  }
  if (http >= 400) {
    Result<CheckboxReceiptInfo> parsed = parseReceiptResponse(data);
    if (!parsed) {
      return Result<QByteArray>::fail(parsed.error());
    }
    return Result<QByteArray>::fail(
        Error::validation(QStringLiteral("Checkbox HTTP %1").arg(http)));
  }
  return Result<QByteArray>::ok(data);
}

Result<QString> CheckboxClient::signInSync(const CheckboxCredentials & creds, int timeoutMs)
{
  if (creds.licenseKey.trimmed().isEmpty() || creds.login.trimmed().isEmpty() ||
      creds.pin.isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("ПРРО: вкажіть license key, логін і PIN касира")));
  }
  QJsonObject body;
  body.insert(QStringLiteral("login"), creds.login.trimmed());
  body.insert(QStringLiteral("password"), creds.pin);
  const Result<QByteArray> raw =
      requestSync(QStringLiteral("POST"),
                  QUrl(QStringLiteral("%1/api/v1/cashier/signin").arg(QLatin1String(kApiHost))),
                  QJsonDocument(body).toJson(QJsonDocument::Compact), {},
                  creds.licenseKey.trimmed().toUtf8(), timeoutMs);
  if (!raw) {
    return Result<QString>::fail(raw.error());
  }
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(raw.value(), &err);
  if (!doc.isObject()) {
    return Result<QString>::fail(Error::validation(QStringLiteral("відповідь signin Checkbox")));
  }
  const QString token = doc.object().value(QStringLiteral("access_token")).toString();
  if (token.isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("Checkbox не повернув access_token")));
  }
  return Result<QString>::ok(token);
}

Status CheckboxClient::ensureOpenShiftSync(const QString & accessToken, int timeoutMs)
{
  const Result<QByteArray> cur =
      requestSync(QStringLiteral("GET"),
                  QUrl(QStringLiteral("%1/api/v1/cashier/shift").arg(QLatin1String(kApiHost))), {},
                  accessToken.toUtf8(), {}, timeoutMs);
  if (cur) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(cur.value(), &err);
    if (doc.isObject()) {
      const QString status = doc.object().value(QStringLiteral("status")).toString();
      if (status.compare(QStringLiteral("OPENED"), Qt::CaseInsensitive) == 0 ||
          status.compare(QStringLiteral("OPEN"), Qt::CaseInsensitive) == 0) {
        return Status::ok();
      }
    }
  } else if (cur.error().code != ErrorCode::NotFound) {
    // Транспорт / 401 / 5xx — не маскуємо спробою відкрити зміну.
    return cur.asStatus();
  }
  const Result<QByteArray> opened = requestSync(
      QStringLiteral("POST"), QUrl(QStringLiteral("%1/api/v1/shifts").arg(QLatin1String(kApiHost))),
      QByteArrayLiteral("{}"), accessToken.toUtf8(), {}, timeoutMs);
  if (!opened) {
    return opened.asStatus();
  }
  return Status::ok();
}

Result<CheckboxReceiptInfo> CheckboxClient::createSellReceiptSync(const QString & accessToken,
                                                                  const QByteArray & sellJson,
                                                                  int timeoutMs)
{
  const Result<QByteArray> raw =
      requestSync(QStringLiteral("POST"),
                  QUrl(QStringLiteral("%1/api/v1/receipts/sell").arg(QLatin1String(kApiHost))),
                  sellJson, accessToken.toUtf8(), {}, timeoutMs);
  if (!raw) {
    return Result<CheckboxReceiptInfo>::fail(raw.error());
  }
  return parseReceiptResponse(raw.value());
}

Status CheckboxClient::cancelReceiptSync(const QString & accessToken, const QString & receiptId,
                                         int timeoutMs)
{
  if (receiptId.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("порожній id чека для анулювання")));
  }
  const QUrl url(QStringLiteral("%1/api/v1/receipts/%2/cancel")
                     .arg(QLatin1String(kApiHost), receiptId.trimmed()));
  const Result<QByteArray> raw = requestSync(QStringLiteral("POST"), url, QByteArrayLiteral("{}"),
                                             accessToken.toUtf8(), {}, timeoutMs);
  if (!raw) {
    return raw.asStatus();
  }
  return Status::ok();
}

Status tryFiscalizeSaleDocument(QSqlDatabase db, DocumentId id)
{
  CompanyRepository companies(db);
  const Result<CompanyProfile> company = companies.load();
  if (!company) {
    return company.asStatus();
  }
  if (!company.value().prroEnabled) {
    return Status::ok();
  }
  if (company.value().checkboxLicenseKey.trimmed().isEmpty() ||
      company.value().checkboxCashierLogin.trimmed().isEmpty() ||
      company.value().checkboxCashierPin.isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral(
        "ПРРО увімкнено, але не заповнені реквізити Checkbox у картці підприємства")));
  }

  DocumentRepository docs(db);
  Result<Document> document = docs.findById(id);
  if (!document) {
    return document.asStatus();
  }
  if (!document.value().posted) {
    return Status::fail(Error::validation(QStringLiteral("документ не проведено")));
  }
  if (!document.value().fiscalCode.trimmed().isEmpty()) {
    return Status::ok();
  }
  if (!isFiscalSaleType(document.value().type)) {
    return Status::ok();
  }

  AccountRepository accountsRepo(db);
  Result<std::vector<Account>> accounts = accountsRepo.listAll();
  if (!accounts) {
    return accounts.asStatus();
  }
  NomenclatureRepository noms(db);
  auto loadNom = [&noms](NomenclatureId nid) { return noms.findById(nid); };

  Result<std::vector<CheckboxSellLine>> lines =
      CheckboxClient::sellLinesFromDocument(document.value(), accounts.value(), loadNom);
  if (!lines) {
    return lines.asStatus();
  }

  const bool isReturn = document.value().type == DocumentType::SaleReturn;
  QString relatedReceiptId;
  if (isReturn && document.value().basedOnId) {
    Result<Document> parent = docs.findById(document.value().basedOnId);
    if (parent && !parent.value().fiscalReceiptId.trimmed().isEmpty()) {
      relatedReceiptId = parent.value().fiscalReceiptId.trimmed();
    }
  }

  const QString receiptUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
  Result<QByteArray> payload = CheckboxClient::buildSellPayload(
      receiptUuid, lines.value(), paymentTypeLabel(company.value().prroPaymentCash), isReturn,
      relatedReceiptId);
  if (!payload) {
    return payload.asStatus();
  }

  CheckboxCredentials creds;
  creds.licenseKey = company.value().checkboxLicenseKey;
  creds.login = company.value().checkboxCashierLogin;
  creds.pin = company.value().checkboxCashierPin;

  CheckboxClient client;
  QString token;
  Status auth = authAndShift(client, creds, &token);
  if (!auth) {
    return auth;
  }
  Result<CheckboxReceiptInfo> receipt = client.createSellReceiptSync(token, payload.value());
  if (!receipt) {
    return receipt.asStatus();
  }

  QString fiscal = receipt.value().fiscalCode;
  if (fiscal.isEmpty()) {
    fiscal = receipt.value().id;
  }
  return docs.setFiscalPayload(id, receipt.value().id, fiscal, receipt.value().status);
}

Status tryVoidFiscalReceipt(QSqlDatabase db, DocumentId id)
{
  CompanyRepository companies(db);
  const Result<CompanyProfile> company = companies.load();
  if (!company) {
    return company.asStatus();
  }

  DocumentRepository docs(db);
  Result<Document> document = docs.findById(id);
  if (!document) {
    return document.asStatus();
  }
  if (document.value().fiscalCode.trimmed().isEmpty() &&
      document.value().fiscalReceiptId.trimmed().isEmpty()) {
    return Status::ok();
  }
  if (!company.value().prroEnabled) {
    return Status::fail(Error::validation(
        QStringLiteral("Документ фіскалізовано, але ПРРО вимкнено — увімкніть Checkbox "
                       "або очистіть фіскальні поля вручну")));
  }
  if (company.value().checkboxLicenseKey.trimmed().isEmpty() ||
      company.value().checkboxCashierLogin.trimmed().isEmpty() ||
      company.value().checkboxCashierPin.isEmpty()) {
    return Status::fail(Error::validation(
        QStringLiteral("Для анулювання чека потрібні реквізити Checkbox у картці підприємства")));
  }

  const QString receiptId = document.value().fiscalReceiptId.trimmed().isEmpty()
                                ? document.value().fiscalCode.trimmed()
                                : document.value().fiscalReceiptId.trimmed();

  CheckboxCredentials creds;
  creds.licenseKey = company.value().checkboxLicenseKey;
  creds.login = company.value().checkboxCashierLogin;
  creds.pin = company.value().checkboxCashierPin;

  CheckboxClient client;
  QString token;
  Status auth = authAndShift(client, creds, &token);
  if (!auth) {
    return auth;
  }
  Status cancelled = client.cancelReceiptSync(token, receiptId);
  if (!cancelled) {
    return Status::fail(Error::validation(
        QStringLiteral("%1. Checkbox дозволяє анулювати лише останній чек поточної зміни; "
                       "інакше оформіть «Повернення від покупця» (чек з is_return).")
            .arg(cancelled.message())));
  }
  return docs.setFiscalPayload(id, QString(), QString(), QStringLiteral("CANCELLED"));
}

} // namespace fin
