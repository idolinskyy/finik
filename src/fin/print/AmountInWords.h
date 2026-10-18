#pragma once

// Сума прописом (укр.).

#include "fin/core/Money.h"
#include "fin/core/Result.h"

#include <QString>

namespace fin {

// Перетворює суму в гривнях та копійках у словесний вираз українською мовою
Result<QString> moneyToUkrainianWords(Money amount);

} // namespace fin
