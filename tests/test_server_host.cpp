#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "characters/components/CharacterComponents.h"
#include "ecs/components/CommonComponents.h"
#include "game/WorldSimulation.h"
#include "server/ServerHost.h"
#include "world/ChunkManager.h"

namespace {

class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::filesystem::path& path)
        : m_old(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }
    ~ScopedWorkingDirectory() {
        std::error_code error;
        std::filesystem::current_path(m_old, error);
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

private:
    std::filesystem::path m_old;
};

}  // namespace

TEST_CASE("M13 ServerHost：无窗口初始化并推进权威模拟") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = true;
    options.playerTargetX = 70.0f;
    options.playerTargetY = 24.0f;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    CHECK(host.Running());
    CHECK(host.TickCount() == 0);
    CHECK(host.Simulation().Ecs().Systems().Count() > 10);

    for (int i = 0; i < 1800 && host.Simulation().Player() == entt::null; ++i) {
        host.RunForTicks(1);
    }
    const zl::ecs::Entity player = host.Simulation().Player();
    REQUIRE(static_cast<bool>(player != entt::null));
    CHECK(host.Simulation().Ecs().Alive(player));
    CHECK(host.Simulation().Chunks().GetStats().loadedChunks > 0);

    auto& registry = host.Simulation().Ecs().Registry();
    REQUIRE(registry.all_of<zl::ecs::NetworkOwner>(player));
    const std::uint64_t playerStableId = host.Simulation().Ecs().IdOf(player).id;
    CHECK(registry.get<zl::ecs::NetworkOwner>(player).stableId == playerStableId);
    auto& input = registry.get<zl::ecs::MoveInput>(player);
    input.x = 1.0f;
    input.y = 0.0f;
    host.RunForTicks(4);
    const auto& velocity = registry.get<zl::ecs::Velocity>(player);
    CHECK((velocity.x != 0.0f || velocity.y != 0.0f));

    std::string loadError;
    CHECK_FALSE(host.Simulation().LoadSave("missing-save-for-test", &loadError));

    const std::uint64_t ticks = host.TickCount();
    CHECK(ticks > 0);
    host.Shutdown();
    CHECK_FALSE(host.Running());

    zl::server::ServerHost invalid;
    CHECK_FALSE(invalid.Init(
        [] {
            zl::server::ServerHost::Options bad;
            bad.configPath = "config/no_such_server_config.json";
            return bad;
        }(),
        &loadError));
}

TEST_CASE("M13 ServerHost：演示人口由同一模拟生成") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = true;
    options.spawnLocalPlayer = false;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    CHECK(host.Simulation().Ecs().AliveCount() >= 16);
    host.RunForTicks(3);
    CHECK(host.TickCount() == 3);
    host.Shutdown();
}