#include <catch2/catch_test_macros.hpp>

#include "fin/bank/MonobankClient.h"

using namespace fin;

TEST_CASE("MonobankClient::parseJson")
{
  const QByteArray json = R"(
  [
    {
      "id": "su2IG-22",
      "time": 1554466347,
      "description": "Покупка в магазині",
      "mcc": 5411,
      "amount": -15000,
      "operationAmount": -15000,
      "currencyCode": 980,
      "commissionRate": 0,
      "cashbackAmount": 150,
      "balance": 100000,
      "hold": false
    },
    {
      "id": "abc-123",
      "time": 1554466400,
      "description": "Переказ на картку",
      "amount": 50000,
      "currencyCode": 980,
      "counterEdrpou": "12345678",
      "counterName": "ТОВ Контрагент"
    }
  ]
  )";

  Result<StatementParse> parsed = MonobankClient::parseJson(json);
  REQUIRE(parsed);
  REQUIRE(parsed.value().rows.size() == 2);

  const StatementRow & r1 = parsed.value().rows[0];
  REQUIRE(r1.incoming == false);
  REQUIRE(r1.amount == Money::fromCents(15000));
  REQUIRE(r1.purpose == QStringLiteral("Покупка в магазині"));
  REQUIRE(r1.edrpou == QStringLiteral("00000000")); // fallback since no counterEdrpou
  REQUIRE(r1.partyName == QStringLiteral("Клієнт Monobank"));
  REQUIRE(r1.date == QDateTime::fromSecsSinceEpoch(1554466347).date());

  const StatementRow & r2 = parsed.value().rows[1];
  REQUIRE(r2.incoming == true);
  REQUIRE(r2.amount == Money::fromCents(50000));
  REQUIRE(r2.purpose == QStringLiteral("Переказ на картку"));
  REQUIRE(r2.edrpou == QStringLiteral("12345678"));
  REQUIRE(r2.partyName == QStringLiteral("ТОВ Контрагент"));
}
