#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "characters/WeaponSocket.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/AnimationSystem.h"
#include "game/AnimationManifest.h"
#include "game/AssetProvider.h"
#include "game/RenderCollectSystem.h"

using namespace zl;

namespace {

nlohmann::json LoadManifest() {
    const std::string path = std::string(ZL_SOURCE_DIR) + "/assets/asset_manifest.json";
    std::ifstream file(path);
    REQUIRE_MESSAGE(file.is_open(), path);
    nlohmann::json doc;
    file >> doc;
    return doc;
}

}  // namespace

TEST_CASE("M14-T2 manifest：idle/walk/attack/hit/death 契约完整且帧序正确") {
    const nlohmann::json doc = LoadManifest();
    for (const char* logical : {"character.placeholder", "character.zombie"}) {
        const nlohmann::json& binding = doc.at("logical").at(logical).at("own");
        game::AnimationManifest manifest;
        std::string error;
        REQUIRE_MESSAGE(game::ParseAnimationManifest(binding, &manifest, &error), error);
        CHECK_FALSE(manifest.Empty());
        for (std::uint8_t i = 1; i < static_cast<std::uint8_t>(ecs::AnimClip::Count);
             ++i) {
            const auto clip = static_cast<ecs::AnimClip>(i);
            const game::AnimationManifestClip* parsed = manifest.Find(clip);
            REQUIRE(parsed != nullptr);
            const auto& spec = characters::AnimationSystem::Spec(clip);
            CHECK(parsed->framesPerDir == spec.framesPerDir);
            CHECK(parsed->fps == doctest::Approx(spec.fps));
            CHECK(parsed->loop == spec.loop);
            CHECK(parsed->eventMask == spec.eventMask);
            CHECK(parsed->event == spec.event);
            CHECK(parsed->frames.size() ==
                  static_cast<std::size_t>(spec.framesPerDir) * 4u);
        }
        const game::AnimationManifestClip* walk = manifest.Find(ecs::AnimClip::Walk);
        REQUIRE(walk != nullptr);
        CHECK(walk->frames.front().find("_walk_") == std::string::npos);
        CHECK(walk->frames.front().find("_down_0") != std::string::npos);
    }
}

TEST_CASE("M14-T2 manifest：帧数/元数据不一致必须拒绝") {
    nlohmann::json doc = LoadManifest();
    nlohmann::json& binding = doc.at("logical").at("character.placeholder").at("own");

    nlohmann::json bad = binding;
    bad["animations"]["walk"]["fps"] = 9.0;
    game::AnimationManifest manifest;
    std::string error;
    CHECK_FALSE(game::ParseAnimationManifest(bad, &manifest, &error));
    CHECK_FALSE(error.empty());

    bad = binding;
    bad["animations"]["attack"]["frames"].erase(
        bad["animations"]["attack"]["frames"].begin());
    CHECK_FALSE(game::ParseAnimationManifest(bad, &manifest, &error));

    bad = binding;
    bad["animations"].erase("death");
    CHECK_FALSE(game::ParseAnimationManifest(bad, &manifest, &error));
}

TEST_CASE("M14-T3 AnimationSystem：事件帧只触发一次，循环片段回卷后可再次触发") {
    ecs::AnimState walk;
    walk.clip = ecs::AnimClip::Walk;
    walk.framesPerDir = 2;
    walk.fps = 5.0f;
    const auto& walkSpec = characters::AnimationSystem::Spec(ecs::AnimClip::Walk);
    characters::AnimationSystem::Advance(walk, walkSpec, 0.1f);
    CHECK(walk.frame == 0);
    CHECK(walk.eventSerial == 0u);
    characters::AnimationSystem::Advance(walk, walkSpec, 0.1f);
    CHECK(walk.frame == 1);
    CHECK(walk.lastEvent == ecs::AnimEvent::Footstep);
    CHECK(walk.eventSerial == 1u);
    characters::AnimationSystem::Advance(walk, walkSpec, 0.1f);
    CHECK(walk.eventSerial == 1u);  // 同一轮不重复
    characters::AnimationSystem::Advance(walk, walkSpec, 0.2f);  // 回卷到 frame 0
    CHECK(walk.frame == 0);
    characters::AnimationSystem::Advance(walk, walkSpec, 0.2f);
    CHECK(walk.frame == 1);
    CHECK(walk.eventSerial == 2u);  // 第二轮再次触发

    ecs::AnimState attack;
    attack.clip = ecs::AnimClip::Attack;
    attack.framesPerDir = 3;
    attack.fps = 10.0f;
    const auto& attackSpec = characters::AnimationSystem::Spec(ecs::AnimClip::Attack);
    characters::AnimationSystem::Advance(attack, attackSpec, 0.1f);
    CHECK(attack.frame == 1);
    CHECK(attack.lastEvent == ecs::AnimEvent::Hit);
    CHECK(attack.eventSerial == 1u);
    characters::AnimationSystem::Advance(attack, attackSpec, 0.1f);
    CHECK(attack.frame == 2);
    CHECK(attack.eventSerial == 1u);
    characters::AnimationSystem::Advance(attack, attackSpec, 0.2f);
    CHECK(attack.frame == 2);  // 一次性片段停在末帧
}

TEST_CASE("M14-T3 v2 选帧：dir*N+frame 并夹取越界帧") {
    game::AssetProvider::AnimationClip clip;
    clip.framesPerDir = 3;
    clip.frames.resize(12);
    ecs::AnimState anim;
    anim.frame = 2;
    ecs::Facing facing;
    facing.dir = ecs::Direction::Up;
    CHECK(game::RenderCollectSystem::PickClipFrameIndex(&clip, &anim, &facing) == 11u);

    anim.frame = 7;
    CHECK(game::RenderCollectSystem::PickClipFrameIndex(&clip, &anim, &facing) == 11u);

    // 旧 8 帧回退：v2 attack 的第 2 帧夹到旧 walk 的第 1 帧。
    anim.clip = ecs::AnimClip::Attack;
    anim.frame = 2;
    CHECK(game::RenderCollectSystem::PickLegacyFrameIndex(&anim, &facing, 0, 8) == 7u);
    CHECK(game::RenderCollectSystem::PickLegacyFrameIndex(nullptr, nullptr, 12345u, 8) ==
          render::HashPick(std::uint64_t{12345}, 0x5EC7u) % 8u);
}

TEST_CASE("M14-T3 武器挂点：攻击命中帧向前送，死亡收在身侧") {
    const auto idle = characters::PrimaryHandSocket(ecs::Direction::Right,
                                                    ecs::ActorAction::Idle, 0);
    const auto windup = characters::PrimaryHandSocket(ecs::Direction::Right,
                                                      ecs::ActorAction::Attacking, 0);
    const auto strike = characters::PrimaryHandSocket(ecs::Direction::Right,
                                                      ecs::ActorAction::Attacking, 1);
    CHECK(strike.x > windup.x);
    CHECK(strike.x > idle.x);
    const auto dead = characters::PrimaryHandSocket(ecs::Direction::Right,
                                                    ecs::ActorAction::Dead, 1);
    CHECK(dead.scale < idle.scale);
    CHECK(dead.y > idle.y);
}
