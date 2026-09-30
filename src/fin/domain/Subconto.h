#pragma once

// Субконто (аналітичні розрізи).

#include "fin/core/Ids.h"

#include <cstdint>

#include <QString>

namespace fin {

// Опис виду субконто (довідника-джерела аналітичних даних)
struct SubcontoKind
{
  QString id; // системний ідентифікатор ('counterparty', 'nomenclature', 'fixed_asset', тощо)
  QString nameUa; // україномовна назва ('Контрагенти', 'Номенклатура', 'Основні засоби')
  QString sourceTable; // назва реляційної таблиці джерела ('counterparties', 'nomenclature')
  QString idColumn;   // колонка первинного ключа ('id')
  QString nameColumn; // колонка для вибірки назви ('name')
};

// Прив'язка виду субконто до субрахунку Плану рахунків
struct AccountSubconto
{
  AccountId accountId; // рахунок, до якого прив'язана аналітика
  int lineNo = 1; // порядковий номер аналітичного розрізу (1, 2 або 3)
  QString subcontoKindId; // посилання на SubcontoKind::id
  bool isRequired = true; // чи є заповнення обов'язковим при проведенні
};

// Одиничне значення субконто в конкретній проводці Головної книги
struct SubcontoValue
{
  QString kindId; // вид аналітики ('counterparty', 'nomenclature', тощо)
  std::int64_t entityId = 0; // первинний ключ об'єкта у відповідній таблиці
  QString name; // найменування об'єкта (заповнюється при формуванні звітів)

  bool empty() const { return kindId.trimmed().isEmpty() || entityId <= 0; }
  void clear()
  {
    kindId.clear();
    entityId = 0;
    name.clear();
  }
};

// Набір значень субконто (до 3 розрізів) для одного плеча проводки (Дебету або Кредиту)
struct SubcontoSet
{
  SubcontoValue s1; // Субконто 1
  SubcontoValue s2; // Субконто 2
  SubcontoValue s3; // Субконто 3

  const SubcontoValue & at(int index) const
  {
    if (index == 1)
      return s1;
    if (index == 2)
      return s2;
    return s3;
  }

  SubcontoValue & at(int index)
  {
    if (index == 1)
      return s1;
    if (index == 2)
      return s2;
    return s3;
  }

  bool empty() const { return s1.empty() && s2.empty() && s3.empty(); }
};

// Системні ідентифікатори стандартних видів субконто
namespace subconto_kinds {
inline const QString Counterparty = QStringLiteral("counterparty");
inline const QString Nomenclature = QStringLiteral("nomenclature");
inline const QString FixedAsset = QStringLiteral("fixed_asset");
inline const QString IntangibleAsset = QStringLiteral("intangible_asset");
inline const QString LowValue = QStringLiteral("low_value");
inline const QString Employee = QStringLiteral("employee");
inline const QString Contract = QStringLiteral("contract");
} // namespace subconto_kinds

} // namespace fin
