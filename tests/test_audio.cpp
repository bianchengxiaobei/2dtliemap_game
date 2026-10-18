#include <cmath>
#include <ostream>
#include <string>

#include <doctest/doctest.h>

#include "audio/AudioEngine.h"
#include "audio/SoundIds.h"
#include "audio/SoundManager.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "core/SoundPropagation.h"
#include "ai/components/AiComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"

using namespace zl;

TEST_CASE("M11-T4：隔墙衰减计数与 AI/音频共享判定") {
    const core::SoundWalkableQuery walkable = [](int x, int y) {
        return !((x == 2 || x == 3) && y == 0);
    };
    CHECK(core::CountBlockingCells(0.5f, 0.5f, 5.5f, 0.5f, walkable) == 2);
    CHECK(core::CountBlockingCells(0.5f, 0.5f, 5.5f, 0.5f, {}) == 0);

    CHECK(core::WallAttenuationGain(0, 0.6f) == doctest::Approx(1.0f));
    CHECK(core::WallAttenuationGain(1, 0.6f) == doctest::Approx(0.6f));
    CHECK(core::WallAttenuationGain(2, 0.6f) == doctest::Approx(0.36f));
    CHECK(core::EffectiveSoundRadius(10.0f, 2, 0.6f) == doctest::Approx(3.6f));
    CHECK(core::IsSoundAudible(10.0f, 3.0f, 2, 0.6f));
    CHECK_FALSE(core::IsSoundAudible(10.0f, 7.0f, 2, 0.6f));
}

TEST_CASE("M11-T3：地面材质 → 脚步音效") {
    CHECK(audio::FootstepClipForFloor("tile.floor.grass") == "footstep_grass");
    CHECK(audio::FootstepClipForFloor("tile.floor.dirt") == "footstep_dirt");
    CHECK(audio::FootstepClipForFloor("tile.floor.wood") == "footstep_wood");
    CHECK(audio::FootstepClipForFloor("unknown") == "footstep_grass");
}

TEST_CASE("M11-T2：距离衰减与耳机左右声像") {
    const core::SoundWalkableQuery empty;
    const audio::SpatialGain right =
        audio::ComputeSpatial({10.0f, 10.0f}, {14.0f, 6.0f}, 2.0f, 20.0f, 0.6f, empty);
    const audio::SpatialGain left =
        audio::ComputeSpatial({10.0f, 10.0f}, {6.0f, 14.0f}, 2.0f, 20.0f, 0.6f, empty);
    CHECK(right.pan > 0.9f);
    CHECK(left.pan < -0.9f);
    CHECK(right.distance == doctest::Approx(std::sqrt(32.0f)));

    const audio::SpatialGain near =
        audio::ComputeSpatial({10.0f, 10.0f}, {11.0f, 10.0f}, 2.0f, 20.0f, 0.6f, empty);
    const audio::SpatialGain far =
        audio::ComputeSpatial({10.0f, 10.0f}, {35.0f, 10.0f}, 2.0f, 20.0f, 0.6f, empty);
    CHECK(near.gain == doctest::Approx(1.0f));
    CHECK(far.gain == doctest::Approx(0.0f));

    const core::SoundWalkableQuery wall = [](int x, int y) {
        return !(x == 12 && y == 10);
    };
    const audio::SpatialGain occluded =
        audio::ComputeSpatial({10.0f, 10.0f}, {14.0f, 10.0f}, 2.0f, 20.0f, 0.6f, wall);
    CHECK(occluded.walls > 0);
    CHECK(occluded.gain < near.gain);
}

TEST_CASE("M11-T1：AudioEngine null device 播放/停止与音量组") {
    audio::AudioEngineOptions options;
    options.root = std::string(ZL_SOURCE_DIR) + "/assets/audio";
    options.device = "null";
    options.masterVolume = 0.65f;
    options.sfxVolume = 0.85f;

    audio::AudioEngine engine;
    REQUIRE(engine.Init(options));
    CHECK(engine.IsReady());
    CHECK_FALSE(engine.IsDeviceAvailable());
    CHECK(engine.GroupVolume(audio::AudioGroup::Master) == doctest::Approx(0.65f));
    engine.SetGroupVolume(audio::AudioGroup::SFX, 0.4f);
    CHECK(engine.GroupVolume(audio::AudioGroup::SFX) == doctest::Approx(0.4f));

    const audio::AudioHandle click =
        engine.Play2D(audio::kUiClick, audio::AudioGroup::SFX, 0.5f);
    CHECK(click.Valid());
    CHECK(engine.GetStats().activeVoices == 1);
    engine.Update(0.016f);
    engine.Stop(click);
    CHECK(engine.GetStats().activeVoices == 0);

    const audio::AudioHandle shot =
        engine.Play3D(audio::kGunshot, {12.0f, 8.0f}, audio::AudioGroup::SFX, 0.8f);
    CHECK(shot.Valid());
    engine.SetListener({10.0f, 10.0f});
    engine.Update(0.016f);
    CHECK(engine.GetStats().played >= 2);
    engine.StopAll();
    CHECK(engine.GetStats().activeVoices == 0);
    engine.Shutdown();
}

TEST_CASE("M11-T1：audio.device=off 时静音且安全") {
    audio::AudioEngineOptions options;
    options.device = "off";
    audio::AudioEngine engine;
    CHECK_FALSE(engine.Init(options));
    CHECK_FALSE(engine.IsReady());
    CHECK_FALSE(engine.Play2D(audio::kUiClick).Valid());
    engine.Update(0.016f);
    engine.StopAll();
    engine.Shutdown();
}

TEST_CASE("M11-T1/T3：SoundManager OnSound/拾取/环境循环") {
    core::EventBus bus;
    audio::SoundManagerOptions options;
    options.engine.root = std::string(ZL_SOURCE_DIR) + "/assets/audio";
    options.engine.device = "null";
    options.engine.ambientVolume = 0.5f;

    audio::SoundManager sound;
    REQUIRE(sound.Init(bus, options));
    CHECK(sound.IsReady());
    CHECK_FALSE(sound.IsDeviceAvailable());

    core::SoundEvent click;
    click.type = core::SoundType::Ui;
    click.clip = audio::kUiClick;
    bus.EmitQueued(core::kOnSound, click);
    bus.FlushQueued();
    CHECK(sound.GetStats().eventsPlayed == 1);

    core::SoundEvent shot;
    shot.type = core::SoundType::Gunshot;
    sound.EmitWorldSound(shot, audio::kGunshot);
    bus.FlushQueued();
    CHECK(sound.GetStats().eventsPlayed == 2);
    CHECK(sound.GetStats().gunshots == 1);

    bus.EmitQueued(core::kOnItemPickup, core::ItemEvent{1, "bandage", 1});
    bus.FlushQueued();
    CHECK(sound.GetStats().pickups == 1);

    sound.Update(0.016f, nullptr, {0.0f, 0.0f}, false, false);
    CHECK(sound.GetStats().musicStarts == 1);
    CHECK(sound.GetStats().ambienceStarts == 1);
    sound.Update(0.016f, nullptr, {0.0f, 0.0f}, false, false);
    CHECK(sound.GetStats().ambienceStarts == 1);
    sound.Update(0.016f, nullptr, {0.0f, 0.0f}, true, false);
    CHECK(sound.GetStats().ambienceStarts == 2);

    sound.PlayUiClick();
    CHECK(sound.GetStats().uiClicks == 1);

    ecs::EcsWorld world;
    const ecs::Entity zombie = world.Create();
    world.Registry().emplace<ecs::Position>(zombie, ecs::Position{5.0f, 0.0f, 0});
    world.Registry().emplace<ecs::AiState>(zombie, ecs::AiState{});
    sound.Update(1.1f, &world, {0.0f, 0.0f}, false, true);
    CHECK(sound.GetStats().growls == 1);
    sound.Shutdown();
}
