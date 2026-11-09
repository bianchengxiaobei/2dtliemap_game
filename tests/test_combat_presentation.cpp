#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "characters/components/CombatComponents.h"
#include "game/CombatAudio.h"
#include "game/CombatPresentation.h"

using namespace zl;

TEST_CASE("M30-T9 远端战斗姿态：格挡、弹反窗口与破防") {
    ecs::CombatStance stance;
    game::CombatPresentationState state =
        game::ResolveCombatPresentation(stance, 0.18f, 0.28f);
    CHECK_FALSE(state.visible);
    CHECK_FALSE(state.blocking);
    CHECK_FALSE(state.dodging);

    stance.blocking = true;
    stance.blockElapsed = 0.10f;
    state = game::ResolveCombatPresentation(stance, 0.18f, 0.28f);
    CHECK(state.visible);
    CHECK(state.blocking);
    CHECK(state.parryActive);
    CHECK(state.blockAlpha == doctest::Approx(1.0f));

    stance.blockElapsed = 0.30f;
    state = game::ResolveCombatPresentation(stance, 0.18f, 0.28f);
    CHECK(state.blocking);
    CHECK_FALSE(state.parryActive);
    CHECK(state.blockAlpha == doctest::Approx(0.72f));

    stance.blocking = false;
    stance.guardBreakLeft = 0.8f;
    state = game::ResolveCombatPresentation(stance, 0.18f, 0.28f);
    CHECK(state.visible);
    CHECK(state.guardBroken);
    CHECK_FALSE(state.blocking);
}

TEST_CASE("M30-T9 远端战斗姿态：闪避优先、方向归一与无敌提示") {
    ecs::CombatStance stance;
    stance.blocking = true;
    stance.blockElapsed = 0.05f;
    stance.dodgeLeft = 0.14f;
    stance.dodgeInvulnLeft = 0.09f;
    stance.dodgeDirX = 3.0f;
    stance.dodgeDirY = 4.0f;

    const game::CombatPresentationState state =
        game::ResolveCombatPresentation(stance, 0.18f, 0.28f);
    CHECK(state.visible);
    CHECK(state.dodging);
    CHECK_FALSE(state.blocking);
    CHECK(state.invulnerable);
    CHECK(state.dodgeAlpha == doctest::Approx(0.5f));
    CHECK(state.dodgeDirX == doctest::Approx(0.6f));
    CHECK(state.dodgeDirY == doctest::Approx(0.8f));
}

TEST_CASE("M30-T9 远端战斗姿态：非法方向与非法时长安全回退") {
    ecs::CombatStance stance;
    stance.dodgeLeft = 0.28f;
    stance.dodgeDirX = std::numeric_limits<float>::quiet_NaN();
    stance.dodgeDirY = 0.0f;

    const game::CombatPresentationState state =
        game::ResolveCombatPresentation(
            stance, std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN());
    CHECK(state.visible);
    CHECK(state.dodging);
    CHECK(state.dodgeDirX == doctest::Approx(0.0f));
    CHECK(state.dodgeDirY == doctest::Approx(0.0f));
    CHECK(state.dodgeAlpha == doctest::Approx(1.0f));
    CHECK_FALSE(state.parryActive);
}

TEST_CASE("M30-T10 近战挥击表现：进度、轻重击与非法时长回退") {
    ecs::ActionState action;
    action.action = ecs::ActorAction::Idle;
    ecs::AttackIntent attack;
    CHECK_FALSE(game::ResolveMeleeSwingPresentation(&action, &attack).active);

    action.action = ecs::ActorAction::Attacking;
    action.elapsed = 0.14f;
    action.duration = 0.28f;
    attack.kind = ecs::AttackKind::Light;
    attack.range = 1.7f;
    game::MeleeSwingPresentation light =
        game::ResolveMeleeSwingPresentation(&action, &attack);
    CHECK(light.active);
    CHECK_FALSE(light.heavy);
    CHECK(light.progress == doctest::Approx(0.5f));
    CHECK(light.alpha == doctest::Approx(1.0f));
    CHECK(light.radius == doctest::Approx(43.6f));

    attack.kind = ecs::AttackKind::Heavy;
    game::MeleeSwingPresentation heavy =
        game::ResolveMeleeSwingPresentation(&action, &attack);
    CHECK(heavy.active);
    CHECK(heavy.heavy);
    CHECK(heavy.radius == doctest::Approx(51.6f));
    CHECK(heavy.thickness > light.thickness);

    action.elapsed = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(game::ResolveMeleeSwingPresentation(&action, &attack).active);

    action.elapsed = 0.14f;
    action.duration = 0.0f;
    heavy = game::ResolveMeleeSwingPresentation(&action, nullptr);
    CHECK(heavy.active);
    CHECK(heavy.progress == doctest::Approx(0.5f));
    CHECK_FALSE(heavy.heavy);
}

TEST_CASE("M30-T10 战斗音效：cue 名称、clip 与半径") {
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(game::CombatCue::Count);
         ++i) {
        const auto cue = static_cast<game::CombatCue>(i);
        const char* name = game::CombatCueName(cue);
        const char* clip = game::CombatCueClip(cue);
        CHECK(name != nullptr);
        CHECK(clip != nullptr);
        CHECK(std::string(name).size() > 0);
        CHECK(std::string(clip).size() > 0);
        CHECK(game::CombatCueRadius(cue) > 0.0f);
    }
    CHECK(std::string(game::CombatCueClip(game::CombatCue::LightSwing)) ==
          audio::kMeleeSwingLight);
    CHECK(std::string(game::CombatCueClip(game::CombatCue::GuardBreak)) ==
          audio::kMeleeGuardBreak);
}
