#pragma once

// Реквізити підприємства та облікові прапорці.

#include <QString>

namespace fin {

// Система оподаткування суб'єкта господарювання
enum class TaxSystem
{
  Simplified,
  General
};

inline QString taxSystemToString(TaxSystem system)
{
  return system == TaxSystem::General ? QStringLiteral("general") : QStringLiteral("simplified");
}

inline TaxSystem taxSystemFromString(const QString & text)
{
  return text.compare(QStringLiteral("general"), Qt::CaseInsensitive) == 0 ? TaxSystem::General
                                                                           : TaxSystem::Simplified;
}

struct CompanyProfile
{
  QString name = QStringLiteral("Підприємство"); // найменування підприємства / ФОП
  QString edrpou; // код ЄДРПОУ (8 цифр) або РНОКПП (10 цифр)
  QString iban; // розрахунковий рахунок у форматі UA... (29 символів)
  QString mfo;        // код банку (6 цифр)
  QString bankName;   // найменування банківської установи
  QString address;    // юридична / фактична адреса
  QString accountant; // ПІБ головного бухгалтера
  QString cashier;    // ПІБ касира
  TaxSystem taxSystem = TaxSystem::Simplified; // система оподаткування
  int singleTaxRateBp = 500; // ставка єдиного податку у б.п. (500 = 5.00%)
  bool vatPayer = false;     // чи є платником ПДВ
  int vatRateBp = 2000;      // базова ставка ПДВ у б.п. (2000 = 20.00%)
  QString monobankToken;     // токен доступу до API Monobank
  bool allowNegativeStock = false; // дозвіл від'ємних складських залишків

  // Checkbox ПРРО (програмний РРО)
  bool prroEnabled = false;
  QString checkboxLicenseKey;    // X-License-Key каси
  QString checkboxCashierLogin;  // логін касира
  QString checkboxCashierPin;    // PIN / пароль касира (seal)
  bool prroPaymentCash = true;   // true = CASH, false = CASHLESS

  // Коди ДПІ для DECLARHEAD (C_REG / C_RAJ / C_STI_ORIG). 0 = не задано.
  int taxOfficeRegion = 0;   // область / м. Київ (напр. 26)
  int taxOfficeDistrict = 0; // район ДПІ (напр. 50)
  int taxOfficeCode = 0;     // код ДПІ (напр. 2650)

  // Формує повний реквізитний рядок для шапки первинних документів
  QString heading() const;
};

} // namespace fin
