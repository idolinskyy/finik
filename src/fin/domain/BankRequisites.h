#pragma once

// Банківські реквізити компанії / контрагента.

#include "fin/core/Result.h"

#include <QString>

namespace fin {

// Нормалізує рядок IBAN: видаляє пробіли, дефіси та переводить у верхній регістр.
QString normalizeIban(const QString & iban);

// Витягує 6-значний код банку (МФО) з рядка українського IBAN (символи з 5-го по 10-й).
QString mfoFromIban(const QString & iban);

// Перевіряє коректність формату та контрольної суми MOD-97 для IBAN.
Status validateIban(const QString & iban);

// Перевіряє коректність коду МФО (рівно 6 десяткових цифр).
Status validateMfo(const QString & mfo);

// Комплексна перевірка: валідність IBAN, МФО та їх взаємна відповідність.
Status validateBankRequisites(const QString & iban, const QString & mfo);

} // namespace fin
