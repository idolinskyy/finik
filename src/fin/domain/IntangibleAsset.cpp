#include "fin/domain/IntangibleAsset.h"

#include "fin/core/Checked.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/DocumentRepository.h"
#include "fin/db/IntangibleAssetRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"

#include <QLatin1String>

namespace fin {
namespace {

bool codeEquals(const QString & code, const char * expected)
{
  return code.compare(QLatin1String(expected), Qt::CaseInsensitive) == 0;
}

} // namespace

Money IntangibleAsset::residual() const
{
  const Result<Money> left = original.minus(accumulated);
  return left ? left.value() : Money::zero();
}

QString intangibleStatusToString(IntangibleStatus status)
{
  switch (status) {
  case IntangibleStatus::InUse:
    return QStringLiteral("in_use");
  case IntangibleStatus::Disposed:
    return QStringLiteral("disposed");
  case IntangibleStatus::Draft:
    return QStringLiteral("draft");
  }
  return QStringLiteral("draft");
}

std::optional<IntangibleStatus> intangibleStatusFromString(const QString & text)
{
  const QString key = text.trimmed().toLower();
  if (key == QLatin1String("draft")) {
    return IntangibleStatus::Draft;
  }
  if (key == QLatin1String("in_use")) {
    return IntangibleStatus::InUse;
  }
  if (key == QLatin1String("disposed")) {
    return IntangibleStatus::Disposed;
  }
  return std::nullopt;
}

QString intangibleStatusLabelUk(IntangibleStatus status)
{
  switch (status) {
  case IntangibleStatus::InUse:
    return QStringLiteral("в експлуатації");
  case IntangibleStatus::Disposed:
    return QStringLiteral("списано");
  case IntangibleStatus::Draft:
    return QStringLiteral("чернетка");
  }
  return QStringLiteral("чернетка");
}

bool isIntangibleAccountCode(const QString & code)
{
  return codeEquals(code, chart::kSoftware) || codeEquals(code, chart::kOtherIntangibles);
}

bool isIaAmortizationAccountCode(const QString & code)
{
  return codeEquals(code, chart::kIaAmortization);
}

bool isIaCorrespondenceCode(const QString & code)
{
  return codeEquals(code, chart::kCapexIa) || codeEquals(code, chart::kSuppliers) ||
         codeEquals(code, chart::kAuxiliary);
}

bool isIntangibleAccount(const Account & account)
{
  return isIntangibleAccountCode(account.code);
}

bool usesIaCommissionForm(DocumentType type)
{
  return type == DocumentType::IaCommission;
}

bool usesIaDepreciationForm(DocumentType type)
{
  return type == DocumentType::IaDepreciation;
}

bool usesIaWriteOffForm(DocumentType type)
{
  return type == DocumentType::IaWriteOff;
}

bool usesIaForm(DocumentType type)
{
  return usesIaCommissionForm(type) || usesIaDepreciationForm(type) || usesIaWriteOffForm(type);
}

Result<Money> iaAmortizationCharge(const IntangibleAsset & asset, int year, int month)
{
  if (asset.status != IntangibleStatus::InUse || !asset.commissionedOn.isValid()) {
    return Result<Money>::ok(Money::zero());
  }
  if (year < 1991 || year > 2100 || month < 1 || month > 12 || asset.usefulLifeMonths <= 0) {
    return Result<Money>::fail(Error::validation(QStringLiteral("некоректний період амортизації")));
  }
  const QDate period(year, month, 1);
  const QDate start(asset.commissionedOn.year(), asset.commissionedOn.month(), 1);
  const QDate firstCharge = start.addMonths(1);
  if (!firstCharge.isValid() || period < firstCharge) {
    return Result<Money>::ok(Money::zero());
  }
  const Result<Money> depreciable = asset.original.minus(asset.salvage);
  if (!depreciable) {
    return Result<Money>::fail(depreciable.error());
  }
  if (!depreciable.value().isPositive()) {
    return Result<Money>::ok(Money::zero());
  }
  const Result<Money> remaining = depreciable.value().minus(asset.accumulated);
  if (!remaining) {
    return Result<Money>::fail(remaining.error());
  }
  if (!remaining.value().isPositive()) {
    return Result<Money>::ok(Money::zero());
  }
  const int index =
      (period.year() - firstCharge.year()) * 12 + (period.month() - firstCharge.month()) + 1;
  const std::int64_t monthly = depreciable.value().cents() / asset.usefulLifeMonths;
  if (monthly <= 0) {
    if (index < asset.usefulLifeMonths) {
      return Result<Money>::ok(Money::zero());
    }
    return Result<Money>::ok(remaining.value());
  }
  if (index >= asset.usefulLifeMonths || monthly >= remaining.value().cents()) {
    return Result<Money>::ok(remaining.value());
  }
  return Result<Money>::ok(Money::fromCents(monthly));
}

Result<std::vector<DocumentLine>> composeIaCommission(const std::vector<Account> & accounts,
                                                      AccountId correspondenceId,
                                                      const std::vector<IntangibleAsset> & assets)
{
  if (!correspondenceId) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("оберіть кореспонденцію введення (154, 631 або 00)")));
  }
  if (assets.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один нематеріальний актив")));
  }
  const AccountId dep = accountIdByCode(accounts, chart::kIaAmortization);
  const AccountId aux = accountIdByCode(accounts, chart::kAuxiliary);
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const IntangibleAsset & asset : assets) {
    ++rowNo;
    if (!asset.id) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: не вибрано картку НМА").arg(rowNo)));
    }
    if (!asset.assetAccountId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: у картки немає рахунку обліку").arg(rowNo)));
    }
    if (!asset.original.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("рядок %1: первісна вартість має бути більшою за нуль").arg(rowNo)));
    }
    DocumentLine cost;
    cost.debitAccountId = asset.assetAccountId;
    cost.creditAccountId = correspondenceId;
    cost.amount = asset.original;
    cost.intangibleId = asset.id;
    cost.description = asset.inventoryNo + QStringLiteral(" ") + asset.name;
    lines.push_back(cost);
    if (!asset.openingAccumulated.isPositive()) {
      continue;
    }
    if (!accountHasCode(accounts, correspondenceId, chart::kAuxiliary)) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("знос на початок можна ввести лише з кореспонденцією 00")));
    }
    if (!dep || !aux) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("у плані рахунків немає 00 або 133")));
    }
    DocumentLine opening;
    opening.debitAccountId = aux;
    opening.creditAccountId = dep;
    opening.amount = asset.openingAccumulated;
    opening.intangibleId = asset.id;
    opening.description = QStringLiteral("знос на початок: ") + cost.description;
    lines.push_back(opening);
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeIaDepreciation(const std::vector<Account> & accounts,
                                                        const std::vector<IntangibleAsset> & assets,
                                                        int year, int month)
{
  const AccountId dep = accountIdByCode(accounts, chart::kIaAmortization);
  if (!dep) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 133")));
  }
  std::vector<DocumentLine> lines;
  for (const IntangibleAsset & asset : assets) {
    const Result<Money> charge = iaAmortizationCharge(asset, year, month);
    if (!charge) {
      return Result<std::vector<DocumentLine>>::fail(charge.error());
    }
    if (!charge.value().isPositive()) {
      continue;
    }
    if (!asset.expenseAccountId) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("%1: немає рахунку витрат амортизації").arg(asset.inventoryNo)));
    }
    DocumentLine line;
    line.debitAccountId = asset.expenseAccountId;
    line.creditAccountId = dep;
    line.amount = charge.value();
    line.intangibleId = asset.id;
    line.description = QStringLiteral("амортизація %1 %2").arg(asset.inventoryNo, asset.name);
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("за цей місяць немає сум до нарахування амортизації")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

namespace {

Result<bool> iaMonthAmortizationPosted(QSqlDatabase db, int year, int month)
{
  if (year < 1991 || year > 2100 || month < 1 || month > 12) {
    return Result<bool>::fail(Error::validation(QStringLiteral("некоректний місяць амортизації")));
  }
  const QDate from(year, month, 1);
  const QDate to = from.addMonths(1).addDays(-1);
  const Result<std::vector<DocumentHeader>> existing =
      DocumentRepository(db).list(from, to, DocumentType::IaDepreciation);
  if (!existing) {
    return Result<bool>::fail(existing.error());
  }
  for (const DocumentHeader & header : existing.value()) {
    if (header.posted) {
      return Result<bool>::ok(true);
    }
  }
  return Result<bool>::ok(false);
}

} // namespace

Result<std::vector<DocumentLine>> composeIaWriteOff(QSqlDatabase db,
                                                    const std::vector<Account> & accounts,
                                                    const std::vector<IntangibleAsset> & assets,
                                                    QDate date)
{
  if (!date.isValid()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("некоректна дата списання НМА")));
  }
  if (assets.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один нематеріальний актив")));
  }
  const AccountId dep = accountIdByCode(accounts, chart::kIaAmortization);
  if (!dep) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 133")));
  }
  const Result<bool> monthPosted = iaMonthAmortizationPosted(db, date.year(), date.month());
  if (!monthPosted) {
    return Result<std::vector<DocumentLine>>::fail(monthPosted.error());
  }
  const bool chargePosted = monthPosted.value();
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const IntangibleAsset & asset : assets) {
    ++rowNo;
    if (!asset.id) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: не вибрано картку НМА").arg(rowNo)));
    }
    if (asset.status != IntangibleStatus::InUse) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: %2 не в експлуатації")
                                .arg(QString::number(rowNo), asset.inventoryNo)));
    }
    if (!asset.assetAccountId || !asset.expenseAccountId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: у картки немає рахунків обліку").arg(rowNo)));
    }
    if (asset.commissionedOn.isValid() && date < asset.commissionedOn) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("рядок %1: дата списання раніше введення в експлуатацію").arg(rowNo)));
    }
    Money extra = Money::zero();
    if (!chargePosted) {
      const Result<Money> charge = iaAmortizationCharge(asset, date.year(), date.month());
      if (!charge) {
        return Result<std::vector<DocumentLine>>::fail(charge.error());
      }
      extra = charge.value();
    }
    const Result<Money> accum = asset.accumulated.plus(extra);
    if (!accum) {
      return Result<std::vector<DocumentLine>>::fail(accum.error());
    }
    const Result<Money> residual = asset.original.minus(accum.value());
    if (!residual) {
      return Result<std::vector<DocumentLine>>::fail(residual.error());
    }
    if (residual.value().isNegative()) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("рядок %1: знос перевищує первісну вартість").arg(rowNo)));
    }
    const QString title = asset.inventoryNo + QStringLiteral(" ") + asset.name;
    if (extra.isPositive()) {
      DocumentLine charge;
      charge.debitAccountId = asset.expenseAccountId;
      charge.creditAccountId = dep;
      charge.amount = extra;
      charge.intangibleId = asset.id;
      charge.description = QStringLiteral("амортизація до списання: ") + title;
      lines.push_back(charge);
    }
    if (accum.value().isPositive()) {
      DocumentLine writeAccum;
      writeAccum.debitAccountId = dep;
      writeAccum.creditAccountId = asset.assetAccountId;
      writeAccum.amount = accum.value();
      writeAccum.intangibleId = asset.id;
      writeAccum.description = QStringLiteral("знос: ") + title;
      lines.push_back(writeAccum);
    }
    if (residual.value().isPositive()) {
      DocumentLine writeRest;
      writeRest.debitAccountId = asset.expenseAccountId;
      writeRest.creditAccountId = asset.assetAccountId;
      writeRest.amount = residual.value();
      writeRest.intangibleId = asset.id;
      writeRest.description = QStringLiteral("залишкова: ") + title;
      lines.push_back(writeRest);
    }
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("немає сум для списання НМА")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<Document> buildIaDepreciationDocument(QSqlDatabase db, int year, int month)
{
  if (year < 1991 || year > 2100 || month < 1 || month > 12) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("некоректний місяць амортизації")));
  }
  const QDate from(year, month, 1);
  const QDate to = from.addMonths(1).addDays(-1);
  DocumentRepository documents(db);
  const Result<std::vector<DocumentHeader>> existing =
      documents.list(from, to, DocumentType::IaDepreciation);
  if (!existing) {
    return Result<Document>::fail(existing.error());
  }
  for (const DocumentHeader & header : existing.value()) {
    if (header.posted) {
      return Result<Document>::fail(
          Error::conflict(QStringLiteral("амортизацію за %1 уже проведено (%2)")
                              .arg(to.toString(QStringLiteral("MM.yyyy")), header.number)));
    }
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db).listPostable();
  if (!accounts) {
    return Result<Document>::fail(accounts.error());
  }
  const Result<std::vector<IntangibleAsset>> assets = IntangibleAssetRepository(db).listInUse();
  if (!assets) {
    return Result<Document>::fail(assets.error());
  }
  const Result<std::vector<DocumentLine>> lines =
      composeIaDepreciation(accounts.value(), assets.value(), year, month);
  if (!lines) {
    return Result<Document>::fail(lines.error());
  }
  Document document;
  document.type = DocumentType::IaDepreciation;
  document.date = to;
  document.comment =
      QStringLiteral("амортизація за %1").arg(to.toString(QStringLiteral("MM.yyyy")));
  document.lines = lines.value();
  if (!existing.value().empty()) {
    const Result<Document> draft = documents.findById(existing.value().front().id);
    if (!draft) {
      return Result<Document>::fail(draft.error());
    }
    Document updated = draft.value();
    updated.date = to;
    updated.comment = document.comment;
    updated.lines = document.lines;
    return Result<Document>::ok(std::move(updated));
  }
  const Result<QString> number = documents.allocateNumber(DocumentType::IaDepreciation);
  if (!number) {
    return Result<Document>::fail(number.error());
  }
  document.number = number.value();
  return Result<Document>::ok(std::move(document));
}

} // namespace fin
