#include "fin/domain/FixedAsset.h"

#include "fin/core/Checked.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/DocumentRepository.h"
#include "fin/db/FixedAssetRepository.h"
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

Money FixedAsset::residual() const
{
  const Result<Money> left = original.minus(accumulated);
  return left ? left.value() : Money::zero();
}

QString fixedAssetStatusToString(FixedAssetStatus status)
{
  switch (status) {
  case FixedAssetStatus::InUse:
    return QStringLiteral("in_use");
  case FixedAssetStatus::Disposed:
    return QStringLiteral("disposed");
  case FixedAssetStatus::Draft:
    return QStringLiteral("draft");
  }
  return QStringLiteral("draft");
}

std::optional<FixedAssetStatus> fixedAssetStatusFromString(const QString & text)
{
  const QString key = text.trimmed().toLower();
  if (key == QLatin1String("draft")) {
    return FixedAssetStatus::Draft;
  }
  if (key == QLatin1String("in_use")) {
    return FixedAssetStatus::InUse;
  }
  if (key == QLatin1String("disposed")) {
    return FixedAssetStatus::Disposed;
  }
  return std::nullopt;
}

QString fixedAssetStatusLabelUk(FixedAssetStatus status)
{
  switch (status) {
  case FixedAssetStatus::InUse:
    return QStringLiteral("в експлуатації");
  case FixedAssetStatus::Disposed:
    return QStringLiteral("списано");
  case FixedAssetStatus::Draft:
    return QStringLiteral("чернетка");
  }
  return QStringLiteral("чернетка");
}

bool isFixedAssetAccountCode(const QString & code)
{
  return codeEquals(code, chart::kMachinery) || codeEquals(code, chart::kVehicles) ||
         codeEquals(code, chart::kOtherFixedAssets);
}

bool isFaDepreciationAccountCode(const QString & code)
{
  return codeEquals(code, chart::kFaDepreciation);
}

bool isFaCorrespondenceCode(const QString & code)
{
  return codeEquals(code, chart::kCapexFa) || codeEquals(code, chart::kSuppliers) ||
         codeEquals(code, chart::kAuxiliary);
}

bool isFixedAssetAccount(const Account & account)
{
  return isFixedAssetAccountCode(account.code);
}

bool usesFaCommissionForm(DocumentType type)
{
  return type == DocumentType::FaCommission;
}

bool usesFaDepreciationForm(DocumentType type)
{
  return type == DocumentType::FaDepreciation;
}

bool usesFaWriteOffForm(DocumentType type)
{
  return type == DocumentType::FaWriteOff;
}

bool usesFaForm(DocumentType type)
{
  return usesFaCommissionForm(type) || usesFaDepreciationForm(type) || usesFaWriteOffForm(type);
}

Result<Money> depreciationCharge(const FixedAsset & asset, int year, int month)
{
  if (asset.status != FixedAssetStatus::InUse || !asset.commissionedOn.isValid()) {
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

Result<std::vector<DocumentLine>> composeFaCommission(const std::vector<Account> & accounts,
                                                      AccountId correspondenceId,
                                                      const std::vector<FixedAsset> & assets)
{
  if (!correspondenceId) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("оберіть кореспонденцію введення (152, 631 або 00)")));
  }
  if (assets.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один основний засіб")));
  }
  const AccountId dep = accountIdByCode(accounts, chart::kFaDepreciation);
  const AccountId aux = accountIdByCode(accounts, chart::kAuxiliary);
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const FixedAsset & asset : assets) {
    ++rowNo;
    if (!asset.id) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: не вибрано картку ОЗ").arg(rowNo)));
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
    cost.fixedAssetId = asset.id;
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
          Error::validation(QStringLiteral("у плані рахунків немає 00 або 131")));
    }
    DocumentLine opening;
    opening.debitAccountId = aux;
    opening.creditAccountId = dep;
    opening.amount = asset.openingAccumulated;
    opening.fixedAssetId = asset.id;
    opening.description = QStringLiteral("знос на початок: ") + cost.description;
    lines.push_back(opening);
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeFaDepreciation(const std::vector<Account> & accounts,
                                                        const std::vector<FixedAsset> & assets,
                                                        int year, int month)
{
  const AccountId dep = accountIdByCode(accounts, chart::kFaDepreciation);
  if (!dep) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 131")));
  }
  std::vector<DocumentLine> lines;
  for (const FixedAsset & asset : assets) {
    const Result<Money> charge = depreciationCharge(asset, year, month);
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
    line.fixedAssetId = asset.id;
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

Result<bool> faMonthDepreciationPosted(QSqlDatabase db, int year, int month)
{
  if (year < 1991 || year > 2100 || month < 1 || month > 12) {
    return Result<bool>::fail(Error::validation(QStringLiteral("некоректний місяць амортизації")));
  }
  const QDate from(year, month, 1);
  const QDate to = from.addMonths(1).addDays(-1);
  const Result<std::vector<DocumentHeader>> existing =
      DocumentRepository(db).list(from, to, DocumentType::FaDepreciation);
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

Result<std::vector<DocumentLine>> composeFaWriteOff(QSqlDatabase db,
                                                    const std::vector<Account> & accounts,
                                                    const std::vector<FixedAsset> & assets,
                                                    QDate date)
{
  if (!date.isValid()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("некоректна дата списання ОЗ")));
  }
  if (assets.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один основний засіб")));
  }
  const AccountId dep = accountIdByCode(accounts, chart::kFaDepreciation);
  if (!dep) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 131")));
  }
  const Result<bool> monthPosted = faMonthDepreciationPosted(db, date.year(), date.month());
  if (!monthPosted) {
    return Result<std::vector<DocumentLine>>::fail(monthPosted.error());
  }
  const bool chargePosted = monthPosted.value();
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const FixedAsset & asset : assets) {
    ++rowNo;
    if (!asset.id) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: не вибрано картку ОЗ").arg(rowNo)));
    }
    if (asset.status != FixedAssetStatus::InUse) {
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
      const Result<Money> charge = depreciationCharge(asset, date.year(), date.month());
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
      charge.fixedAssetId = asset.id;
      charge.description = QStringLiteral("амортизація до списання: ") + title;
      lines.push_back(charge);
    }
    if (accum.value().isPositive()) {
      DocumentLine writeAccum;
      writeAccum.debitAccountId = dep;
      writeAccum.creditAccountId = asset.assetAccountId;
      writeAccum.amount = accum.value();
      writeAccum.fixedAssetId = asset.id;
      writeAccum.description = QStringLiteral("знос: ") + title;
      lines.push_back(writeAccum);
    }
    if (residual.value().isPositive()) {
      DocumentLine writeRest;
      writeRest.debitAccountId = asset.expenseAccountId;
      writeRest.creditAccountId = asset.assetAccountId;
      writeRest.amount = residual.value();
      writeRest.fixedAssetId = asset.id;
      writeRest.description = QStringLiteral("залишкова: ") + title;
      lines.push_back(writeRest);
    }
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("немає сум для списання ОЗ")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<Document> buildFaDepreciationDocument(QSqlDatabase db, int year, int month)
{
  if (year < 1991 || year > 2100 || month < 1 || month > 12) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("некоректний місяць амортизації")));
  }
  const QDate from(year, month, 1);
  const QDate to = from.addMonths(1).addDays(-1);
  DocumentRepository documents(db);
  const Result<std::vector<DocumentHeader>> existing =
      documents.list(from, to, DocumentType::FaDepreciation);
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
  const Result<std::vector<FixedAsset>> assets = FixedAssetRepository(db).listInUse();
  if (!assets) {
    return Result<Document>::fail(assets.error());
  }
  const Result<std::vector<DocumentLine>> lines =
      composeFaDepreciation(accounts.value(), assets.value(), year, month);
  if (!lines) {
    return Result<Document>::fail(lines.error());
  }
  Document document;
  document.type = DocumentType::FaDepreciation;
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
  const Result<QString> number = documents.allocateNumber(DocumentType::FaDepreciation);
  if (!number) {
    return Result<Document>::fail(number.error());
  }
  document.number = number.value();
  return Result<Document>::ok(std::move(document));
}

} // namespace fin
