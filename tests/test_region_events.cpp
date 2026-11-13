#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "game/RegionEventDirector.h"
#include "save/WorldManifest.h"
#include "world/WorldDefinition.h"

using namespace zl;

namespace {

world::WorldDefinition MakeDefinition() {
    world::WorldDefinition definition;
    definition.format = world::kWorldFormat;
    definition.mapId = "region_event_test";
    definition.seed = 20261110;
    definition.generatorVersion = 1;
    definition.bounds = world::WorldBounds{0, 0, 4, 4};
    definition.regions.push_back(world::WorldRegion{
        "core", world::WorldRect{0, 0, 2, 2}, "village", {"clinic"}});
    definition.regions.push_back(world::WorldRegion{
        "wilds", world::WorldRect{20, 20, 2, 2}, "forest", {}});
    definition.pois.push_back(world::WorldPoi{
        "clinic", "clinic_v1", 15.5, 15.5, 4.0, 1, "test"});
    return definition;
}

game::RegionEventDirector MakeDirector() {
    game::RegionEventConfig config;
    config.durationSeconds = 10.0f;
    config.contentRadius = 8.0f;
    return game::RegionEventDirector(MakeDefinition(), config);
}

const game::RegionEvent* FindEvent(
    const game::RegionEventDirector& director, std::string_view regionId) {
    for (const game::RegionEvent& event : director.Events()) {
        if (event.regionId == regionId) {
            return &event;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("M31-T6 区域事件：确定性站点、近距生成与冻结恢复") {
    game::RegionEventDirector director = MakeDirector();
    director.AdvanceDay(3);
    REQUIRE(director.Events().size() == 2u);
    CHECK(director.Day() == 3);
    const game::RegionEvent* core = FindEvent(director, "core");
    const game::RegionEvent* wilds = FindEvent(director, "wilds");
    REQUIRE(core != nullptr);
    REQUIRE(wilds != nullptr);
    CHECK(core->x == doctest::Approx(15.5f));
    CHECK(core->y == doctest::Approx(15.5f));
    CHECK(core->status == game::RegionEventStatus::Scheduled);
    CHECK(core->targetCount > 0);
    CHECK(core->id != wilds->id);

    game::RegionEventDirector repeat = MakeDirector();
    repeat.AdvanceDay(3);
    REQUIRE(repeat.Events().size() == 2u);
    CHECK(repeat.Events()[0].kind == director.Events()[0].kind);
    CHECK(repeat.Events()[1].kind == director.Events()[1].kind);

    game::RegionEventDirector nextDay = MakeDirector();
    nextDay.AdvanceDay(4);
    CHECK(nextDay.Events()[0].id != director.Events()[0].id);

    auto phase = [](std::string_view regionId) {
        return regionId == "core" ? world::RegionPhase::Active
                                  : world::RegionPhase::Frozen;
    };
    std::vector<std::string> spawned;
    const auto spawn = [&spawned](const game::RegionEvent& event) {
        spawned.push_back(event.regionId);
        return 2;
    };

    director.Update(0.0f, 0.0f, 0.0f, phase, spawn);
    CHECK(spawned.empty());
    CHECK_FALSE(FindEvent(director, "core")->contentSpawned);
    CHECK(FindEvent(director, "core")->status ==
          game::RegionEventStatus::Scheduled);

    director.Update(0.0f, 16.0f, 16.0f, phase, spawn);
    REQUIRE(spawned.size() == 1u);
    CHECK(spawned[0] == "core");
    CHECK(FindEvent(director, "core")->status ==
          game::RegionEventStatus::Active);
    CHECK(FindEvent(director, "core")->contentSpawned);
    CHECK(FindEvent(director, "core")->spawnedCount == 2);
    REQUIRE(director.NearestVisible(16.0f, 16.0f) != nullptr);
    CHECK(director.NearestVisible(16.0f, 16.0f)->regionId == "core");

    director.Update(3.0f, 16.0f, 16.0f, phase, spawn);
    CHECK(FindEvent(director, "core")->remainingSeconds ==
          doctest::Approx(7.0f));
    CHECK(spawned.size() == 1u);

    const auto ambient = [](std::string_view) {
        return world::RegionPhase::Ambient;
    };
    director.Update(100.0f, 16.0f, 16.0f, ambient, spawn);
    CHECK(FindEvent(director, "core")->status ==
          game::RegionEventStatus::Frozen);
    CHECK(FindEvent(director, "core")->remainingSeconds ==
          doctest::Approx(7.0f));
    CHECK(director.NearestVisible(16.0f, 16.0f) != nullptr);

    director.Update(0.0f, 16.0f, 16.0f, phase, spawn);
    CHECK(FindEvent(director, "core")->status ==
          game::RegionEventStatus::Active);
    director.Update(7.0f, 16.0f, 16.0f, phase, spawn);
    CHECK(FindEvent(director, "core")->status ==
          game::RegionEventStatus::Resolved);
    CHECK(spawned.size() == 1u);
}

TEST_CASE("M31-T6 区域事件：重载不重复生成，未知区域拒绝恢复") {
    game::RegionEventDirector director = MakeDirector();
    director.AdvanceDay(5);
    auto phase = [](std::string_view regionId) {
        return regionId == "core" ? world::RegionPhase::Active
                                  : world::RegionPhase::Frozen;
    };
    std::vector<std::string> spawned;
    const auto spawn = [&spawned](const game::RegionEvent& event) {
        spawned.push_back(event.regionId);
        return 1;
    };
    director.Update(0.0f, 16.0f, 16.0f, phase, spawn);
    REQUIRE(spawned.size() == 1u);

    game::RegionEventDirector restored = MakeDirector();
    REQUIRE(restored.Restore(director.Snapshot(), 5));
    restored.Update(0.0f, 16.0f, 16.0f, phase, spawn);
    CHECK(spawned.size() == 1u);
    CHECK(FindEvent(restored, "core")->contentSpawned);
    CHECK(FindEvent(restored, "core")->spawnedCount == 1);

    std::vector<game::RegionEvent> invalid = director.Snapshot();
    invalid[0].regionId = "missing_region";
    CHECK_FALSE(restored.Restore(invalid, 5));
}

TEST_CASE("M31-T6 world_manifest 区域事件往返与校验") {
    save::WorldManifest manifest;
    manifest.identity.mapId = "region_event_test";
    manifest.identity.seed = 7;
    manifest.identity.generatorVersion = 1;
    manifest.identity.mapHash = 0x1234u;
    manifest.events.push_back(save::ManifestRegionEvent{
        "event_a", "core", 0, 1, 5, 15.5f, 15.5f, 7.0f, 8, 8, true});
    manifest.events.push_back(save::ManifestRegionEvent{
        "event_b", "wilds", 2, 2, 5, 25.5f, 25.5f, 4.0f, 2, 0, false});

    std::string error;
    const std::vector<std::uint8_t> bytes =
        save::EncodeWorldManifest(manifest, &error);
    REQUIRE_FALSE(bytes.empty());
    CHECK(error.empty());

    save::WorldManifest decoded;
    REQUIRE(save::DecodeWorldManifest(bytes, &decoded, &error));
    CHECK(error.empty());
    REQUIRE(decoded.events.size() == 2u);
    CHECK(decoded.events[0].id == "event_a");
    CHECK(decoded.events[0].regionId == "core");
    CHECK(decoded.events[0].kind == 0);
    CHECK(decoded.events[0].status == 1);
    CHECK(decoded.events[0].x == doctest::Approx(15.5f));
    CHECK(decoded.events[0].remainingSeconds == doctest::Approx(7.0f));
    CHECK(decoded.events[0].contentSpawned);
    CHECK(decoded.events[1].regionId == "wilds");

    save::WorldManifest oldManifest;
    oldManifest.version = 2;
    oldManifest.identity.mapId = "legacy_region_test";
    oldManifest.identity.seed = 8;
    oldManifest.identity.generatorVersion = 1;
    oldManifest.identity.mapHash = 0x5678u;
    const std::vector<std::uint8_t> v2Empty =
        save::EncodeWorldManifest(oldManifest, &error);
    REQUIRE(v2Empty.size() > 20u);
    save::WorldManifest decodedV2;
    REQUIRE(save::DecodeWorldManifest(v2Empty, &decodedV2, &error));
    CHECK(decodedV2.version == 2);
    CHECK(decodedV2.events.empty());
    CHECK(decodedV2.strategicRegions.empty());
    std::vector<std::uint8_t> v1 = v2Empty;
    v1.resize(v1.size() - 4u);
    v1[4] = 1;
    v1[5] = 0;
    const std::uint64_t payloadSize = v1.size() - 16u;
    for (int i = 0; i < 8; ++i) {
        v1[8u + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((payloadSize >> (i * 8)) & 0xFFu);
    }
    save::WorldManifest decodedV1;
    REQUIRE(save::DecodeWorldManifest(v1, &decodedV1, &error));
    CHECK(decodedV1.version == 1);
    CHECK(decodedV1.events.empty());
    CHECK(decodedV1.identity.mapId == "legacy_region_test");

    save::WorldManifest duplicate = manifest;
    duplicate.events.push_back(duplicate.events[0]);
    CHECK_FALSE(save::ValidateWorldManifest(duplicate, &error));

    save::WorldManifest badStatus = manifest;
    badStatus.events[0].status = 9;
    CHECK_FALSE(save::ValidateWorldManifest(badStatus, &error));

    save::WorldManifest badPosition = manifest;
    badPosition.events[0].x = std::numeric_limits<float>::infinity();
    CHECK_FALSE(save::ValidateWorldManifest(badPosition, &error));

    CHECK(save::kWorldManifestVersion == 6u);
    CHECK(save::kWorldManifestMinVersion == 1u);
}
