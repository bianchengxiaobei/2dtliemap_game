#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/Objectives.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"

using zl::ecs::Objective;
using zl::ecs::ObjectiveStep;

TEST_CASE("M14-T6 objective state and HUD text") {
    CHECK(zl::game::AdvanceObjective(ObjectiveStep::FindRadioPart, true, false) ==
          ObjectiveStep::RepairRadio);
    CHECK(zl::game::AdvanceObjective(ObjectiveStep::RepairRadio, true, false) ==
          ObjectiveStep::RepairRadio);
    CHECK(zl::game::AdvanceObjective(ObjectiveStep::ReachExtraction, false, true) ==
          ObjectiveStep::Complete);
    CHECK(zl::game::AdvanceObjective(ObjectiveStep::Complete, true, true) ==
          ObjectiveStep::Complete);

    const std::string findText =
        zl::game::ObjectiveText(ObjectiveStep::FindRadioPart, false, 66.5f, 28.5f);
    CHECK_FALSE(findText.empty());
    const std::string repairText =
        zl::game::ObjectiveText(ObjectiveStep::RepairRadio, true, 66.5f, 28.5f);
    CHECK(repairText.find("E") != std::string::npos);
    const std::string reachText =
        zl::game::ObjectiveText(ObjectiveStep::ReachExtraction, false, 66.5f, 28.5f);
    CHECK(reachText.find("66") != std::string::npos);
    const std::string lostText =
        zl::game::ObjectiveText(ObjectiveStep::RepairRadio, false, 66.5f, 28.5f);
    CHECK_FALSE(lostText.empty());

    CHECK(std::string(zl::game::ObjectiveTone(ObjectiveStep::FindRadioPart)) == "info");
    CHECK(std::string(zl::game::ObjectiveTone(ObjectiveStep::RepairRadio)) == "warn");
    CHECK(std::string(zl::game::ObjectiveTone(ObjectiveStep::Complete)) == "ok");
}

TEST_CASE("M14-T6 Objective save roundtrip") {
    zl::save::ComponentRegistry components;
    std::string error;
    REQUIRE(zl::save::RegisterBuiltinComponents(components, &error));
    CHECK(error.empty());
    CHECK(components.Size() == 46u);

    zl::ecs::EcsWorld source;
    const zl::ecs::Entity player = source.Create();
    source.Registry().emplace<zl::ecs::Position>(
        player, zl::ecs::Position{72.5f, 21.5f, 0});
    source.Registry().emplace<zl::ecs::Objective>(
        player, Objective{ObjectiveStep::ReachExtraction});

    zl::save::SavedEntity saved;
    REQUIRE(zl::save::CaptureEntity(source, player, components, &saved, &error));
    const std::vector<std::uint8_t> bytes =
        zl::save::EncodeEntitySnapshot(zl::save::EntitySnapshot{{saved}}, &error);
    REQUIRE_FALSE(bytes.empty());

    zl::save::EntitySnapshot decoded;
    REQUIRE(zl::save::DecodeEntitySnapshot(bytes, &decoded, &error));
    zl::ecs::EcsWorld target;
    const zl::ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != zl::ecs::EcsWorld::kNull);
    REQUIRE(zl::save::ApplyEntity(decoded.entities[0], target, loaded, components,
                                  &error));
    const auto* objective = target.Registry().try_get<zl::ecs::Objective>(loaded);
    REQUIRE(objective != nullptr);
    CHECK(objective->step == ObjectiveStep::ReachExtraction);
}
