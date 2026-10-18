#pragma once

// Основні засоби (НП(С)БО 7): картка, амортизація.

#include "fin/core/Ids.h"
#include "fin/core/Money.h"
#include "fin/core/Result.h"
#include "fin/domain/Account.h"
#include "fin/domain/Document.h"

#include <optional>
#include <vector>

#include <QDate>
#include <QSqlDatabase>
#include <QString>

namespace fin {

// Життєвий цикл інвентарного об'єкта ОЗ
enum class FixedAssetStatus
{
  Draft, // чернетка (створена картка, ще не введена в експлуатацію)
  InUse, // в експлуатації (нараховується щомісячна амортизація)
  Disposed // списано / ліквідовано
};

struct FixedAsset
{
  FixedAssetId id;
  QString inventoryNo;            // інвентарний номер
  QString name;                   // найменування об'єкта
  AccountId assetAccountId;       // рахунок обліку (104, 105, 109)
  AccountId expenseAccountId;     // рахунок витрат амортизації (92)
  QDate commissionedOn;           // дата введення в експлуатацію
  QDate disposedOn;               // дата списання
  Money original = Money::zero(); // первісна (історична) вартість
  Money salvage = Money::zero();  // ліквідаційна вартість
  Money openingAccumulated = Money::zero(); // накопичений знос на дату початку обліку в програмі
  Money accumulated = Money::zero(); // сумарний накопичений знос
  int usefulLifeMonths = 0; // строк корисного використання у місяцях
  FixedAssetStatus status = FixedAssetStatus::Draft;
  bool isActive = true;

  // Залишкова (балансова) вартість об'єкта: первісна мінус накопичений знос
  Money residual() const;
};

QString fixedAssetStatusToString(FixedAssetStatus status);
std::optional<FixedAssetStatus> fixedAssetStatusFromString(const QString & text);
QString fixedAssetStatusLabelUk(FixedAssetStatus status);

bool isFixedAssetAccountCode(const QString & code);
bool isFaDepreciationAccountCode(const QString & code);
bool isFaCorrespondenceCode(const QString & code);
bool isFixedAssetAccount(const Account & account);

// Розрахунок місячної суми амортизації прямолінійним методом: (первісна - ліквідаційна) / місяці
Result<Money> depreciationCharge(const FixedAsset & asset, int year, int month);

// Формування проводок введення в експлуатацію (Дт 10x Кт 152/631/00)
Result<std::vector<DocumentLine>> composeFaCommission(const std::vector<Account> & accounts,
                                                      AccountId correspondenceId,
                                                      const std::vector<FixedAsset> & assets);

// Формування проводок щомісячної амортизації за всіма діючими об'єктами
Result<std::vector<DocumentLine>> composeFaDepreciation(const std::vector<Account> & accounts,
                                                        const std::vector<FixedAsset> & assets,
                                                        int year, int month);

// Формування проводок списання об'єкта ОЗ (закриття зносу та віднесення залишкової вартості на витрати)
Result<std::vector<DocumentLine>> composeFaWriteOff(QSqlDatabase db,
                                                    const std::vector<Account> & accounts,
                                                    const std::vector<FixedAsset> & assets,
                                                    QDate date);

// Груповий документ амортизації ОЗ за календарний місяць
Result<Document> buildFaDepreciationDocument(QSqlDatabase db, int year, int month);

bool usesFaCommissionForm(DocumentType type);
bool usesFaDepreciationForm(DocumentType type);
bool usesFaWriteOffForm(DocumentType type);
bool usesFaForm(DocumentType type);

} // namespace fin
