#include "game/StealthConfig.h"

#include <string>

#include "core/Config.h"
#include "core/Log.h"

namespace zl::game {

characters::StealthRuleConfig LoadStealthRuleConfig(const core::Config& config) {
    characters::StealthRuleConfig rules = characters::DefaultStealthRuleConfig();
    const auto read = [&config](const char* key, float fallback) {
        return static_cast<float>(
            config.Get<double>(std::string("stealth.") + key, fallback));
    };

    rules.enabled = config.Get<bool>("stealth.enabled", rules.enabled);
    rules.crouchSpeedScale = read("crouchSpeedScale", rules.crouchSpeedScale);
    rules.stepNoiseScale = read("stepNoiseScale", rules.stepNoiseScale);
    rules.visibilityScale = read("visibilityScale", rules.visibilityScale);
    rules.backstabMultiplier = read("backstabMultiplier", rules.backstabMultiplier);
    rules.backstabHalfAngleDegrees =
        read("backstabHalfAngleDegrees", rules.backstabHalfAngleDegrees);
    rules.exposeOnAttack =
        config.Get<bool>("stealth.exposeOnAttack", rules.exposeOnAttack);

    const characters::StealthRuleConfig safe =
        characters::SanitizeStealthRuleConfig(rules);
    if (safe.crouchSpeedScale != rules.crouchSpeedScale ||
        safe.stepNoiseScale != rules.stepNoiseScale ||
        safe.visibilityScale != rules.visibilityScale ||
        safe.backstabMultiplier != rules.backstabMultiplier ||
        safe.backstabHalfAngleDegrees != rules.backstabHalfAngleDegrees) {
        ZL_WARN("stealth", "stealth.* 存在非法数值，已回退默认值");
    }
    return safe;
}

}  // namespace zl::game
