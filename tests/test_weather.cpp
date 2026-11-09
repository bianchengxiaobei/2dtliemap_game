#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "ai/components/AiComponents.h"
#include "ai/systems/PathFollowSystem.h"
#include "ai/systems/PerceptionSystem.h"
#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/Climate.h"
#include "game/Weather.h"
#include "game/WeatherStatus.h"

using namespace zl;

TEST_CASE("M20-F2 天气：游戏时间确定性采样、首块晴天与夜间降温") {
    game::WeatherParams params;
    params.seed = 20261024u;
    const game::WeatherSystem weather(params);
    const game::WeatherState first = weather.Sample(10.0, 12);
    const game::WeatherState same = weather.Sample(10.0, 12);
    CHECK(first.kind == game::WeatherKind::Clear);
    CHECK(same.kind == first.kind);
    CHECK(same.ambientC == doctest::Approx(first.ambientC));
    CHECK(same.sightScale == doctest::Approx(first.sightScale));

    const game::WeatherState later = weather.Sample(5000.0, 12);
    const bool validKind = later.kind == game::WeatherKind::Clear ||
                           later.kind == game::WeatherKind::Rain ||
                           later.kind == game::WeatherKind::Fog ||
                           later.kind == game::WeatherKind::ColdSnap;
    CHECK(validKind);
    CHECK(later.sightScale >= 0.25f);
    CHECK(later.sightScale <= 1.5f);
    CHECK(later.moveScale >= 0.25f);

    const game::WeatherState day = weather.Sample(5000.0, 12);
    const game::WeatherState night = weather.Sample(5000.0, 23);
    CHECK(night.night);
    CHECK(night.ambientC == doctest::Approx(day.ambientC -
                                            params.nightTemperatureDropC));
}

TEST_CASE("M20-F2 天气：块边界插值不硬切") {
    game::WeatherParams params;
    params.seed = 7u;
    params.periodMinutes = 60.0f;
    params.transitionMinutes = 30.0f;
    params.clearWeight = 1.0f;
    params.rainWeight = 1.0f;
    params.fogWeight = 1.0f;
    params.coldSnapWeight = 1.0f;
    const game::WeatherSystem weather(params);
    const game::WeatherState before = weather.Sample(59.9, 12);
    const game::WeatherState at = weather.Sample(60.0, 12);
    const game::WeatherState after = weather.Sample(60.1, 12);
    CHECK(at.ambientC == doctest::Approx(before.ambientC).epsilon(0.35));
    CHECK(after.ambientC == doctest::Approx(at.ambientC).epsilon(0.35));
    CHECK(at.sightScale == doctest::Approx(before.sightScale).epsilon(0.05));
}

TEST_CASE("M20-F2 温湿：雨天潮湿、室内避雨、防水与篝火保温") {
    game::ClimateParams params;
    ecs::Climate outside;
    game::ClimateEnvironment rain;
    rain.ambientC = 10.0f;
    rain.rainIntensity = 1.0f;
    const game::ClimateTick rainTick =
        game::UpdateClimate(outside, rain, params, 120.0f);
    CHECK(outside.wetness > 0.5f);
    CHECK(rainTick.fatigueDelta > 0.0f);

    ecs::Climate indoors;
    game::ClimateEnvironment indoorEnv = rain;
    indoorEnv.indoor = true;
    game::UpdateClimate(indoors, indoorEnv, params, 120.0f);
    CHECK(indoors.wetness <= 0.001f);
    CHECK(indoors.bodyC > outside.bodyC);

    ecs::Climate raincoat = outside;
    raincoat.wetness = 0.0f;
    game::ClimateEnvironment gear = rain;
    gear.waterproof = 0.9f;
    game::UpdateClimate(raincoat, gear, params, 120.0f);
    CHECK(raincoat.wetness < outside.wetness);

    ecs::Climate cold;
    cold.bodyC = 34.0f;
    game::ClimateEnvironment coldEnv;
    coldEnv.ambientC = -12.0f;
    const game::ClimateTick coldTick =
        game::UpdateClimate(cold, coldEnv, params, 10.0f);
    CHECK(coldTick.damaging);
    CHECK(coldTick.healthDamage > 0.0f);
    CHECK(cold.warning == 2);
    CHECK(cold.moveScale < 1.0f);

    ecs::Climate coldFire;
    coldFire.bodyC = 34.0f;
    game::ClimateEnvironment fireEnv = coldEnv;
    fireEnv.nearFire = true;
    game::UpdateClimate(coldFire, fireEnv, params, 120.0f);
    CHECK(coldFire.bodyC > cold.bodyC);
}

TEST_CASE("M20-F2 HUD：天气/体温/潮湿文案与色调") {
    game::WeatherState weather;
    weather.kind = game::WeatherKind::Rain;
    weather.ambientC = 13.0f;
    ecs::Climate climate;
    climate.bodyC = 35.8f;
    climate.wetness = 0.62f;
    climate.indoor = true;
    std::string tone;
    std::string overlay;
    const std::string text =
        game::WeatherStatusText(weather, climate, &tone, &overlay);
    CHECK(text.find("天气：雨 13°C") != std::string::npos);
    CHECK(text.find("体温 35.8°C") != std::string::npos);
    CHECK(text.find("潮湿 62%") != std::string::npos);
    CHECK(text.find("室内") != std::string::npos);
    CHECK(tone == "warn");
    CHECK(overlay == "rain");

    climate.bodyC = 34.0f;
    game::WeatherStatusText(weather, climate, &tone, &overlay);
    CHECK(tone == "fail");
}

TEST_CASE("M24-T4 寒潮事件临时覆盖天气且到期恢复") {
    game::WeatherParams params;
    params.clearWeight = 1.0f;
    params.rainWeight = 0.0f;
    params.fogWeight = 0.0f;
    params.coldSnapWeight = 0.0f;
    params.coldSnapTemperatureC = -7.0f;
    game::WeatherSystem weather(params);
    CHECK(weather.Sample(120.0, 12).kind == game::WeatherKind::Clear);
    weather.SetOverride(game::WeatherKind::ColdSnap, 135.0);
    const game::WeatherState forced = weather.Sample(120.0, 12);
    CHECK(forced.kind == game::WeatherKind::ColdSnap);
    CHECK(forced.ambientC == doctest::Approx(-7.0f));
    CHECK(weather.Sample(134.9, 12).kind == game::WeatherKind::ColdSnap);
    CHECK(weather.Sample(135.0, 12).kind == game::WeatherKind::Clear);
    weather.SetOverride(game::WeatherKind::ColdSnap, 200.0);
    weather.ClearOverride();
    CHECK(weather.Sample(150.0, 12).kind == game::WeatherKind::Clear);
}

TEST_CASE("M20-F2 感知：天气视距缩放在视线判定前生效") {
    ecs::EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity player = world.Create();
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Position>(player, ecs::Position{5.0f, 5.0f, 0});
    registry.emplace<ecs::Health>(player, ecs::Health{100.0f, 100.0f});

    const ecs::Entity zombie = world.Create();
    registry.emplace<ecs::Position>(zombie, ecs::Position{0.0f, 5.0f, 0});
    registry.emplace<ecs::Facing>(zombie, ecs::Facing{ecs::Direction::Right});
    registry.emplace<ecs::AiState>(zombie, ecs::AiState{});
    registry.emplace<ecs::PerceptionState>(zombie, ecs::PerceptionState{});

    ai::PerceptionParams params;
    params.sightRadius = 12.0f;
    params.closeRadius = 2.5f;
    params.fovDegrees = 160.0f;
    ai::PerceptionSystem perception([](int, int) { return true; }, params);
    perception.Update(world, 0.016f);
    CHECK(registry.get<ecs::AiState>(zombie).targetId == world.IdOf(player).id);

    registry.get<ecs::AiState>(zombie).targetId = 0;
    perception.SetEnvironmentSightScale(0.25f);
    perception.Update(world, 0.016f);
    CHECK(registry.get<ecs::AiState>(zombie).targetId == 0);
}

TEST_CASE("M20-F2 尸速：天气缩放只作用于非本地控制实体") {
    ecs::EcsWorld world;
    entt::registry& registry = world.Registry();
    const ecs::Entity zombie = world.Create();
    registry.emplace<ecs::Position>(zombie, ecs::Position{0.5f, 0.5f, 0});
    registry.emplace<ecs::MoveIntent>(zombie, ecs::MoveIntent{});
    ecs::PathFollow follow;
    follow.status = ecs::PathStatus::Following;
    follow.speed = 4.0f;
    follow.waypoints.push_back(ecs::Waypoint{10.5f, 0.5f});
    registry.emplace<ecs::PathFollow>(zombie, follow);

    ai::PathFollowSystem paths(nullptr);
    paths.SetEnvironmentMoveScale(0.5f);
    paths.Update(world, 0.016f);
    CHECK(registry.get<ecs::MoveIntent>(zombie).x == doctest::Approx(2.0f));

    registry.emplace<ecs::LocalControl>(zombie, ecs::LocalControl{});
    registry.get<ecs::MoveIntent>(zombie) = ecs::MoveIntent{};
    paths.Update(world, 0.016f);
    CHECK(registry.get<ecs::MoveIntent>(zombie).x == doctest::Approx(4.0f));
}
