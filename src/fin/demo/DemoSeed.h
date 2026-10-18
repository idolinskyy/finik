#pragma once

#include "fin/core/Result.h"
#include "fin/session/LedgerSession.h"

#include <cstdint>

namespace fin {

Status seedDemoData(LedgerSession & session, std::uint32_t seed = 20260915);

} // namespace fin
