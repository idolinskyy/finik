#pragma once

// Strong IDs: різні типи Id не змішуються на етапі компіляції.

#include <cstdint>

namespace fin {

template <typename Tag> struct StrongId
{
  std::int64_t value = 0;

  constexpr bool isValid() const noexcept { return value > 0; }
  constexpr explicit operator bool() const noexcept { return isValid(); }

  friend constexpr bool operator==(StrongId a, StrongId b) noexcept { return a.value == b.value; }
  friend constexpr bool operator!=(StrongId a, StrongId b) noexcept { return !(a == b); }
};

// Теги для створення несумісних типів ідентифікаторів
struct AccountTag
{};
struct CounterpartyTag
{};
struct DocumentTag
{};
struct LedgerEntryTag
{};
struct NomenclatureTag
{};
struct FixedAssetTag
{};
struct LowValueTag
{};
struct IntangibleTag
{};
struct UserTag
{};
struct EmployeeTag
{};
struct WarehouseTag
{};
struct ContractTag
{};
struct BomTag
{};
struct HrOrderTag
{};

using AccountId = StrongId<AccountTag>;
using CounterpartyId = StrongId<CounterpartyTag>;
using DocumentId = StrongId<DocumentTag>;
using LedgerEntryId = StrongId<LedgerEntryTag>;
using NomenclatureId = StrongId<NomenclatureTag>;
using FixedAssetId = StrongId<FixedAssetTag>;
using LowValueId = StrongId<LowValueTag>;
using IntangibleId = StrongId<IntangibleTag>;
using UserId = StrongId<UserTag>;
using EmployeeId = StrongId<EmployeeTag>;
using WarehouseId = StrongId<WarehouseTag>;
using ContractId = StrongId<ContractTag>;
using BomId = StrongId<BomTag>;
using HrOrderId = StrongId<HrOrderTag>;

} // namespace fin
