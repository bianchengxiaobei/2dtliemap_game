#include <cstdint>
#include <memory>
#include <vector>

#include <doctest/doctest.h>

#include "ai/components/AiComponents.h"
#include "ai/systems/ZombieSpecialSystem.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"

using namespace zl;

namespace {

struct SpecialRig {
    ecs::EcsWorld world;
    ecs::Entity player = entt::null;
    ecs::Entity howler = entt::null;
    ecs::Entity victim = entt::null;
    std::vector<core::SoundEvent> sounds;

    SpecialRig(ai::ZombieSpecialParams params = {}) {
        player = world.Create();
        world.Registry().emplace<ecs::PlayerTag>(player);
        world.Registry().emplace<ecs::Position>(player, 10.5f, 10.5f, 0);
        world.Registry().emplace<ecs::Health>(player);

        howler = AddZombie(15.5f, 10.5f);
        auto& special =
            world.Registry().emplace<ecs::ZombieSpecial>(howler);
        special.kind = ecs::ZombieSpecialKind::Howler;

        victim = AddZombie(21.0f, 10.5f);
        system = std::make_unique<ai::ZombieSpecialSystem>(
            params, [this](const core::SoundEvent& sound) {
                sounds.push_back(sound);
            });
    }

    ecs::Entity AddZombie(float x, float y) {
        const ecs::Entity entity = world.Create();
        world.Registry().emplace<ecs::Position>(entity, x, y, 0);
        world.Registry().emplace<ecs::Health>(entity);
        world.Registry().emplace<ecs::AiState>(entity);
        world.Registry().emplace<ecs::PerceptionState>(entity);
        world.Registry().emplace<ecs::ActionState>(entity);
        return entity;
    }

    void Update(float dt) { system->Update(world, dt); }

    std::unique_ptr<ai::ZombieSpecialSystem> system;
};

}  // namespace

TEST_CASE("M20-F3 howler telegraphs, howls, attracts and respects cooldown") {
    ai::ZombieSpecialParams params;
    params.howlWindupSeconds = 1.0f;
    SpecialRig rig(params);
    auto& howlerSpecial =
        rig.world.Registry().get<ecs::ZombieSpecial>(rig.howler);
    auto& howlerAi = rig.world.Registry().get<ecs::AiState>(rig.howler);
    howlerAi.status = ecs::AiStatus::Chase;
    auto& victimPerception =
        rig.world.Registry().get<ecs::PerceptionState>(rig.victim);
    victimPerception.sinceHeard = 1000.0f;
    auto& victimAi = rig.world.Registry().get<ecs::AiState>(rig.victim);
    victimAi.status = ecs::AiStatus::Idle;

    rig.Update(0.5f);
    CHECK(rig.system->GetStats().windups == 1);
    CHECK(rig.system->GetStats().howls == 0);
    CHECK(howlerSpecial.windup > 0.0f);
    CHECK(rig.world.Registry().get<ecs::ActionState>(rig.howler).action ==
          ecs::ActorAction::Howling);
    CHECK(rig.sounds.empty());

    rig.Update(0.6f);
    CHECK(howlerSpecial.windup == doctest::Approx(0.4f));
    CHECK(rig.sounds.empty());

    rig.Update(0.5f);
    CHECK(rig.system->GetStats().howls == 1);
    REQUIRE(rig.sounds.size() == 1u);
    CHECK(rig.sounds[0].radius == doctest::Approx(24.0f));
    CHECK(rig.sounds[0].type == core::SoundType::ZombieGrowl);
    CHECK(victimPerception.sinceHeard == doctest::Approx(0.0f));
    CHECK(victimPerception.alertness == doctest::Approx(1.0f));
    CHECK(victimPerception.lastKnownX == doctest::Approx(10.5f));
    CHECK(victimAi.status == ecs::AiStatus::Chase);
    CHECK(victimAi.targetId == rig.world.IdOf(rig.player).id);
    CHECK(rig.system->GetStats().attracted == 1);
    CHECK(howlerSpecial.cooldown > 0.0f);

    rig.Update(1.0f);
    CHECK(rig.system->GetStats().howls == 0);
    CHECK(rig.sounds.size() == 1u);
}

TEST_CASE("M20-F3 howler is interruptible and cancels when the player escapes") {
    SpecialRig rig;
    auto& howlerSpecial =
        rig.world.Registry().get<ecs::ZombieSpecial>(rig.howler);
    auto& howlerAi = rig.world.Registry().get<ecs::AiState>(rig.howler);
    howlerAi.status = ecs::AiStatus::Chase;

    rig.Update(0.3f);
    REQUIRE(howlerSpecial.windup > 0.0f);
    auto& hit = rig.world.Registry().emplace<ecs::HitReaction>(rig.howler);
    hit.staggerLeft = 0.2f;
    rig.Update(0.1f);
    CHECK(rig.system->GetStats().interrupted == 1);
    CHECK(howlerSpecial.windup == doctest::Approx(0.0f));
    CHECK(rig.world.Registry().get<ecs::ActionState>(rig.howler).action ==
          ecs::ActorAction::Idle);
    CHECK(rig.sounds.empty());

    rig.world.Registry().remove<ecs::HitReaction>(rig.howler);
    howlerSpecial.cooldown = 0.0f;
    rig.Update(0.1f);
    REQUIRE(howlerSpecial.windup > 0.0f);
    rig.world.Registry().get<ecs::Position>(rig.player).x = 40.0f;
    rig.Update(0.1f);
    CHECK(howlerSpecial.windup == doctest::Approx(0.0f));
    CHECK(rig.sounds.empty());
}

TEST_CASE("M20-F3 breaker windup is decremented and interrupted by stagger") {
    SpecialRig rig;
    auto& special = rig.world.Registry().get<ecs::ZombieSpecial>(rig.howler);
    special.kind = ecs::ZombieSpecialKind::Breaker;
    special.windup = 0.9f;
    special.windupDuration = 0.9f;
    rig.Update(0.3f);
    CHECK(special.windup == doctest::Approx(0.6f));

    auto& hit = rig.world.Registry().emplace<ecs::HitReaction>(rig.howler);
    hit.staggerLeft = 0.2f;
    rig.Update(0.1f);
    CHECK(special.windup == doctest::Approx(0.0f));
    CHECK(rig.system->GetStats().interrupted == 1);
}
