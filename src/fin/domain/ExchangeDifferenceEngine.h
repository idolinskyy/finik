#pragma once

// Регламентна переоцінка монетарних статей (НП(С)БО 21).

#include "fin/core/Result.h"
#include "fin/domain/Account.h"
#include "fin/domain/Document.h"

#include <vector>

#include <QDate>
#include <QSqlDatabase>

namespace fin {

// Формує проводки курсових різниць на кінець періоду:
//   грошові кошти 312 / 302 / 334;
//   розрахунки з іноземними покупцями 362 та постачальниками 632
//   (з аналітикою контрагента на рядку документа).
// Немонетарні аванси 3712 / 6812 не переоцінюються.
// Відсутність курсу НБУ при ненульовому валютному залишку — помилка (fail-closed).
class ExchangeDifferenceEngine
{
 public:
  explicit ExchangeDifferenceEngine(QSqlDatabase db);

  // Додає рядки переоцінки до lines; коригує exchangeGainNet / exchangeLossNet
  // так, щоб подальше закриття 714/944 на 79 врахувало нові проводки.
  Status appendRevaluation(QDate through, const std::vector<Account> & accounts,
                           std::vector<DocumentLine> & lines, std::int64_t & exchangeGainNet,
                           std::int64_t & exchangeLossNet) const;

 private:
  QSqlDatabase db_;
};

} // namespace fin
