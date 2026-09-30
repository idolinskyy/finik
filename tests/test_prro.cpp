#include <catch2/catch_test_macros.hpp>

#include "fin/prro/CheckboxClient.h"

using namespace fin;

TEST_CASE("CheckboxClient::buildSellPayload minimal")
{
  std::vector<CheckboxSellLine> goods;
  CheckboxSellLine a;
  a.code = QStringLiteral("SKU-1");
  a.name = QStringLiteral("Хліб");
  a.unitPriceCents = 2500;
  a.quantityMilli = 2000; // 2 шт
  goods.push_back(a);

  Result<QByteArray> json = CheckboxClient::buildSellPayload(
      QStringLiteral("11111111-1111-1111-1111-111111111111"), goods, QStringLiteral("CASH"));
  REQUIRE(json);
  const QString text = QString::fromUtf8(json.value());
  REQUIRE(text.contains(QStringLiteral("\"type\":\"CASH\"")));
  REQUIRE(text.contains(QStringLiteral("\"price\":2500")));
  REQUIRE(text.contains(QStringLiteral("\"quantity\":2000")));
  REQUIRE(text.contains(QStringLiteral("\"value\":5000"))); // 2500*2
  REQUIRE_FALSE(text.contains(QStringLiteral("is_return")));
}

TEST_CASE("CheckboxClient::buildSellPayload return with related receipt")
{
  std::vector<CheckboxSellLine> goods;
  CheckboxSellLine a;
  a.code = QStringLiteral("SKU-1");
  a.name = QStringLiteral("Хліб");
  a.unitPriceCents = 2500;
  a.quantityMilli = 1000;
  goods.push_back(a);

  Result<QByteArray> json = CheckboxClient::buildSellPayload(
      QStringLiteral("22222222-2222-2222-2222-222222222222"), goods, QStringLiteral("CASH"), true,
      QStringLiteral("11111111-1111-1111-1111-111111111111"));
  REQUIRE(json);
  const QString text = QString::fromUtf8(json.value());
  REQUIRE(text.contains(QStringLiteral("\"is_return\":true")));
  REQUIRE(text.contains(
      QStringLiteral("\"related_receipt_id\":\"11111111-1111-1111-1111-111111111111\"")));
}

TEST_CASE("CheckboxClient::parseReceiptResponse")
{
  const QByteArray body = R"({
    "id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
    "fiscal_code": "1234567890",
    "status": "DONE"
  })";
  Result<CheckboxReceiptInfo> info = CheckboxClient::parseReceiptResponse(body);
  REQUIRE(info);
  REQUIRE(info.value().fiscalCode == QStringLiteral("1234567890"));
  REQUIRE(info.value().status == QStringLiteral("DONE"));
}

TEST_CASE("CheckboxClient::parseReceiptResponse error message")
{
  const QByteArray body = R"({"message":"shift is closed"})";
  Result<CheckboxReceiptInfo> info = CheckboxClient::parseReceiptResponse(body);
  REQUIRE_FALSE(info);
  REQUIRE(info.error().message.contains(QStringLiteral("shift is closed")));
}
