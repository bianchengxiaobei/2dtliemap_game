#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/InfectionSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/InfectionStatus.h"

using namespace zl;

namespace {

ecs::Wounds MakeWounds(float severity, bool bandaged = false) {
    ecs::Wounds wounds;
    wounds.count = 1;
    wounds.entries[0] =
        ecs::WoundEntry{ecs::DamageType::Bite, severity, 0.0f, 0.0f, 0.0f,
                        bandaged};
    return wounds;
}

}  // namespace

TEST_CASE("M20-F1 感染暴露：爪/咬概率、未包扎伤口加成与确定性判定") {
    const ecs::Wounds empty;
    characters::InfectionParams params;
    CHECK(characters::ExposureChance(ecs::DamageType::Claw, empty, params) ==
          doctest::Approx(0.06f));
    CHECK(characters::ExposureChance(ecs::DamageType::Bite, empty, params) ==
          doctest::Approx(0.20f));
    CHECK(characters::ExposureChance(ecs::DamageType::Blunt, empty, params) ==
          doctest::Approx(0.0f));

    const ecs::Wounds open = MakeWounds(0.4f);
    const float openChance =
        characters::ExposureChance(ecs::DamageType::Bite, open, params);
    CHECK(openChance == doctest::Approx(0.42f));
    const ecs::Wounds bandaged = MakeWounds(0.4f, true);
    CHECK(characters::ExposureChance(ecs::DamageType::Bite, bandaged, params) ==
          doctest::Approx(0.20f));

    params.maxChance = 0.25f;
    CHECK(characters::ExposureChance(ecs::DamageType::Bite, open, params) ==
          doctest::Approx(0.25f));

    params.maxChance = 0.75f;
    ecs::Infection infection;
    CHECK_FALSE(characters::TryInfect(infection, 0.42f, 0.5f, &open, params));
    CHECK(infection.stage == ecs::InfectionStage::None);
    CHECK(characters::TryInfect(infection, 0.42f, 0.41f, &open, params));
    CHECK(infection.stage == ecs::InfectionStage::Incubating);
    CHECK_FALSE(characters::TryInfect(infection, 1.0f, 0.0f, &open, params));
}

TEST_CASE("F6-T2 污染物品感染风险：确定性判定与满风险必感染") {
    const ecs::Wounds open = MakeWounds(0.4f);
    characters::InfectionParams params;
    ecs::Infection guaranteed;
    REQUIRE(characters::TryInfectWithRisk(guaranteed, 1.0f, 42, &open, params));
    CHECK(guaranteed.stage == ecs::InfectionStage::Incubating);

    ecs::Infection first;
    ecs::Infection second;
    const bool firstRoll =
        characters::TryInfectWithRisk(first, 0.35f, 123456, &open, params);
    const bool secondRoll =
        characters::TryInfectWithRisk(second, 0.35f, 123456, &open, params);
    CHECK(firstRoll == secondRoll);
    CHECK(first.stage == second.stage);

    ecs::Infection none;
    CHECK_FALSE(characters::TryInfectWithRisk(none, 0.0f, 1, &open, params));
    CHECK(none.stage == ecs::InfectionStage::None);
}

TEST_CASE("M20-F1 感染阶段：病程、未包扎加速、抗生素抑制与倒计时") {
    characters::InfectionParams params;
    params.totalSeconds = 100.0f;
    params.incubatingEnd = 0.30f;
    params.feverEnd = 0.70f;
    ecs::Infection infection;
    REQUIRE(characters::TryInfect(infection, 1.0f, 0.0f, nullptr, params));
    CHECK(infection.stage == ecs::InfectionStage::Incubating);
    CHECK(infection.timeToNextStage == doctest::Approx(30.0f));

    const ecs::Wounds open = MakeWounds(0.5f);
    CHECK(characters::InfectionProgressPerSecond(infection, &open, params) ==
          doctest::Approx(0.014f));
    infection.slowStrength = 0.5f;
    infection.slowRemaining = 10.0f;
    infection.slowDuration = 10.0f;
    CHECK(characters::InfectionProgressPerSecond(infection, &open, params) ==
          doctest::Approx(0.007f));

    infection.stage = ecs::InfectionStage::Incubating;
    infection.progress = 0.29f;
    infection.slowRemaining = 0.0f;
    infection.slowStrength = 0.0f;
    characters::RefreshInfection(infection, &open, params);
    CHECK(infection.timeToNextStage == doctest::Approx(0.01f / 0.014f));

    infection.progress = 0.31f;
    characters::RefreshInfection(infection, nullptr, params);
    CHECK(infection.stage == ecs::InfectionStage::Fever);
    infection.progress = 0.71f;
    characters::RefreshInfection(infection, nullptr, params);
    CHECK(infection.stage == ecs::InfectionStage::Severe);
}

TEST_CASE("M20-F1 感染治疗：抗生素降低病程并可清除，抑制窗口生效") {
    characters::InfectionParams params;
    ecs::Infection infection;
    infection.stage = ecs::InfectionStage::Fever;
    infection.progress = 0.60f;
    infection.timeToNextStage = 100.0f;

    const characters::InfectionTreatment treated =
        characters::TreatInfection(infection, 0.35f, 0.5f, 60.0f, nullptr,
                                   params);
    CHECK(treated.applied);
    CHECK_FALSE(treated.cured);
    CHECK(treated.progressReduced == doctest::Approx(0.35f));
    CHECK(infection.progress == doctest::Approx(0.25f));
    CHECK(infection.stage == ecs::InfectionStage::Incubating);
    CHECK(infection.slowStrength == doctest::Approx(0.5f));
    CHECK(infection.slowRemaining == doctest::Approx(60.0f));
    CHECK(infection.slowDuration == doctest::Approx(60.0f));

    const characters::InfectionTreatment cured =
        characters::TreatInfection(infection, 0.35f, 0.0f, 0.0f, nullptr,
                                   params);
    CHECK(cured.applied);
    CHECK(cured.cured);
    CHECK(infection.stage == ecs::InfectionStage::None);
    CHECK(infection.progress == doctest::Approx(0.0f));
    CHECK(infection.slowRemaining == doctest::Approx(0.0f));
}

TEST_CASE("M20-F1 感染系统：发热疲劳、重症掉血与感染死因") {
    const characters::InfectionParams params;

    ecs::EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity player = world.Create();
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Health>(player, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::Needs>(player, ecs::Needs{});
    ecs::Infection fever;
    fever.stage = ecs::InfectionStage::Fever;
    fever.progress = 0.40f;
    registry.emplace<ecs::Infection>(player, fever);

    const ecs::Entity zombie = world.Create();
    registry.emplace<ecs::Health>(zombie, ecs::Health{100.0f, 100.0f});
    ecs::Infection zombieInfection;
    zombieInfection.stage = ecs::InfectionStage::Severe;
    zombieInfection.progress = 0.99f;
    registry.emplace<ecs::Infection>(zombie, zombieInfection);

    characters::InfectionSystem system(params);
    system.Update(world, 10.0f);
    CHECK(registry.get<ecs::Needs>(player).fatigue > 0.0f);
    CHECK(registry.get<ecs::Health>(player).current == doctest::Approx(100.0f));
    CHECK(registry.get<ecs::Infection>(player).progress > 0.40f);
    CHECK(registry.get<ecs::Infection>(zombie).progress ==
          doctest::Approx(0.99f));

    auto& infection = registry.get<ecs::Infection>(player);
    infection.stage = ecs::InfectionStage::Severe;
    infection.progress = 0.999f;
    system.Update(world, 1.0f);
    CHECK(registry.get<ecs::Health>(player).current <= 0.0f);
    REQUIRE(registry.try_get<ecs::LastDamage>(player) != nullptr);
    CHECK(registry.get<ecs::LastDamage>(player).cause ==
          ecs::DamageCause::Infection);
}

TEST_CASE("M20-F1 战斗暴露回调在写新伤口之前触发") {
    ecs::EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity target = world.Create();
    registry.emplace<ecs::PlayerTag>(target);
    registry.emplace<ecs::Position>(target, ecs::Position{0.0f, 0.0f, 0});
    registry.emplace<ecs::Health>(target, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::Infection>(target, ecs::Infection{});
    registry.emplace<ecs::Wounds>(target, MakeWounds(0.4f));

    characters::CombatSystem combat;
    int calls = 0;
    float seenSeverity = -1.0f;
    ecs::DamageType seenType = ecs::DamageType::Blunt;
    combat.SetInfectionExposureCallback(
        [&](ecs::Entity victim, ecs::DamageType type, float) {
            ++calls;
            seenType = type;
            const auto* wounds = registry.try_get<ecs::Wounds>(victim);
            seenSeverity = wounds != nullptr
                               ? characters::UnbandagedWoundSeverity(*wounds)
                               : -1.0f;
        });

    ecs::DamageEvent event;
    event.type = ecs::DamageType::Claw;
    event.amount = 10.0f;
    REQUIRE(combat.ApplyDamage(world, target, event));
    CHECK(calls == 1);
    CHECK(seenType == ecs::DamageType::Claw);
    CHECK(seenSeverity == doctest::Approx(0.4f));
    REQUIRE(registry.get<ecs::Wounds>(target).count == 2);

    event.type = ecs::DamageType::Blunt;
    REQUIRE(combat.ApplyDamage(world, target, event));
    CHECK(calls == 1);
}

TEST_CASE("M20-F1 感染 HUD 文案与色调") {
    ecs::Infection infection;
    infection.stage = ecs::InfectionStage::Fever;
    infection.progress = 0.52f;
    infection.timeToNextStage = 120.0f;
    infection.slowStrength = 0.5f;
    infection.slowRemaining = 45.0f;
    std::string tone;
    const std::string text = game::InfectionStatusText(infection, &tone);
    CHECK(text.find("感染：发热/疲劳 52%") != std::string::npos);
    CHECK(text.find("重症约 2 分后") != std::string::npos);
    CHECK(text.find("抗生素抑制 45 秒") != std::string::npos);
    CHECK(tone == "warn");

    infection.stage = ecs::InfectionStage::Severe;
    infection.timeToNextStage = 30.0f;
    infection.slowRemaining = 0.0f;
    const std::string severe = game::InfectionStatusText(infection, &tone);
    CHECK(severe.find("死亡约 30 秒后") != std::string::npos);
    CHECK(tone == "fail");

    infection = {};
    CHECK(game::InfectionStatusText(infection, &tone).empty());
    CHECK(tone == "info");
}
