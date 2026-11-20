#include <cmath>
#include <limits>
#include <string>

#include <doctest/doctest.h>

#include "characters/components/CharacterComponents.h"
#include "characters/systems/MovementSystem.h"
#include "characters/systems/StealthRules.h"
#include "core/Config.h"
#include "core/Input.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/StealthConfig.h"
#include "platform/KeyNames.h"
#include "ui/HudState.h"
#include "ui/MenuUi.h"

using namespace zl;

TEST_CASE("M34-F1-T1 潜行规则默认值、范围与非法回退") {
    const characters::StealthRuleConfig defaults =
        characters::DefaultStealthRuleConfig();
    CHECK(defaults.enabled);
    CHECK(defaults.crouchSpeedScale == doctest::Approx(0.55f));
    CHECK(defaults.stepNoiseScale == doctest::Approx(0.45f));
    CHECK(defaults.visibilityScale == doctest::Approx(0.65f));
    CHECK(defaults.backstabMultiplier == doctest::Approx(1.6f));
    CHECK(defaults.backstabHalfAngleDegrees == doctest::Approx(60.0f));
    CHECK(defaults.exposeOnAttack);

    characters::StealthRuleConfig invalid;
    invalid.crouchSpeedScale = 0.0f;
    invalid.stepNoiseScale = -0.1f;
    invalid.visibilityScale = 1.1f;
    invalid.backstabMultiplier = 0.5f;
    invalid.backstabHalfAngleDegrees = 180.1f;
    const characters::StealthRuleConfig safe =
        characters::SanitizeStealthRuleConfig(invalid);
    CHECK(safe.crouchSpeedScale == doctest::Approx(defaults.crouchSpeedScale));
    CHECK(safe.stepNoiseScale == doctest::Approx(defaults.stepNoiseScale));
    CHECK(safe.visibilityScale == doctest::Approx(defaults.visibilityScale));
    CHECK(safe.backstabMultiplier == doctest::Approx(defaults.backstabMultiplier));
    CHECK(safe.backstabHalfAngleDegrees ==
          doctest::Approx(defaults.backstabHalfAngleDegrees));

    characters::StealthRuleConfig nanValues;
    nanValues.crouchSpeedScale = std::numeric_limits<float>::quiet_NaN();
    nanValues.stepNoiseScale = std::numeric_limits<float>::infinity();
    const characters::StealthRuleConfig nanSafe =
        characters::SanitizeStealthRuleConfig(nanValues);
    CHECK(nanSafe.crouchSpeedScale == doctest::Approx(defaults.crouchSpeedScale));
    CHECK(nanSafe.stepNoiseScale == doctest::Approx(defaults.stepNoiseScale));
}

TEST_CASE("M34-F1-T1 潜行状态骨架按姿态派生噪声与可视倍率") {
    characters::StealthRuleConfig rules = characters::DefaultStealthRuleConfig();
    rules.stepNoiseScale = 0.35f;
    rules.visibilityScale = 0.7f;

    ecs::StealthState standing;
    CHECK_FALSE(standing.crouched);
    CHECK(standing.speedScale == doctest::Approx(1.0f));
    CHECK(standing.noiseScale == doctest::Approx(1.0f));
    CHECK(standing.visibilityScale == doctest::Approx(1.0f));

    const characters::StealthScales crouched =
        characters::StealthScalesFor(true, rules);
    CHECK(crouched.speedScale == doctest::Approx(0.55f));
    CHECK(crouched.noiseScale == doctest::Approx(0.35f));
    CHECK(crouched.visibilityScale == doctest::Approx(0.7f));
    const characters::StealthScales stood =
        characters::StealthScalesFor(false, rules);
    CHECK(stood.speedScale == doctest::Approx(1.0f));
    CHECK(stood.noiseScale == doctest::Approx(1.0f));
    CHECK(stood.visibilityScale == doctest::Approx(1.0f));
}

TEST_CASE("M34-F1-T2 潜行速度/脚步缩放边界与取消条件") {
    characters::StealthRuleConfig rules = characters::DefaultStealthRuleConfig();
    rules.crouchSpeedScale = 0.55f;
    rules.stepNoiseScale = 0.45f;

    CHECK(characters::StealthFootstepRadius(2.5f, true, rules) ==
          doctest::Approx(1.125f));
    CHECK(characters::StealthFootstepRadius(2.5f, false, rules) ==
          doctest::Approx(2.5f));
    CHECK(characters::StealthFootstepRadius(-1.0f, true, rules) ==
          doctest::Approx(0.0f));
    CHECK(characters::StealthFootstepRadius(
              std::numeric_limits<float>::quiet_NaN(), true, rules) ==
          doctest::Approx(0.0f));

    characters::StealthRuleConfig disabled = rules;
    disabled.enabled = false;
    const characters::StealthScales disabledScales =
        characters::StealthScalesFor(true, disabled);
    CHECK(disabledScales.speedScale == doctest::Approx(1.0f));
    CHECK(disabledScales.noiseScale == doctest::Approx(1.0f));
    CHECK(disabledScales.visibilityScale == doctest::Approx(1.0f));
    CHECK(characters::StealthFootstepRadius(2.5f, true, disabled) ==
          doctest::Approx(2.5f));

    characters::StealthRuleConfig invalid = rules;
    invalid.crouchSpeedScale = 0.0f;
    invalid.stepNoiseScale = -1.0f;
    const characters::StealthScales fallback =
        characters::StealthScalesFor(true, invalid);
    CHECK(fallback.speedScale ==
          doctest::Approx(characters::DefaultStealthRuleConfig().crouchSpeedScale));
    CHECK(fallback.noiseScale ==
          doctest::Approx(characters::DefaultStealthRuleConfig().stepNoiseScale));

    CHECK_FALSE(characters::StealthExitRequired(
        false, characters::StealthExitReason::Attack));
    CHECK(characters::StealthExitRequired(
        true, characters::StealthExitReason::Attack));
    CHECK(characters::StealthExitRequired(
        true, characters::StealthExitReason::Throw));
    CHECK(characters::StealthExitRequired(
        true, characters::StealthExitReason::Damage));
    CHECK(characters::StealthExitRequired(
        true, characters::StealthExitReason::Vehicle));
    CHECK(characters::StealthExitRequired(
        true, characters::StealthExitReason::Downed));
}

TEST_CASE("M34-F1-T4 背刺纯函数：未警觉、背后扇区与倍率边界") {
    using ecs::AiStatus;
    CHECK(characters::StealthTargetUnaware(AiStatus::Idle, 0.0f));
    CHECK(characters::StealthTargetUnaware(AiStatus::Wander, 0.49f));
    CHECK_FALSE(characters::StealthTargetUnaware(AiStatus::Idle, 0.5f));
    CHECK_FALSE(characters::StealthTargetUnaware(AiStatus::Idle, -0.1f));
    CHECK_FALSE(characters::StealthTargetUnaware(
        AiStatus::Idle, std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(characters::StealthTargetUnaware(AiStatus::Chase, 0.0f));
    CHECK_FALSE(characters::StealthTargetUnaware(AiStatus::Attack, 0.0f));
    CHECK_FALSE(characters::StealthTargetUnaware(
        AiStatus::SearchLastKnown, 0.0f));
    CHECK_FALSE(characters::StealthTargetUnaware(AiStatus::Dead, 0.0f));

    constexpr float kSin60 = 0.8660254f;
    constexpr float kCos60 = 0.5f;
    constexpr float kSin61 = 0.8746197f;
    constexpr float kCos61 = 0.4848096f;
    CHECK(characters::StealthBehindTarget(0.0f, 1.0f, 0.0f, -1.0f, 60.0f));
    CHECK_FALSE(characters::StealthBehindTarget(0.0f, 1.0f, 0.0f, 1.0f, 60.0f));
    CHECK_FALSE(characters::StealthBehindTarget(0.0f, 1.0f, 1.0f, 0.0f, 60.0f));
    CHECK(characters::StealthBehindTarget(0.0f, 1.0f, kSin60, -kCos60, 60.0f));
    CHECK_FALSE(
        characters::StealthBehindTarget(0.0f, 1.0f, kSin61, -kCos61, 60.0f));
    CHECK_FALSE(characters::StealthBehindTarget(
        0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 60.0f));
    CHECK_FALSE(characters::StealthBehindTarget(0.0f, 0.0f, 0.0f, -1.0f, 60.0f));

    CHECK(characters::StealthBackstabDamageScale(true, 1.6f) ==
          doctest::Approx(1.6f));
    CHECK(characters::StealthBackstabDamageScale(false, 1.6f) ==
          doctest::Approx(1.0f));
    CHECK(characters::StealthBackstabDamageScale(true, 1.0f) ==
          doctest::Approx(1.0f));
    CHECK(characters::StealthBackstabDamageScale(true, 99.0f) ==
          doctest::Approx(3.0f));
    CHECK(characters::StealthBackstabDamageScale(
              true, std::numeric_limits<float>::quiet_NaN()) ==
          doctest::Approx(1.0f));
    CHECK(characters::StealthExitRequired(
        true, characters::StealthExitReason::Attack));
}

TEST_CASE("M34-F1-T2 MovementSystem 只缩放低姿态玩家移速") {
    ecs::EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity player = world.Create();
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Position>(player, ecs::Position{0.0f, 0.0f, 0});
    registry.emplace<ecs::Velocity>(player, ecs::Velocity{});
    registry.emplace<ecs::MoveIntent>(player, ecs::MoveIntent{4.0f, 0.0f});
    ecs::StealthState stealth;
    stealth.crouched = true;
    stealth.speedScale = 0.55f;
    registry.emplace<ecs::StealthState>(player, stealth);

    characters::MovementSystem movement;
    movement.Update(world, 0.5f);
    CHECK(registry.get<ecs::Velocity>(player).x == doctest::Approx(2.2f));
    CHECK(registry.get<ecs::Position>(player).x == doctest::Approx(1.1f));

    auto& standing = registry.get<ecs::StealthState>(player);
    standing.crouched = false;
    standing.speedScale = 1.0f;
    auto& velocity = registry.get<ecs::Velocity>(player);
    velocity.x = 4.0f;
    movement.Update(world, 0.5f);
    CHECK(registry.get<ecs::Velocity>(player).x == doctest::Approx(4.0f));
    CHECK(registry.get<ecs::Position>(player).x == doctest::Approx(3.1f));

    standing.crouched = true;
    standing.speedScale = 0.0f;
    velocity.x = 4.0f;
    movement.Update(world, 0.5f);
    CHECK(registry.get<ecs::Velocity>(player).x == doctest::Approx(0.4f));
}

TEST_CASE("M34-F1-T2 潜行 HUD 文案与噪声等级") {
    ui::HudState hud;
    std::string tone;
    CHECK(ui::StealthStatusText(hud, &tone).empty());
    CHECK(tone == "info");

    hud.stealthValid = true;
    hud.stealthCrouched = true;
    hud.stealthMoveSpeed = 2.2f;
    hud.stealthFootstepRadius = 1.125f;
    CHECK(ui::StealthStatusText(hud, &tone) ==
          "潜行 · 2.2 格/s · 脚步 1.1 格");
    CHECK(tone == "ok");

    hud.stealthCrouched = false;
    hud.stealthMoveSpeed = 4.0f;
    hud.stealthFootstepRadius = 2.5f;
    CHECK(ui::StealthStatusText(hud, &tone) ==
          "站立 · 4.0 格/s · 脚步 2.5 格");
    CHECK(tone == "info");

    hud.stealthMoveSpeed = std::numeric_limits<float>::quiet_NaN();
    hud.stealthFootstepRadius = std::numeric_limits<float>::infinity();
    CHECK(ui::StealthStatusText(hud, &tone).find("0.0 格/s") !=
          std::string::npos);
    CHECK(ui::StealthStatusText(hud, &tone).find("脚步 0.0 格") !=
          std::string::npos);
}

TEST_CASE("M34-F1-T6 潜行 HUD 附近警觉/背刺提示与抑制") {
    ui::HudState hud;
    std::string tone;
    hud.stealthValid = true;
    hud.stealthCrouched = true;
    hud.stealthMoveSpeed = 2.2f;
    hud.stealthFootstepRadius = 1.125f;
    CHECK(ui::StealthStatusText(hud, &tone) ==
          "潜行 · 2.2 格/s · 脚步 1.1 格");
    CHECK(tone == "ok");

    hud.stealthAwarenessValid = true;
    hud.stealthNearbyAlertCount = 0;
    CHECK(ui::StealthStatusText(hud, &tone) ==
          "潜行 · 2.2 格/s · 脚步 1.1 格 · 附近警觉 0");
    hud.stealthNearbyAlertCount = 2;
    hud.stealthBackstabAvailable = true;
    CHECK(ui::StealthStatusText(hud, &tone) ==
          "潜行 · 2.2 格/s · 脚步 1.1 格 · 附近警觉 2 · 可背刺");

    hud.stealthAwarenessValid = false;
    hud.stealthBackstabAvailable = true;
    CHECK(ui::StealthStatusText(hud, &tone).find("可背刺") ==
          std::string::npos);

    CHECK((ui::HudStealthPromptSuppressed(true, false, false, false, false)));
    CHECK((ui::HudStealthPromptSuppressed(false, true, false, false, false)));
    CHECK((ui::HudStealthPromptSuppressed(false, false, true, false, false)));
    CHECK((ui::HudStealthPromptSuppressed(false, false, false, true, false)));
    CHECK((ui::HudStealthPromptSuppressed(false, false, false, false, true)));
    CHECK_FALSE(
        (ui::HudStealthPromptSuppressed(false, false, false, false, false)));
}

TEST_CASE("M34-F1-T1 stealth 配置读取、旧配置回退与非法值保护") {
    core::Config shipped;
    REQUIRE_MESSAGE(shipped.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"),
                    "config/game.json 加载失败");
    const characters::StealthRuleConfig loaded =
        game::LoadStealthRuleConfig(shipped);
    CHECK(loaded.enabled);
    CHECK(loaded.crouchSpeedScale == doctest::Approx(0.55f));
    CHECK(loaded.stepNoiseScale == doctest::Approx(0.45f));
    CHECK(loaded.visibilityScale == doctest::Approx(0.65f));
    CHECK(loaded.backstabMultiplier == doctest::Approx(1.6f));
    CHECK(loaded.backstabHalfAngleDegrees == doctest::Approx(60.0f));
    CHECK(loaded.exposeOnAttack);

    core::Config custom;
    custom.Set("stealth.enabled", false);
    custom.Set("stealth.crouchSpeedScale", 0.7);
    custom.Set("stealth.stepNoiseScale", 0.2);
    custom.Set("stealth.visibilityScale", 0.8);
    custom.Set("stealth.backstabMultiplier", 2.0);
    custom.Set("stealth.backstabHalfAngleDegrees", 45.0);
    custom.Set("stealth.exposeOnAttack", false);
    const characters::StealthRuleConfig customRules =
        game::LoadStealthRuleConfig(custom);
    CHECK_FALSE(customRules.enabled);
    CHECK(customRules.crouchSpeedScale == doctest::Approx(0.7f));
    CHECK(customRules.stepNoiseScale == doctest::Approx(0.2f));
    CHECK(customRules.visibilityScale == doctest::Approx(0.8f));
    CHECK(customRules.backstabMultiplier == doctest::Approx(2.0f));
    CHECK(customRules.backstabHalfAngleDegrees == doctest::Approx(45.0f));
    CHECK_FALSE(customRules.exposeOnAttack);

    core::Config legacy;
    CHECK(legacy.Get<std::string>("input.bindings.Stealth", "N") == "N");
    const characters::StealthRuleConfig legacyRules =
        game::LoadStealthRuleConfig(legacy);
    CHECK(legacyRules.crouchSpeedScale ==
          doctest::Approx(characters::DefaultStealthRuleConfig().crouchSpeedScale));
    CHECK(legacyRules.enabled);

    core::Config invalid;
    invalid.Set("stealth.crouchSpeedScale", 5.0);
    invalid.Set("stealth.stepNoiseScale", -2.0);
    invalid.Set("stealth.visibilityScale", 0.0);
    invalid.Set("stealth.backstabMultiplier", 99.0);
    invalid.Set("stealth.backstabHalfAngleDegrees", 999.0);
    const characters::StealthRuleConfig invalidRules =
        game::LoadStealthRuleConfig(invalid);
    CHECK(invalidRules.crouchSpeedScale ==
          doctest::Approx(characters::DefaultStealthRuleConfig().crouchSpeedScale));
    CHECK(invalidRules.stepNoiseScale ==
          doctest::Approx(characters::DefaultStealthRuleConfig().stepNoiseScale));
    CHECK(invalidRules.visibilityScale ==
          doctest::Approx(characters::DefaultStealthRuleConfig().visibilityScale));
    CHECK(invalidRules.backstabMultiplier ==
          doctest::Approx(characters::DefaultStealthRuleConfig().backstabMultiplier));
    CHECK(invalidRules.backstabHalfAngleDegrees ==
          doctest::Approx(characters::DefaultStealthRuleConfig()
                              .backstabHalfAngleDegrees));
}

TEST_CASE("M34-F1-T1 Stealth 输入动作、默认绑定与旧配置补键入口存在") {
    core::Config shipped;
    REQUIRE_MESSAGE(shipped.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"),
                    "config/game.json 加载失败");
    CHECK(shipped.Get<std::string>("input.bindings.Stealth", "") == "N");

    bool menuBindingFound = false;
    for (const ui::MenuKeyBindingDef& def : ui::kMenuBindingDefs) {
        if (std::string(def.action) == "Stealth") {
            menuBindingFound = true;
            CHECK(std::string(def.defaultKey) == "N");
            CHECK(std::string(def.elementId) == "bind-Stealth");
        }
    }
    CHECK(menuBindingFound);

    const int key = platform::KeyCodeFromName("N");
    REQUIRE(key >= 0);
    core::Input input;
    input.Bind("Stealth", key);
    CHECK(input.HasAction("Stealth"));
    input.SetKeyState(key, true);
    CHECK(input.IsActionPressed("Stealth"));
    input.NewFrame();
    CHECK_FALSE(input.IsActionPressed("Stealth"));
    CHECK(input.IsActionDown("Stealth"));
    input.SetGameplayBlocked(true);
    CHECK_FALSE(input.IsActionDown("Stealth"));
}
