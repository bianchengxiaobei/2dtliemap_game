#pragma once

#include "characters/systems/StealthRules.h"

namespace zl::core {
class Config;
}

namespace zl::game {

characters::StealthRuleConfig LoadStealthRuleConfig(const core::Config& config);

}  // namespace zl::game
