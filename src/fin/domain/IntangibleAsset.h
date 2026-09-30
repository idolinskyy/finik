#pragma once

// Нематеріальні активи (НП(С)БО 8).

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

// Життєвий цикл картки НМА
enum class IntangibleStatus
{
  Draft,   // чернетка (створено картку НМА)
  InUse,   // в експлуатації (нараховується амортизація)
  Disposed // списано
};

struct IntangibleAsset
{
  IntangibleId id;
  QString inventoryNo;        // інвентарний номер
  QString name;               // найменування об'єкта НМА
  AccountId assetAccountId;   // рахунок обліку (125, 127)
  AccountId expenseAccountId; // рахунок витрат (92)
  QDate commissionedOn; // дата введення в господарський оборот
  QDate disposedOn;     // дата списання
  Money original = Money::zero(); // первісна вартість придбання / створення
  Money salvage = Money::zero(); // ліквідаційна вартість
  Money openingAccumulated = Money::zero(); // накопичена амортизація на початок обліку
  Money accumulated = Money::zero(); // сумарна накопичена амортизація
  int usefulLifeMonths = 0; // строк корисного використання у місяцях
  IntangibleStatus status = IntangibleStatus::Draft;
  bool isActive = true;

  // Залишкова вартість НМА
  Money residual() const;
};

QString intangibleStatusToString(IntangibleStatus status);
std::optional<IntangibleStatus> intangibleStatusFromString(const QString & text);
QString intangibleStatusLabelUk(IntangibleStatus status);

bool isIntangibleAccountCode(const QString & code);
bool isIaAmortizationAccountCode(const QString & code);
bool isIaCorrespondenceCode(const QString & code);
bool isIntangibleAccount(const Account & account);

// Розрахунок місячної амортизації прямолінійним методом: (первісна - ліквідаційна) / місяці
Result<Money> iaAmortizationCharge(const IntangibleAsset & asset, int year, int month);

// Формування проводок введення НМА в оборот (Дт 12x Кт 154/631/00)
Result<std::vector<DocumentLine>> composeIaCommission(const std::vector<Account> & accounts,
                                                      AccountId correspondenceId,
                                                      const std::vector<IntangibleAsset> & assets);

// Формування проводок щомісячної амортизації НМА (Дт 92 Кт 133)
Result<std::vector<DocumentLine>> composeIaDepreciation(const std::vector<Account> & accounts,
                                                        const std::vector<IntangibleAsset> & assets,
                                                        int year, int month);

// Формування проводок списання НМА (закриття амортизації та списання залишкової вартості на витрати)
Result<std::vector<DocumentLine>> composeIaWriteOff(QSqlDatabase db,
                                                    const std::vector<Account> & accounts,
                                                    const std::vector<IntangibleAsset> & assets,
                                                    QDate date);

// Документ амортизації НМА за місяць
Result<Document> buildIaDepreciationDocument(QSqlDatabase db, int year, int month);

bool usesIaCommissionForm(DocumentType type);
bool usesIaDepreciationForm(DocumentType type);
bool usesIaWriteOffForm(DocumentType type);
bool usesIaForm(DocumentType type);

} // namespace fin
