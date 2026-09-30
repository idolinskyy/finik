#include "fin/core/Money.h"
#include "fin/core/Quantity.h"

#include <limits>

#include <limits>

#include <QString>

#include <catch2/catch_test_macros.hpp>

using fin::Money;
using fin::Quantity;

TEST_CASE("Money parses and formats hryvnia without floating point")
{
  const auto parsed = Money::fromDisplayString(QStringLiteral("100 000.50"));
  REQUIRE(parsed);
  REQUIRE(parsed.value().cents() == 10000050);
  REQUIRE(parsed.value().toDisplay() == QStringLiteral("100\u00A0000.50"));

  const auto comma = Money::fromDisplayString(QStringLiteral("10,5"));
  REQUIRE(comma);
  REQUIRE(comma.value().cents() == 1050);

  const auto roundtrip = Money::fromDisplayString(parsed.value().toDisplay());
  REQUIRE(roundtrip);
  REQUIRE(roundtrip.value() == parsed.value());
}

TEST_CASE("Money rejects malformed and oversized fractional input")
{
  REQUIRE_FALSE(Money::fromDisplayString(QString()));
  REQUIRE_FALSE(Money::fromDisplayString(QStringLiteral("abc")));
  REQUIRE_FALSE(Money::fromDisplayString(QStringLiteral("10.501")));
  REQUIRE_FALSE(Money::fromDisplayString(QStringLiteral("10.50.1")));
}

TEST_CASE("Money arithmetic detects overflow")
{
  const Money max = Money::fromCents(std::numeric_limits<std::int64_t>::max());
  REQUIRE_FALSE(max.plus(Money::fromCents(1)));
  REQUIRE(Money::fromCents(200).minus(Money::fromCents(50)).value().cents() == 150);
}

TEST_CASE("Negative money roundtrips")
{
  const auto parsed = Money::fromDisplayString(QStringLiteral("-1.01"));
  REQUIRE(parsed);
  REQUIRE(parsed.value().cents() == -101);
  REQUIRE(parsed.value().toDisplay() == QStringLiteral("-1.01"));
}

TEST_CASE("Quantity uses milliunits and multiplies into money")
{
  const auto parsed = Quantity::fromDisplayString(QStringLiteral("10,5"));
  REQUIRE(parsed);
  REQUIRE(parsed.value().milli() == 10500);
  REQUIRE(parsed.value().toDisplay() == QStringLiteral("10.5"));

  const auto empty = Quantity::fromDisplayString(QString());
  REQUIRE(empty);
  REQUIRE(empty.value().isZero());

  REQUIRE_FALSE(Quantity::fromDisplayString(QStringLiteral("1.2345")));

  const auto amount = Quantity::fromMilli(10000).times(Money::fromCents(40000));
  REQUIRE(amount);
  REQUIRE(amount.value().cents() == 400000);
}
