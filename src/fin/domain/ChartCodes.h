#pragma once

// Коди плану рахунків (субрахунки українського плану).

namespace fin::chart {

// Допоміжний синтетичний рахунок 00 — використовується для введення
// початкових залишків (OpeningBalance), не входить до стандартного плану.
inline constexpr const char * kAuxiliary = "00";

// Основні засоби (НП(С)БО 7)
inline constexpr const char * kMachinery = "104";        // машини та обладнання
inline constexpr const char * kVehicles = "105";         // транспортні засоби
inline constexpr const char * kOtherFixedAssets = "109"; // інші ОЗ
inline constexpr const char * kFaDepreciation = "131"; // знос ОЗ (контрактивний)
inline constexpr const char * kCapexFa = "152"; // незавершені капіталовкладення в ОЗ

// Нематеріальні активи (НП(С)БО 8)
inline constexpr const char * kSoftware = "125";         // авторські права, ПЗ
inline constexpr const char * kOtherIntangibles = "127"; // інші НМА
inline constexpr const char * kIaAmortization = "133"; // накопичена амортизація НМА
inline constexpr const char * kCapexIa = "154"; // незавершені капіталовкладення в НМА

// Грошові кошти
inline constexpr const char * kCash = "301";        // готівка в касі (UAH)
inline constexpr const char * kForeignCash = "302"; // готівка в інвалюті
inline constexpr const char * kBank = "311"; // поточні рахунки в національній валюті (UAH)
inline constexpr const char * kForeignBank = "312"; // поточні рахунки в іноземній валюті
inline constexpr const char * kForeignTransit = "334"; // грошові кошти в дорозі в іноземній валюті

// Запаси та виробництво
inline constexpr const char * kGoods = "281"; // товари на складі (середньозважена)
inline constexpr const char * kRawMaterials = "201"; // сировина й матеріали
inline constexpr const char * kPurchasedComponents =
    "202"; // купівельні напівфабрикати та комплектуючі
inline constexpr const char * kFuel = "203"; // паливо
inline constexpr const char * kFinishedGoods = "26"; // готова продукція (середньозважена)
inline constexpr const char * kProduction = "231"; // основне виробництво (калькуляція собівартості)
inline constexpr const char * kLowValue = "22"; // малоцінні та швидкозношувані предмети

// Розрахунки з контрагентами та підзвітними особами
inline constexpr const char * kCustomers = "361"; // дебіторська заборгованість покупців (UAH)
inline constexpr const char * kForeignCustomers = "362"; // розрахунки з іноземними покупцями
inline constexpr const char * kAdvancesIssued = "371"; // аванси видані постачальникам (UAH)
inline constexpr const char * kForeignAdvancesIssued = "3712"; // аванси видані в іноземній валюті
inline constexpr const char * kAccountablePersons =
    "372"; // папка: розрахунки з підзвітними особами
inline constexpr const char * kAccountablePersonsUah =
    "3721"; // розрахунки з підзвітними особами (UAH)
inline constexpr const char * kAccountablePersonsVal =
    "3722"; // розрахунки з підзвітними особами в іноземній валюті
inline constexpr const char * kSuppliers =
    "631"; // кредиторська заборгованість постачальників (UAH)
inline constexpr const char * kForeignSuppliers = "632"; // розрахунки з іноземними постачальниками
inline constexpr const char * kAdvancesReceived = "681"; // аванси отримані від покупців (UAH)
inline constexpr const char * kForeignAdvancesReceived =
    "6812"; // аванси одержані в іноземній валюті

// Розрахунки з бюджетом та фондами
// 64* — розрахунки з бюджетом; kVatFolder — батьківська група для фільтрації
inline constexpr const char * kVatFolder = "64"; // папка рахунків ПДВ
inline constexpr const char * kVatBudget = "641"; // ПДВ до сплати в бюджет (чисте зобов'язання)
inline constexpr const char * kVatLiability = "643"; // ПДВ-зобов'язання за відвантаженням
    // (до перекидання на 641 після реєстрації ПН)
inline constexpr const char * kVatCredit = "644"; // ПДВ-кредит за надходженням
    // (до підтвердження реєстрацією вхідної ПН)
inline constexpr const char * kTaxCreditCheques =
    "6442"; // ПДВ-кредит за касовими чеками / без отримання ПН
inline constexpr const char * kPitPayable = "642"; // ПДФО до перерахування
inline constexpr const char * kMilitaryLevy = "645"; // військовий збір до перерахування
inline constexpr const char * kSscPayable = "651"; // ЄСВ до перерахування

// Розрахунки з персоналом
inline constexpr const char * kSalaryPayable = "661"; // нарахована, але не виплачена зарплата

// Доходи
inline constexpr const char * kRevenue = "701"; // дохід від реалізації товарів
inline constexpr const char * kServiceRevenue = "703"; // дохід від реалізації послуг
inline constexpr const char * kOtherIncome = "71"; // інші операційні доходи
inline constexpr const char * kExchangeGain = "714"; // дохід від операційної курсової різниці

// Витрати та фінансовий результат
inline constexpr const char * kProductionCogs =
    "901"; // собівартість реалізованої готової продукції
inline constexpr const char * kCogs = "902"; // собівартість реалізованих товарів
inline constexpr const char * kManufacturingOverhead = "91"; // загальновиробничі витрати
inline constexpr const char * kAdminExpense = "92"; // адміністративні витрати
inline constexpr const char * kDistributionExpense = "93"; // витрати на збут
inline constexpr const char * kExchangeLoss = "944"; // втрати від операційної курсової різниці
inline constexpr const char * kFinancialResult = "79"; // фінансові результати
    // (транзитний рахунок закриття місяця)

// Власний капітал
inline constexpr const char * kCapital = "40"; // статутний капітал
inline constexpr const char * kRetainedEarnings =
    "44"; // нерозподілений прибуток (непокритий збиток)

} // namespace fin::chart
