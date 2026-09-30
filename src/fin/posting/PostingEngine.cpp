#include "fin/posting/PostingEngine.h"

#include <QLatin1String>

#include "fin/auth/Access.h"
#include "fin/core/Checked.h"
#include "fin/core/Transaction.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/AuditLog.h"
#include "fin/db/DocumentRepository.h"
#include "fin/db/FixedAssetRepository.h"
#include "fin/db/IntangibleAssetRepository.h"
#include "fin/db/InventoryRepository.h"
#include "fin/db/LedgerRepository.h"
#include "fin/db/PeriodRepository.h"
#include "fin/db/SubcontoRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/Currency.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Subconto.h"
#include "fin/db/LowValueRepository.h"
#include "fin/domain/IntangibleAsset.h"
#include "fin/domain/Employee.h"
#include "fin/domain/Vat.h"
#include "fin/integrity/IntegrityChecker.h"
#include "fin/posting/AdvanceOffset.h"
#include "fin/posting/DocumentValidator.h"
#include "fin/posting/InventoryCosting.h"

#include <QHash>
#include <QSet>
#include <optional>

namespace fin {
namespace {

// Визначаємо рахунки запасів (281, 201, 202, 203, 26) за кодом, щоб автоматично застосовувати
// Середньозважене коштування товарних та матеріальних рядків будь-якого типу.
bool isInventoryStock(const Account & account)
{
  const QString & code = account.code;
  return code.compare(QLatin1String(chart::kGoods), Qt::CaseInsensitive) == 0 ||
         code.compare(QLatin1String(chart::kRawMaterials), Qt::CaseInsensitive) == 0 ||
         code.compare(QLatin1String(chart::kPurchasedComponents), Qt::CaseInsensitive) == 0 ||
         code.compare(QLatin1String(chart::kFuel), Qt::CaseInsensitive) == 0 ||
         code.compare(QLatin1String(chart::kFinishedGoods), Qt::CaseInsensitive) == 0;
}

bool isCogs(const Account & account)
{
  const QString & code = account.code;
  return code.compare(QLatin1String(chart::kCogs), Qt::CaseInsensitive) == 0 ||
         code.compare(QLatin1String(chart::kProductionCogs), Qt::CaseInsensitive) == 0;
}

Result<SubcontoSet> extractSubcontoSet(QSqlDatabase db, AccountId accountId,
                                       const Document & document, const DocumentLine & line)
{
  SubcontoSet set;
  SubcontoRepository repo(db);
  const auto listRes = repo.listForAccount(accountId);
  if (!listRes) {
    return Result<SubcontoSet>::fail(listRes.error());
  }
  for (const auto & setting : listRes.value()) {
    SubcontoValue val;
    val.kindId = setting.subcontoKindId;
    if (setting.subcontoKindId == subconto_kinds::Counterparty) {
      val.entityId =
          line.counterpartyId ? line.counterpartyId.value : document.counterpartyId.value;
    } else if (setting.subcontoKindId == subconto_kinds::Nomenclature) {
      val.entityId = line.nomenclatureId.value;
    } else if (setting.subcontoKindId == subconto_kinds::FixedAsset) {
      val.entityId = line.fixedAssetId.value;
    } else if (setting.subcontoKindId == subconto_kinds::IntangibleAsset) {
      val.entityId = line.intangibleId.value;
    } else if (setting.subcontoKindId == subconto_kinds::LowValue) {
      val.entityId = line.lowValueId.value;
    } else if (setting.subcontoKindId == subconto_kinds::Employee) {
      val.entityId = line.employeeId ? line.employeeId.value : document.employeeId.value;
    } else if (setting.subcontoKindId == subconto_kinds::Contract) {
      val.entityId = document.contractId.value;
    }
    set.at(setting.lineNo) = val;
  }
  return Result<SubcontoSet>::ok(std::move(set));
}

// Оновлює картки ОЗ після проведення документа:
//   FaCommission  → markCommissioned (ОЗ переходить у стан «в експлуатації»,
//                   початкова накопичена амортизація з рядка 131 зберігається)
//   FaDepreciation → addDepreciation (збільшує накопичений знос картки)
//   FaWriteOff    → addDepreciation (за рядком 131) + markDisposed
// QSet<qlonglong> done — захищає від повторної обробки однієї картки
// при наявності кількох рядків для одного ОЗ.
Status applyFixedAssets(QSqlDatabase db, const Document & document)
{
  if (document.type != DocumentType::FaCommission &&
      document.type != DocumentType::FaDepreciation && document.type != DocumentType::FaWriteOff) {
    return Status::ok();
  }
  FixedAssetRepository assets(db);
  AccountRepository accounts(db);
  if (document.type == DocumentType::FaWriteOff) {
    for (const DocumentLine & line : document.lines) {
      if (!line.fixedAssetId) {
        continue;
      }
      const Result<Account> credit = accounts.findById(line.creditAccountId);
      if (!credit) {
        return credit.asStatus();
      }
      if (!isFaDepreciationAccountCode(credit.value().code)) {
        continue;
      }
      Status status = assets.addDepreciation(line.fixedAssetId, line.amount);
      if (!status) {
        return status;
      }
    }
    QSet<qlonglong> done;
    for (const DocumentLine & line : document.lines) {
      if (!line.fixedAssetId || done.contains(line.fixedAssetId.value)) {
        continue;
      }
      done.insert(line.fixedAssetId.value);
      Status status = assets.markDisposed(line.fixedAssetId, document.date);
      if (!status) {
        return status;
      }
    }
    return Status::ok();
  }
  if (document.type == DocumentType::FaCommission) {
    QHash<qlonglong, Money> opening;
    for (const DocumentLine & line : document.lines) {
      if (!line.fixedAssetId) {
        continue;
      }
      const Result<Account> credit = accounts.findById(line.creditAccountId);
      if (!credit) {
        return credit.asStatus();
      }
      if (isFaDepreciationAccountCode(credit.value().code)) {
        opening.insert(line.fixedAssetId.value, line.amount);
      }
    }
    QSet<qlonglong> done;
    for (const DocumentLine & line : document.lines) {
      if (!line.fixedAssetId || done.contains(line.fixedAssetId.value)) {
        continue;
      }
      const Result<Account> debit = accounts.findById(line.debitAccountId);
      if (!debit) {
        return debit.asStatus();
      }
      if (!isFixedAssetAccountCode(debit.value().code)) {
        continue;
      }
      done.insert(line.fixedAssetId.value);
      const Money start = opening.value(line.fixedAssetId.value, Money::zero());
      Status status = assets.markCommissioned(line.fixedAssetId, document.date, start);
      if (!status) {
        return status;
      }
    }
    return Status::ok();
  }
  for (const DocumentLine & line : document.lines) {
    if (!line.fixedAssetId) {
      return Status::fail(Error::validation(QStringLiteral("рядок амортизації без картки ОЗ")));
    }
    Status status = assets.addDepreciation(line.fixedAssetId, line.amount);
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

// Відкочує зміни карток ОЗ при знятті проведення.
// При відкоті FaDepreciation перевіряє, чи не існує проведений FaWriteOff
// для тієї ж картки — знімати амортизацію після списання заборонено.
Status revertFixedAssets(QSqlDatabase db, const Document & document)
{
  if (document.type != DocumentType::FaCommission &&
      document.type != DocumentType::FaDepreciation && document.type != DocumentType::FaWriteOff) {
    return Status::ok();
  }
  FixedAssetRepository assets(db);
  if (document.type == DocumentType::FaWriteOff) {
    QSet<qlonglong> done;
    for (const DocumentLine & line : document.lines) {
      if (!line.fixedAssetId || done.contains(line.fixedAssetId.value)) {
        continue;
      }
      done.insert(line.fixedAssetId.value);
      Status status = assets.revertDispose(line.fixedAssetId);
      if (!status) {
        return status;
      }
    }
    AccountRepository accounts(db);
    for (const DocumentLine & line : document.lines) {
      if (!line.fixedAssetId) {
        continue;
      }
      const Result<Account> credit = accounts.findById(line.creditAccountId);
      if (!credit) {
        return credit.asStatus();
      }
      if (!isFaDepreciationAccountCode(credit.value().code)) {
        continue;
      }
      Status status = assets.subtractDepreciation(line.fixedAssetId, line.amount);
      if (!status) {
        return status;
      }
    }
    return Status::ok();
  }
  if (document.type == DocumentType::FaCommission) {
    QSet<qlonglong> done;
    for (const DocumentLine & line : document.lines) {
      if (!line.fixedAssetId || done.contains(line.fixedAssetId.value)) {
        continue;
      }
      done.insert(line.fixedAssetId.value);
      Status status = assets.revertCommission(line.fixedAssetId);
      if (!status) {
        return status;
      }
    }
    return Status::ok();
  }
  for (const DocumentLine & line : document.lines) {
    if (!line.fixedAssetId) {
      continue;
    }
    const Result<bool> writtenOff = assets.hasPostedWriteOff(line.fixedAssetId);
    if (!writtenOff) {
      return writtenOff.asStatus();
    }
    if (writtenOff.value()) {
      return Status::fail(
          Error::conflict(QStringLiteral("спочатку зніміть проведення списання ОЗ")));
    }
    Status status = assets.subtractDepreciation(line.fixedAssetId, line.amount);
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

// Аналог applyFixedAssets для нематеріальних активів (НМА).
// Рахунки: 125/127 (вартість), 133 (амортизація), 154 (капіталовкладення).
Status applyIntangibles(QSqlDatabase db, const Document & document)
{
  if (document.type != DocumentType::IaCommission &&
      document.type != DocumentType::IaDepreciation && document.type != DocumentType::IaWriteOff) {
    return Status::ok();
  }
  IntangibleAssetRepository assets(db);
  AccountRepository accounts(db);
  if (document.type == DocumentType::IaWriteOff) {
    for (const DocumentLine & line : document.lines) {
      if (!line.intangibleId) {
        continue;
      }
      const Result<Account> credit = accounts.findById(line.creditAccountId);
      if (!credit) {
        return credit.asStatus();
      }
      if (!isIaAmortizationAccountCode(credit.value().code)) {
        continue;
      }
      Status status = assets.addDepreciation(line.intangibleId, line.amount);
      if (!status) {
        return status;
      }
    }
    QSet<qlonglong> done;
    for (const DocumentLine & line : document.lines) {
      if (!line.intangibleId || done.contains(line.intangibleId.value)) {
        continue;
      }
      done.insert(line.intangibleId.value);
      Status status = assets.markDisposed(line.intangibleId, document.date);
      if (!status) {
        return status;
      }
    }
    return Status::ok();
  }
  if (document.type == DocumentType::IaCommission) {
    QHash<qlonglong, Money> opening;
    for (const DocumentLine & line : document.lines) {
      if (!line.intangibleId) {
        continue;
      }
      const Result<Account> credit = accounts.findById(line.creditAccountId);
      if (!credit) {
        return credit.asStatus();
      }
      if (isIaAmortizationAccountCode(credit.value().code)) {
        opening.insert(line.intangibleId.value, line.amount);
      }
    }
    QSet<qlonglong> done;
    for (const DocumentLine & line : document.lines) {
      if (!line.intangibleId || done.contains(line.intangibleId.value)) {
        continue;
      }
      const Result<Account> debit = accounts.findById(line.debitAccountId);
      if (!debit) {
        return debit.asStatus();
      }
      if (!isIntangibleAccountCode(debit.value().code)) {
        continue;
      }
      done.insert(line.intangibleId.value);
      const Money start = opening.value(line.intangibleId.value, Money::zero());
      Status status = assets.markCommissioned(line.intangibleId, document.date, start);
      if (!status) {
        return status;
      }
    }
    return Status::ok();
  }
  for (const DocumentLine & line : document.lines) {
    if (!line.intangibleId) {
      return Status::fail(Error::validation(QStringLiteral("рядок амортизації без картки НМА")));
    }
    Status status = assets.addDepreciation(line.intangibleId, line.amount);
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

Status revertIntangibles(QSqlDatabase db, const Document & document)
{
  if (document.type != DocumentType::IaCommission &&
      document.type != DocumentType::IaDepreciation && document.type != DocumentType::IaWriteOff) {
    return Status::ok();
  }
  IntangibleAssetRepository assets(db);
  if (document.type == DocumentType::IaWriteOff) {
    QSet<qlonglong> done;
    for (const DocumentLine & line : document.lines) {
      if (!line.intangibleId || done.contains(line.intangibleId.value)) {
        continue;
      }
      done.insert(line.intangibleId.value);
      Status status = assets.revertDispose(line.intangibleId);
      if (!status) {
        return status;
      }
    }
    AccountRepository accounts(db);
    for (const DocumentLine & line : document.lines) {
      if (!line.intangibleId) {
        continue;
      }
      const Result<Account> credit = accounts.findById(line.creditAccountId);
      if (!credit) {
        return credit.asStatus();
      }
      if (!isIaAmortizationAccountCode(credit.value().code)) {
        continue;
      }
      Status status = assets.subtractDepreciation(line.intangibleId, line.amount);
      if (!status) {
        return status;
      }
    }
    return Status::ok();
  }
  if (document.type == DocumentType::IaCommission) {
    QSet<qlonglong> done;
    for (const DocumentLine & line : document.lines) {
      if (!line.intangibleId || done.contains(line.intangibleId.value)) {
        continue;
      }
      done.insert(line.intangibleId.value);
      Status status = assets.revertCommission(line.intangibleId);
      if (!status) {
        return status;
      }
    }
    return Status::ok();
  }
  for (const DocumentLine & line : document.lines) {
    if (!line.intangibleId) {
      continue;
    }
    const Result<bool> writtenOff = assets.hasPostedWriteOff(line.intangibleId);
    if (!writtenOff) {
      return writtenOff.asStatus();
    }
    if (writtenOff.value()) {
      return Status::fail(
          Error::conflict(QStringLiteral("спочатку зніміть проведення списання НМА")));
    }
    Status status = assets.subtractDepreciation(line.intangibleId, line.amount);
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

// Оновлює картки МШП при проведенні:
//   MsReceipt → items.receive (збільшує кількість та балансову вартість)
//   MsIssue   → items.issue   (зменшує кількість; середня вже в картці МШП —
//                вартість фіксується при надходженні)
Status applyLowValue(QSqlDatabase db, const Document & document)
{
  if (document.type != DocumentType::MsReceipt && document.type != DocumentType::MsIssue) {
    return Status::ok();
  }
  LowValueRepository items(db);
  if (document.type == DocumentType::MsReceipt) {
    for (const DocumentLine & line : document.lines) {
      if (!line.lowValueId) {
        continue;
      }
      Status status = items.receive(line.lowValueId, line.quantity, line.amount);
      if (!status) {
        return status;
      }
    }
    return Status::ok();
  }
  for (const DocumentLine & line : document.lines) {
    if (!line.lowValueId) {
      continue;
    }
    Status status = items.issue(line.lowValueId, line.quantity, line.amount);
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

Status revertLowValue(QSqlDatabase db, const Document & document)
{
  if (document.type != DocumentType::MsReceipt && document.type != DocumentType::MsIssue) {
    return Status::ok();
  }
  LowValueRepository items(db);
  if (document.type == DocumentType::MsReceipt) {
    for (const DocumentLine & line : document.lines) {
      if (!line.lowValueId) {
        continue;
      }
      Status status = items.revertReceive(line.lowValueId, line.quantity, line.amount);
      if (!status) {
        return status;
      }
    }
    return Status::ok();
  }
  for (const DocumentLine & line : document.lines) {
    if (!line.lowValueId) {
      continue;
    }
    Status status = items.revertIssue(line.lowValueId, line.quantity, line.amount);
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

} // namespace

PostingEngine::PostingEngine(QSqlDatabase db) : db_(std::move(db)) {}

Status PostingEngine::post(DocumentId documentId)
{
  // Вся операція проведення — одна атомарна транзакція SQLite.
  // Якщо будь-який крок завершується помилкою, транзакція відкочується
  // автоматично деструктором Transaction.
  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }

  DocumentRepository documents(db_);
  Result<Document> loaded = documents.findById(documentId);
  if (!loaded) {
    return loaded.asStatus();
  }
  Document document = std::move(loaded.value());
  Status access = Access::requireDocumentWrite(db_, document.type);
  if (!access) {
    return access;
  }
  if (!requiresLedgerPosting(document.type)) {
    return Status::fail(
        Error::validation(QStringLiteral("рахунок на оплату не проводиться в журнал")));
  }
  if (document.posted) {
    return Status::fail(
        Error::alreadyPosted(QStringLiteral("документ %1 уже проведено").arg(document.number)));
  }

  Status status = PeriodRepository(db_).ensureDateWritable(document.date);
  if (!status) {
    return status;
  }
  if (document.vatRegistrationDate.isValid()) {
    status = PeriodRepository(db_).ensureDateWritable(document.vatRegistrationDate);
    if (!status) {
      return status;
    }
  }

  DocumentValidator validator(db_);
  status = validator.validate(document);
  if (!status) {
    return status;
  }

  InventoryCosting costing(db_);
  AccountRepository accounts(db_);
  const Result<std::vector<Account>> chart = accounts.listPostable();
  if (!chart) {
    return chart.asStatus();
  }
  // Перегенерація автоматичних рядків:
  // 1. Видаляємо старі рядки заліку авансів (681→361 або 371→631),
  //    щоб перерахувати їх заново — на випадок зміни суми документа.
  stripAdvanceOffsetLines(chart.value(), document);
  if (document.type == DocumentType::TaxInvoice) {
    // 2. Для ПН: видаляємо рядки реєстрації (643→641 або 644→641)
    //    і додаємо їх знову з актуальною датою реєстрації.
    //    Якщо vatRegistrationDate невалідна — ПН ще не зареєстрована,
    //    рядки реєстрації не додаються.
    stripVatRegistrationLines(chart.value(), document);
    if (document.vatRegistrationDate.isValid()) {
      const Result<std::vector<DocumentLine>> extra = vatRegistrationLines(chart.value(), document);
      if (!extra) {
        return extra.asStatus();
      }
      document.lines.insert(document.lines.end(), extra.value().begin(), extra.value().end());
    }
  }
  const bool isForeign = !document.currencyCode.isEmpty() &&
                         document.currencyCode != QLatin1String("UAH") &&
                         document.currencyRateMicros > 0;
  ExchangeRate docRate;
  if (isForeign) {
    docRate = ExchangeRate(document.currencyRateMicros, document.currencyNominal);
  }

  // Коштування товарних рядків (середньозважена собівартість на залишку).
  // Для валютних документів суми в document_lines лишаються в валюті документа;
  // UAH-собівартість береться окремо для складу та ГЛ (без повторної конвертації).
  std::vector<std::optional<Money>> inventoryUah(document.lines.size());
  if (document.type == DocumentType::StockTransfer) {
    const WarehouseId srcWh =
        document.warehouseId.value > 0 ? document.warehouseId : WarehouseId{1};
    const WarehouseId dstWh =
        document.targetWarehouseId.value > 0 ? document.targetWarehouseId : WarehouseId{1};
    for (std::size_t i = 0; i < document.lines.size(); ++i) {
      DocumentLine & line = document.lines[i];
      const Result<Money> cost =
          costing.takeOutgoingCost(srcWh, line.nomenclatureId, line.quantity);
      if (!cost) {
        return cost.asStatus();
      }
      if (cost.value().isPositive()) {
        line.amount = cost.value();
      }
      status = costing.receive(dstWh, line.nomenclatureId, line.quantity, line.amount);
      if (!status) {
        return status;
      }
    }
  } else if (document.type == DocumentType::KitAssembly) {
    const WarehouseId wh = document.warehouseId.value > 0 ? document.warehouseId : WarehouseId{1};
    Money total = Money::zero();
    for (std::size_t i = 1; i < document.lines.size(); ++i) {
      DocumentLine & line = document.lines[i];
      const Result<Money> cost = costing.takeOutgoingCost(wh, line.nomenclatureId, line.quantity);
      if (!cost) {
        return cost.asStatus();
      }
      if (!cost.value().isPositive()) {
        return Status::fail(Error::conflict(
            QStringLiteral("немає собівартості компонента; оприбуткуйте товар або перевірте "
                           "залишок")));
      }
      line.amount = cost.value();
      const Result<Money> next = total.plus(cost.value());
      if (!next) {
        return next.asStatus();
      }
      total = next.value();
    }
    document.lines[0].amount = total;
    status =
        costing.receive(wh, document.lines[0].nomenclatureId, document.lines[0].quantity, total);
    if (!status) {
      return status;
    }
  } else if (document.type == DocumentType::KitDisassembly) {
    const WarehouseId wh = document.warehouseId.value > 0 ? document.warehouseId : WarehouseId{1};
    DocumentLine & kitLine = document.lines[0];
    const Result<Money> kitCost =
        costing.takeOutgoingCost(wh, kitLine.nomenclatureId, kitLine.quantity);
    if (!kitCost) {
      return kitCost.asStatus();
    }
    if (!kitCost.value().isPositive()) {
      return Status::fail(
          Error::conflict(QStringLiteral("немає собівартості комплекту для розукомплектації")));
    }
    kitLine.amount = kitCost.value();

    std::vector<std::int64_t> weights(document.lines.size(), 0);
    std::int64_t weightSum = 0;
    for (std::size_t i = 1; i < document.lines.size(); ++i) {
      const DocumentLine & line = document.lines[i];
      std::int64_t w = 0;
      if (line.unitPrice.isPositive()) {
        const Result<Money> part = line.quantity.times(line.unitPrice);
        if (part) {
          w = part.value().cents();
        }
      }
      if (w <= 0) {
        w = line.quantity.milli();
      }
      if (w <= 0) {
        w = 1;
      }
      weights[i] = w;
      weightSum += w;
    }
    if (weightSum <= 0) {
      return Status::fail(
          Error::validation(QStringLiteral("немає ваг для розподілу собівартості")));
    }
    Money allocated = Money::zero();
    for (std::size_t i = 1; i < document.lines.size(); ++i) {
      DocumentLine & line = document.lines[i];
      Money share = Money::zero();
      if (i + 1 == document.lines.size()) {
        const Result<Money> rest = kitCost.value().minus(allocated);
        if (!rest) {
          return rest.asStatus();
        }
        share = rest.value();
      } else {
        const Result<std::int64_t> cents =
            mulDivRound(kitCost.value().cents(), weights[i], weightSum);
        if (!cents) {
          return cents.asStatus();
        }
        share = Money::fromCents(cents.value());
        const Result<Money> next = allocated.plus(share);
        if (!next) {
          return next.asStatus();
        }
        allocated = next.value();
      }
      if (!share.isPositive()) {
        return Status::fail(
            Error::validation(QStringLiteral("нульова частка собівартості компонента")));
      }
      line.amount = share;
      status = costing.receive(wh, line.nomenclatureId, line.quantity, share);
      if (!status) {
        return status;
      }
    }
  } else {
    for (std::size_t i = 0; i < document.lines.size(); ++i) {
      DocumentLine & line = document.lines[i];
      const Result<Account> debit = accounts.findById(line.debitAccountId);
      const Result<Account> credit = accounts.findById(line.creditAccountId);
      if (!debit || !credit) {
        return Status::fail(
            Error::validation(QStringLiteral("не вдалося визначити рахунки рядка")));
      }
      const WarehouseId docWh =
          line.warehouseId.value > 0
              ? line.warehouseId
              : (document.warehouseId.value > 0 ? document.warehouseId : WarehouseId{1});
      if (isInventoryStock(debit.value())) {
        if (isCogs(credit.value())) {
          const Result<Money> cost = costing.peekCost(docWh, line.nomenclatureId, line.quantity);
          if (!cost) {
            return cost.asStatus();
          }
          if (isForeign) {
            inventoryUah[i] = cost.value();
            const Result<Money> fxCost = docRate.toForeign(cost.value());
            if (!fxCost) {
              return fxCost.asStatus();
            }
            line.amount = fxCost.value();
          } else if (line.amount != cost.value()) {
            line.amount = cost.value();
          }
        }
        Money receiveAmount = line.amount;
        if (isForeign) {
          if (inventoryUah[i].has_value()) {
            receiveAmount = inventoryUah[i].value();
          } else {
            const Result<Money> uahAmt = docRate.toUah(line.amount);
            if (!uahAmt) {
              return uahAmt.asStatus();
            }
            receiveAmount = uahAmt.value();
          }
        }
        status = costing.receive(docWh, line.nomenclatureId, line.quantity, receiveAmount);
        if (!status) {
          return status;
        }
      } else if (isInventoryStock(credit.value())) {
        const Result<Money> cost =
            costing.takeOutgoingCost(docWh, line.nomenclatureId, line.quantity);
        if (!cost) {
          return cost.asStatus();
        }
        if (!cost.value().isPositive()) {
          return Status::fail(
              Error::conflict(QStringLiteral("немає собівартості для списання; оприбуткуйте товар "
                                             "або перевірте залишок")));
        }
        if (isForeign) {
          inventoryUah[i] = cost.value();
          const Result<Money> fxCost = docRate.toForeign(cost.value());
          if (!fxCost) {
            return fxCost.asStatus();
          }
          line.amount = fxCost.value();
        } else if (line.amount != cost.value()) {
          line.amount = cost.value();
        }
      }
    }
  }
  status = applyAdvanceOffset(db_, chart.value(), document);
  if (!status) {
    return status;
  }
  // Залік авансу може додати рядки — вирівняти вектор собівартості.
  if (inventoryUah.size() < document.lines.size()) {
    inventoryUah.resize(document.lines.size());
  }
  status = documents.replaceLines(documentId, document.lines);
  if (!status) {
    return status;
  }
  status = applyFixedAssets(db_, document);
  if (!status) {
    return status;
  }
  status = applyIntangibles(db_, document);
  if (!status) {
    return status;
  }
  status = applyLowValue(db_, document);
  if (!status) {
    return status;
  }

  // Формування рядків Головної книги:
  //   entry.date: для рядків реєстрації ПН використовується postingDate
  //   (= дата реєстрації в ЄРПН), для решти — дата документа.
  //   entry.description: спочатку береться з рядка, якщо порожньо —
  //   з коментаря заголовку документа.
  std::vector<LedgerEntry> entries;
  std::vector<InventoryMovement> movements;
  entries.reserve(document.lines.size());
  for (std::size_t i = 0; i < document.lines.size(); ++i) {
    const DocumentLine & line = document.lines[i];
    LedgerEntry entry;
    entry.documentId = documentId;
    entry.date = line.postingDate.isValid() ? line.postingDate : document.date;
    entry.debitAccountId = line.debitAccountId;
    entry.creditAccountId = line.creditAccountId;
    entry.amount = line.amount;
    if (isForeign) {
      if (inventoryUah[i].has_value()) {
        // Собівартість у гривні; currency_amount — FX-еквівалент (узгоджено з document_lines).
        entry.amount = inventoryUah[i].value();
        entry.currencyCode = document.currencyCode;
        entry.currencyAmount = line.amount;
        entry.currencyRateMicros = document.currencyRateMicros;
      } else {
        const Result<Money> uahAmt = docRate.toUah(line.amount);
        if (!uahAmt) {
          return uahAmt.asStatus();
        }
        entry.amount = uahAmt.value();
        entry.currencyCode = document.currencyCode;
        entry.currencyAmount = line.amount;
        entry.currencyRateMicros = document.currencyRateMicros;
      }
    }
    entry.counterpartyId = line.counterpartyId ? line.counterpartyId : document.counterpartyId;
    const Result<SubcontoSet> debitSub =
        extractSubcontoSet(db_, line.debitAccountId, document, line);
    if (!debitSub)
      return debitSub.asStatus();
    const Result<SubcontoSet> creditSub =
        extractSubcontoSet(db_, line.creditAccountId, document, line);
    if (!creditSub)
      return creditSub.asStatus();
    entry.debitSubconto = debitSub.value();
    entry.creditSubconto = creditSub.value();
    entry.description = line.description.isEmpty() ? document.comment : line.description;
    entries.push_back(entry);

    const Result<Account> debit = accounts.findById(line.debitAccountId);
    const Result<Account> credit = accounts.findById(line.creditAccountId);
    if (!debit || !credit) {
      return Status::fail(Error::validation(QStringLiteral("не вдалося визначити рахунки рядка")));
    }
    if (document.type == DocumentType::StockTransfer) {
      const WarehouseId srcWh =
          document.warehouseId.value > 0 ? document.warehouseId : WarehouseId{1};
      const WarehouseId dstWh =
          document.targetWarehouseId.value > 0 ? document.targetWarehouseId : WarehouseId{1};
      const Result<Quantity> negQty = line.quantity.negated();
      const Result<Money> negAmt = entry.amount.negated();
      if (!negQty || !negAmt) {
        return Status::fail(
            Error::overflow(QStringLiteral("не вдалося сформувати складський рух")));
      }
      // Видаток зі складу-відправника
      InventoryMovement outMove;
      outMove.documentId = documentId;
      outMove.date = document.date;
      outMove.warehouseId = srcWh;
      outMove.nomenclatureId = line.nomenclatureId;
      outMove.quantity = negQty.value();
      outMove.amount = negAmt.value();
      outMove.description = entry.description.isEmpty()
                                ? QStringLiteral("Переміщення товарів (списання)")
                                : entry.description;
      movements.push_back(outMove);

      // Надходження на склад-одержувач
      InventoryMovement inMove;
      inMove.documentId = documentId;
      inMove.date = document.date;
      inMove.warehouseId = dstWh;
      inMove.nomenclatureId = line.nomenclatureId;
      inMove.quantity = line.quantity;
      inMove.amount = entry.amount;
      inMove.description = entry.description.isEmpty()
                               ? QStringLiteral("Переміщення товарів (надходження)")
                               : entry.description;
      movements.push_back(inMove);
    } else if (document.type == DocumentType::KitAssembly ||
               document.type == DocumentType::KitDisassembly) {
      const WarehouseId wh = document.warehouseId.value > 0 ? document.warehouseId : WarehouseId{1};
      const bool kitIncoming = (document.type == DocumentType::KitAssembly && i == 0) ||
                               (document.type == DocumentType::KitDisassembly && i > 0);
      InventoryMovement movement;
      movement.documentId = documentId;
      movement.date = document.date;
      movement.warehouseId = wh;
      movement.nomenclatureId = line.nomenclatureId;
      movement.description = entry.description;
      if (kitIncoming) {
        movement.quantity = line.quantity;
        movement.amount = entry.amount;
      } else {
        const Result<Quantity> qty = line.quantity.negated();
        const Result<Money> amount = entry.amount.negated();
        if (!qty || !amount) {
          return Status::fail(
              Error::overflow(QStringLiteral("не вдалося сформувати складський рух")));
        }
        movement.quantity = qty.value();
        movement.amount = amount.value();
      }
      movements.push_back(movement);
    } else if (isInventoryStock(debit.value()) || isInventoryStock(credit.value())) {
      const WarehouseId docWh =
          line.warehouseId.value > 0
              ? line.warehouseId
              : (document.warehouseId.value > 0 ? document.warehouseId : WarehouseId{1});
      InventoryMovement movement;
      movement.documentId = documentId;
      movement.date = document.date;
      movement.warehouseId = docWh;
      movement.nomenclatureId = line.nomenclatureId;
      movement.description = entry.description;
      if (isInventoryStock(debit.value())) {
        movement.quantity = line.quantity;
        movement.amount = entry.amount;
      } else {
        const Result<Quantity> qty = line.quantity.negated();
        const Result<Money> amount = entry.amount.negated();
        if (!qty || !amount) {
          return Status::fail(
              Error::overflow(QStringLiteral("не вдалося сформувати складський рух")));
        }
        movement.quantity = qty.value();
        movement.amount = amount.value();
      }
      movements.push_back(movement);
    }
  }

  LedgerRepository ledger(db_);
  status = ledger.replaceForDocument(documentId, entries);
  if (!status) {
    return status;
  }
  InventoryRepository inventory(db_);
  status = inventory.replaceForDocument(documentId, movements);
  if (!status) {
    return status;
  }
  status = documents.setPosted(documentId, true, false);
  if (!status) {
    return status;
  }
  // Фінальна перевірка балансу: сума всіх Дт = сума всіх Кт у таблиці
  // ledger_entries. Якщо баланс порушено — транзакція відкочується,
  // щоб некоректний запис ніколи не потрапив у БД.
  IntegrityChecker integrity(db_);
  status = integrity.verifyEngine();
  if (!status) {
    return status;
  }
  status = appendAudit(db_, QStringLiteral("post"), QStringLiteral("document"), documentId.value,
                       document.number);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status PostingEngine::unpost(DocumentId documentId)
{
  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }

  DocumentRepository documents(db_);
  Result<Document> document = documents.findById(documentId);
  if (!document) {
    return document.asStatus();
  }
  Status access = Access::requireDocumentWrite(db_, document.value().type);
  if (!access) {
    return access;
  }
  if (!document.value().posted) {
    return Status::fail(
        Error::notPosted(QStringLiteral("документ %1 не проведено").arg(document.value().number)));
  }

  Status status = PeriodRepository(db_).ensureDateWritable(document.value().date);
  if (!status) {
    return status;
  }
  if (document.value().vatRegistrationDate.isValid()) {
    status = PeriodRepository(db_).ensureDateWritable(document.value().vatRegistrationDate);
    if (!status) {
      return status;
    }
  }
  // Перевірка конфліктів залежних документів перед знятям проведення.
  // Порядок важливий: спочатку перевіряємо залежності, потім відкочуємо стан.
  if (document.value().type == DocumentType::Payroll ||
      document.value().type == DocumentType::VacationPayroll ||
      document.value().type == DocumentType::SickLeavePayroll) {
    // Не можна зняти нарахування, якщо вже є проведена виплата
    // (CashOut / BankPayment), що посилається на цей документ.
    const Result<bool> paid = hasPostedPayrollPayment(db_, document.value().id);
    if (!paid) {
      return paid.asStatus();
    }
    if (paid.value()) {
      return Status::fail(
          Error::conflict(QStringLiteral("спочатку зніміть проведення виплати зарплати")));
    }
  }
  if (document.value().type != DocumentType::TaxInvoice) {
    // Не можна зняти первинний документ, якщо на його основі створена
    // і проведена Податкова накладна.
    const Result<bool> dependent = hasPostedTaxInvoiceBasedOn(db_, document.value().id, {});
    if (!dependent) {
      return dependent.asStatus();
    }
    if (dependent.value()) {
      return Status::fail(
          Error::conflict(QStringLiteral("спочатку зніміть проведення податкової накладної")));
    }
  }
  status = revertFixedAssets(db_, document.value());
  if (!status) {
    return status;
  }
  status = revertIntangibles(db_, document.value());
  if (!status) {
    return status;
  }
  status = revertLowValue(db_, document.value());
  if (!status) {
    return status;
  }

  LedgerRepository ledger(db_);
  status = ledger.removeForDocument(documentId);
  if (!status) {
    return status;
  }
  InventoryRepository inventory(db_);
  status = inventory.removeForDocument(documentId);
  if (!status) {
    return status;
  }
  AccountRepository accounts(db_);
  const Result<std::vector<Account>> chart = accounts.listPostable();
  if (!chart) {
    return chart.asStatus();
  }
  stripAdvanceOffsetLines(chart.value(), document.value());
  if (document.value().type == DocumentType::TaxInvoice) {
    stripVatRegistrationLines(chart.value(), document.value());
  }
  // Спочатку знімаємо posted — replaceLines більше не чіпає проведені документи.
  status = documents.setPosted(documentId, false, true);
  if (!status) {
    return status;
  }
  status = documents.replaceLines(documentId, document.value().lines);
  if (!status) {
    return status;
  }
  IntegrityChecker integrity(db_);
  status = integrity.verifyEngine();
  if (!status) {
    return status;
  }
  status = appendAudit(db_, QStringLiteral("unpost"), QStringLiteral("document"), documentId.value,
                       document.value().number);
  if (!status) {
    return status;
  }
  return txn.commit();
}

// Допоміжна функція перезапису Головної книги з поточного стану документа.
// Використовується при registerTaxInvoice / unregisterTaxInvoice, коли
// рядки документа змінюються без повного циклу unpost→post.
Status writeLedgerFromDocument(QSqlDatabase db, const Document & document)
{
  const bool isForeign = !document.currencyCode.isEmpty() &&
                         document.currencyCode != QLatin1String("UAH") &&
                         document.currencyRateMicros > 0;
  ExchangeRate docRate;
  if (isForeign) {
    docRate = ExchangeRate(document.currencyRateMicros, document.currencyNominal);
  }

  std::vector<LedgerEntry> entries;
  entries.reserve(document.lines.size());
  for (const DocumentLine & line : document.lines) {
    LedgerEntry entry;
    entry.documentId = document.id;
    entry.date = line.postingDate.isValid() ? line.postingDate : document.date;
    entry.debitAccountId = line.debitAccountId;
    entry.creditAccountId = line.creditAccountId;
    entry.amount = line.amount;
    if (isForeign) {
      const Result<Money> uahAmt = docRate.toUah(line.amount);
      if (!uahAmt) {
        return uahAmt.asStatus();
      }
      entry.amount = uahAmt.value();
      entry.currencyCode = document.currencyCode;
      entry.currencyAmount = line.amount;
      entry.currencyRateMicros = document.currencyRateMicros;
    }
    entry.counterpartyId = line.counterpartyId ? line.counterpartyId : document.counterpartyId;
    const Result<SubcontoSet> debitSub =
        extractSubcontoSet(db, line.debitAccountId, document, line);
    if (!debitSub)
      return debitSub.asStatus();
    const Result<SubcontoSet> creditSub =
        extractSubcontoSet(db, line.creditAccountId, document, line);
    if (!creditSub)
      return creditSub.asStatus();
    entry.debitSubconto = debitSub.value();
    entry.creditSubconto = creditSub.value();
    entry.description = line.description.isEmpty() ? document.comment : line.description;
    entries.push_back(entry);
  }
  return LedgerRepository(db).replaceForDocument(document.id, entries);
}

// Фіксує дату реєстрації ПН в ЄРПН та генерує відповідні рядки Головної книги.
// Для вихідної ПН (продавець = наше підприємство): Дт 643 Кт 641
// Для вхідної ПН (покупець = наше підприємство): Дт 641 Кт 644
// Дата проводки = registeredOn (може бути в наступному місяці після дати ПН).
Status PostingEngine::registerTaxInvoice(DocumentId documentId, QDate registeredOn)
{
  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }
  DocumentRepository documents(db_);
  Result<Document> loaded = documents.findById(documentId);
  if (!loaded) {
    return loaded.asStatus();
  }
  Document document = std::move(loaded.value());
  Status access = Access::requireDocumentWrite(db_, document.type);
  if (!access) {
    return access;
  }
  if (document.type != DocumentType::TaxInvoice) {
    return Status::fail(
        Error::validation(QStringLiteral("реєструвати в ЄРПН можна лише податкову накладну")));
  }
  if (!document.posted) {
    return Status::fail(Error::notPosted(QStringLiteral("спочатку проведіть податкову накладну")));
  }
  if (document.vatRegistrationDate.isValid()) {
    return Status::fail(Error::conflict(QStringLiteral("податкова накладна вже зареєстрована")));
  }
  if (!registeredOn.isValid() || registeredOn < document.date) {
    return Status::fail(
        Error::validation(QStringLiteral("дата реєстрації не може бути раніше дати ПН")));
  }
  Status status = PeriodRepository(db_).ensureDateWritable(registeredOn);
  if (!status) {
    return status;
  }
  AccountRepository accounts(db_);
  const Result<std::vector<Account>> chart = accounts.listPostable();
  if (!chart) {
    return chart.asStatus();
  }
  document.vatRegistrationDate = registeredOn;
  stripVatRegistrationLines(chart.value(), document);
  const Result<std::vector<DocumentLine>> extra = vatRegistrationLines(chart.value(), document);
  if (!extra) {
    return extra.asStatus();
  }
  document.lines.insert(document.lines.end(), extra.value().begin(), extra.value().end());
  status = documents.replaceLines(documentId, document.lines, true);
  if (!status) {
    return status;
  }
  status = documents.setVatRegistration(documentId, registeredOn);
  if (!status) {
    return status;
  }
  status = writeLedgerFromDocument(db_, document);
  if (!status) {
    return status;
  }
  IntegrityChecker integrity(db_);
  status = integrity.verifyEngine();
  if (!status) {
    return status;
  }
  status = appendAudit(db_, QStringLiteral("register"), QStringLiteral("document"),
                       documentId.value, document.number);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status PostingEngine::unregisterTaxInvoice(DocumentId documentId)
{
  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }
  DocumentRepository documents(db_);
  Result<Document> loaded = documents.findById(documentId);
  if (!loaded) {
    return loaded.asStatus();
  }
  Document document = std::move(loaded.value());
  Status access = Access::requireDocumentWrite(db_, document.type);
  if (!access) {
    return access;
  }
  if (document.type != DocumentType::TaxInvoice || !document.posted) {
    return Status::fail(
        Error::validation(QStringLiteral("скасувати реєстрацію можна лише для проведеної ПН")));
  }
  if (!document.vatRegistrationDate.isValid()) {
    return Status::fail(
        Error::validation(QStringLiteral("податкова накладна ще не зареєстрована")));
  }
  Status status = PeriodRepository(db_).ensureDateWritable(document.vatRegistrationDate);
  if (!status) {
    return status;
  }
  AccountRepository accounts(db_);
  const Result<std::vector<Account>> chart = accounts.listPostable();
  if (!chart) {
    return chart.asStatus();
  }
  stripVatRegistrationLines(chart.value(), document);
  document.vatRegistrationDate = QDate();
  status = documents.replaceLines(documentId, document.lines, true);
  if (!status) {
    return status;
  }
  status = documents.setVatRegistration(documentId, QDate());
  if (!status) {
    return status;
  }
  status = writeLedgerFromDocument(db_, document);
  if (!status) {
    return status;
  }
  IntegrityChecker integrity(db_);
  status = integrity.verifyEngine();
  if (!status) {
    return status;
  }
  status = appendAudit(db_, QStringLiteral("unregister"), QStringLiteral("document"),
                       documentId.value, document.number);
  if (!status) {
    return status;
  }
  return txn.commit();
}

} // namespace fin
