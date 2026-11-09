#include <cmath>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "characters/systems/CombatSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/ProgressionRules.h"

using namespace zl;

namespace {

game::ProgressionConfig TestConfig() {
    game::ProgressionConfig config = game::DefaultProgressionConfig();
    config.xpBase = 100.0f;
    config.xpGrowth = 2.0f;
    config.maxLevel = 4;
    for (game::SkillRules& skill : config.skills) {
        skill.xpBase = 50.0f;
        skill.xpGrowth = 2.0f;
    }
    return config;
}

}  // namespace

TEST_CASE("M19-F1 经验按行为发放、升级给 Perk，且不会负经验/重复点") {
    const game::ProgressionConfig config = TestConfig();
    ecs::Progression progression;

    const game::ProgressionAward first =
        game::AwardXp(progression, ecs::SkillKind::Melee, 120.0f, config);
    CHECK(first.characterXp == doctest::Approx(120.0f));
    CHECK(first.characterLevels == 1);
    CHECK(first.perkPoints == 1);
    CHECK(progression.level == 2);
    CHECK(progression.xp == doctest::Approx(20.0f));
    CHECK(progression.perkPoints == 1);
    CHECK(progression.skills[ecs::SkillIndex(ecs::SkillKind::Melee)].level == 1);
    CHECK(progression.skills[ecs::SkillIndex(ecs::SkillKind::Melee)].xp ==
          doctest::Approx(70.0f));

    const game::ProgressionAward capped =
        game::AwardXp(progression, ecs::SkillKind::Melee, 1000000.0f, config);
    CHECK(capped.characterLevels > 0);
    CHECK(progression.level == config.maxLevel);
    CHECK(progression.perkPoints >= 1);
    CHECK(progression.xp >= 0.0f);
    CHECK(progression.skills[ecs::SkillIndex(ecs::SkillKind::Melee)].level <=
          config.maxSkillLevel);

    ecs::Progression disabled;
    disabled.perks.push_back("veteran");
    disabled.perks.push_back("learner");
    const game::ProgressionAward boosted =
        game::AwardXp(disabled, ecs::SkillKind::Crafting, 10.0f, config);
    CHECK(boosted.characterXp == doctest::Approx(11.0f));
    CHECK(boosted.skillXp == doctest::Approx(11.5f));

    game::ProgressionConfig off = config;
    off.enabled = false;
    ecs::Progression untouched;
    const game::ProgressionAward none =
        game::AwardXp(untouched, ecs::SkillKind::Melee, 100.0f, off);
    CHECK_FALSE(none.LeveledUp());
    CHECK(untouched.xp == 0.0f);
}

TEST_CASE("M19-F1 Perk 三选一、去重、扣点与效果生效") {
    const game::ProgressionConfig config = TestConfig();
    ecs::Progression progression;
    progression.perkPoints = 1;

    const std::array<std::string, 3> choices =
        game::PerkChoiceIds(progression, config);
    REQUIRE(game::PerkChoiceCount(choices) == 3);
    CHECK(choices[0] != choices[1]);
    CHECK(choices[1] != choices[2]);
    CHECK(choices[0] != choices[2]);
    for (const std::string& id : choices) {
        CHECK(game::FindPerk(config, id) != nullptr);
        CHECK_FALSE(ecs::HasPerk(progression, id));
    }

    const std::string chosen = choices[0];
    CHECK(game::ChoosePerkByIndex(progression, 0, config));
    CHECK(progression.perkPoints == 0);
    CHECK(ecs::HasPerk(progression, chosen));
    CHECK_FALSE(game::ChoosePerkByIndex(progression, 0, config));
    CHECK_FALSE(game::ChoosePerkById(progression, chosen, config));

    progression.perkPoints = 1;
    const std::array<std::string, 3> next = game::PerkChoiceIds(progression, config);
    for (const std::string& id : next) {
        CHECK(id != chosen);
    }

    ecs::Progression effects;
    effects.skills[ecs::SkillIndex(ecs::SkillKind::Melee)] =
        ecs::SkillProgress{4, 0.0f};
    effects.skills[ecs::SkillIndex(ecs::SkillKind::Building)] =
        ecs::SkillProgress{2, 0.0f};
    effects.skills[ecs::SkillIndex(ecs::SkillKind::Crafting)] =
        ecs::SkillProgress{2, 0.0f};
    effects.skills[ecs::SkillIndex(ecs::SkillKind::Survival)] =
        ecs::SkillProgress{3, 0.0f};
    effects.perks = {"bruiser", "brawler", "fleet", "pack_mule", "quick_hands",
                     "builder", "survivor"};
    game::RefreshDerived(effects, config);

    CHECK(game::MeleeDamageMultiplier(effects, config) > 1.0f);
    CHECK(game::MeleeCooldownMultiplier(effects, config) < 1.0f);
    CHECK(game::MoveSpeedMultiplier(effects, config) > 1.0f);
    CHECK(game::CarryCapacityBonusKg(effects, config) > 0.0f);
    CHECK(game::CraftDurationScale(effects, config) < 1.0f);
    CHECK(game::RepairAmountMultiplier(effects, config) > 1.0f);
    CHECK(effects.hungerDecayScale < 1.0f);
    CHECK(effects.thirstDecayScale < 1.0f);
    CHECK(effects.fatigueDecayScale < 1.0f);
    CHECK(game::LootXpMultiplier(effects, config) > 1.0f);
}

TEST_CASE("M19-F1 战斗命中记录只给实际命中发经验") {
    const game::ProgressionConfig config = TestConfig();
    ecs::EcsWorld world;
    entt::registry& registry = world.Registry();

    const ecs::Entity player = world.Create();
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Position>(player, ecs::Position{0.0f, 0.0f, 0});
    registry.emplace<ecs::Health>(player, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::Progression>(player);
    ecs::AttackIntent attack;
    attack.dirX = 1.0f;
    attack.dirY = 0.0f;
    attack.range = 2.0f;
    attack.damage = 50.0f;
    attack.pending = true;
    registry.emplace<ecs::AttackIntent>(player, attack);

    const ecs::Entity zombie = world.Create();
    registry.emplace<ecs::Position>(zombie, ecs::Position{1.0f, 0.0f, 0});
    registry.emplace<ecs::Health>(zombie, ecs::Health{10.0f, 10.0f});

    characters::CombatSystem combat;
    combat.Update(world, 0.0f);
    REQUIRE(combat.GetHits().size() == 1u);
    CHECK(combat.GetHits()[0].attacker == player);
    CHECK(combat.GetHits()[0].target == zombie);
    CHECK(combat.GetHits()[0].lethal);

    std::vector<ecs::Entity> changed;
    const int awarded =
        game::AwardCombatHits(world, combat, config, &changed);
    CHECK(awarded == 1);
    REQUIRE(changed.size() == 1u);
    CHECK(changed[0] == player);
    const ecs::Progression& progression =
        registry.get<ecs::Progression>(player);
    CHECK(progression.xp ==
          doctest::Approx(config.meleeXpPerHit + config.meleeXpPerKill));

    // 下一次没有 AttackIntent 时不会凭空发经验；空挥不产生命中记录。
    const float xpAfter = progression.xp;
    combat.Update(world, 0.0f);
    CHECK(combat.GetHits().empty());
    CHECK(game::AwardCombatHits(world, combat, config, nullptr) == 0);
    CHECK(progression.xp == doctest::Approx(xpAfter));
}
