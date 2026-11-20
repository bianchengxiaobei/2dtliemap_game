#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "core/Log.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/WorldSimulation.h"
#include "physics/PhysicsWorld.h"
#include "save/ChunkDiff.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "save/SaveService.h"
#include "save/SaveTypes.h"
#include "server/ServerHost.h"
#include "world/Chunk.h"
#include "world/ChunkManager.h"
#include "world/GridSquare.h"
#include "world/World.h"

using namespace zl;

namespace {

using Clock = std::chrono::steady_clock;

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

class ScopedLogLevel {
public:
    explicit ScopedLogLevel(const char* level) { core::SetLogLevel(level); }
    ~ScopedLogLevel() { core::SetLogLevel("info"); }

    ScopedLogLevel(const ScopedLogLevel&) = delete;
    ScopedLogLevel& operator=(const ScopedLogLevel&) = delete;
};

std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "zomboidlike_t9" / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

double MsSince(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin)
        .count();
}

double Percentile(std::vector<double> values, double percentile) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double index =
        std::ceil(percentile * static_cast<double>(values.size())) - 1.0;
    return values[static_cast<std::size_t>(
        std::clamp(index, 0.0, static_cast<double>(values.size() - 1)))];
}

void TeleportPlayerDirect(server::ServerHost& host, ecs::Entity player,
                          float x, float y) {
    auto& registry = host.Simulation().Ecs().Registry();
    auto* position = registry.try_get<ecs::Position>(player);
    if (position == nullptr) {
        return;
    }
    position->x = x;
    position->y = y;
    if (auto* rigid = registry.try_get<ecs::RigidBody>(player);
        rigid != nullptr && rigid->body != nullptr) {
        host.Simulation().Physics().TeleportBody(rigid->body, x, y,
                                                 position->z);
    }
}

std::uint64_t PackChunk(int chunkX, int chunkY) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX))
            << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkY));
}

struct StressReport {
    std::uint64_t uniqueChunks = 0;
    int maxLoaded = 0;
    std::uint64_t totalLoaded = 0;
    std::uint64_t totalUnloaded = 0;
    std::uint64_t totalFailed = 0;
    double tickP50Ms = 0.0;
    double tickP95Ms = 0.0;
    double tickMaxMs = 0.0;
    double wallMs = 0.0;
    int builds = 0;
    int combats = 0;
    int zombieDamage = 0;
};

}  // namespace

TEST_CASE("M31-T9 1000+ 脏 chunk 后台写盘、文件统计与读档") {
    constexpr int kChunks = 1024;
    const std::filesystem::path root = TempDir("dirty_chunks");
    save::SaveManager::Options options;
    options.root = root;
    options.gameVersion = save::kDefaultGameVersion;
    save::SaveManager manager(options);

    save::SaveManager::CreateOptions create;
    create.name = "M31-T9 dirty";
    create.mapId = "village_demo_m31";
    create.gameVersion = save::kDefaultGameVersion;
    save::EntitySnapshot empty;
    create.entities = &empty;
    std::string saveId;
    std::string error;
    REQUIRE_MESSAGE(manager.Create(create, &saveId, &error), error);

    std::vector<save::ChunkDiff> diffs;
    diffs.reserve(kChunks);
    double maxSnapshotMs = 0.0;
    std::uint64_t snapshotSquares = 0;
    std::string snapshotError;
    for (int i = 0; i < kChunks; ++i) {
        const int chunkX = i % 32;
        const int chunkY = i / 32;
        world::Chunk chunk(chunkX, chunkY);
        chunk.Square(1, 1, 0).SetFloor({"tile.floor.grass", 0, 0});
        chunk.CaptureBaseline();
        chunk.Square(1, 1, 0).SetFloor({"tile.floor.wood", 0, 0});
        chunk.Square(2, 2, 0).SetFloor({"tile.floor.dirt", 0, 0});
        chunk.Square(3, 3, 0).AddObject(world::SquareLayer::Wall,
                                        {"wall.brick", 0, 0, true});
        chunk.Square(4, 4, 0).SetRoomId(7);
        const Clock::time_point snapshotBegin = Clock::now();
        save::ChunkDiff diff;
        if (!save::CaptureChunkDiff(chunk, &diff, &error)) {
            snapshotError = error + " chunk=" + std::to_string(chunkX) + "," +
                            std::to_string(chunkY);
            break;
        }
        maxSnapshotMs = std::max(maxSnapshotMs, MsSince(snapshotBegin));
        snapshotSquares += diff.squares.size();
        diffs.push_back(std::move(diff));
    }
    REQUIRE_MESSAGE(diffs.size() == static_cast<std::size_t>(kChunks),
                    snapshotError);

    save::SaveService service(manager);
    const Clock::time_point queueBegin = Clock::now();
    bool queued = true;
    for (save::ChunkDiff& diff : diffs) {
        if (!service.RequestChunk(saveId, std::move(diff), &error)) {
            queued = false;
            break;
        }
    }
    const double queueMs = MsSince(queueBegin);
    REQUIRE_MESSAGE(queued, error);
    CHECK(service.Busy());
    CHECK(service.PendingChunkWrites() > 0u);
    const Clock::time_point waitBegin = Clock::now();
    service.WaitIdle();
    const double totalWriteMs = MsSince(waitBegin) + queueMs;
    const save::SaveService::ChunkWriteStats writeStats =
        service.GetChunkWriteStats();
    CHECK(writeStats.completed == static_cast<std::uint64_t>(kChunks));
    CHECK(writeStats.failed == 0u);
    CHECK(writeStats.retried == 0u);
    CHECK(writeStats.maxMs < 1000.0);
    CHECK(service.PendingChunkWrites() == 0u);

    const save::SaveManager::StorageStats storage =
        manager.MeasureStorage(saveId, &error);
    REQUIRE_MESSAGE(storage.valid, error);
    CHECK(storage.chunkFiles == static_cast<std::size_t>(kChunks));
    CHECK(storage.files >= static_cast<std::size_t>(kChunks));
    CHECK(storage.totalBytes > storage.chunkBytes);
    CHECK(storage.chunkBytes > 0u);

    const Clock::time_point loadBegin = Clock::now();
    int loadFailures = 0;
    int wrongFloors = 0;
    std::string firstLoadError;
    for (int i = 0; i < kChunks; ++i) {
        const int chunkX = i % 32;
        const int chunkY = i / 32;
        world::Chunk target(chunkX, chunkY);
        target.CaptureBaseline();
        if (manager.LoadChunk(saveId, target, &error) !=
            save::SaveManager::LoadResult::Ok) {
            if (loadFailures == 0) {
                firstLoadError = error + " chunk=" + std::to_string(chunkX) +
                                 "," + std::to_string(chunkY);
            }
            ++loadFailures;
            continue;
        }
        const world::GridSquare& first = target.Square(1, 1, 0);
        const world::GridSquare& second = target.Square(2, 2, 0);
        const world::GridSquare& third = target.Square(3, 3, 0);
        const world::GridSquare& fourth = target.Square(4, 4, 0);
        bool correct = first.Floor() != nullptr &&
                       first.Floor()->sprite == "tile.floor.wood" &&
                       second.Floor() != nullptr &&
                       second.Floor()->sprite == "tile.floor.dirt" &&
                       third.Layer(world::SquareLayer::Wall).size() == 1u &&
                       fourth.RoomId() == 7;
        if (!correct) {
            ++wrongFloors;
        }
    }
    const double loadMs = MsSince(loadBegin);
    CHECK_MESSAGE(loadFailures == 0, firstLoadError);
    CHECK(wrongFloors == 0);

    std::ostringstream message;
    message << std::fixed << std::setprecision(2)
            << "M31-T9 dirty chunks: files=" << storage.files
            << " chunks=" << storage.chunkFiles
            << " bytes=" << storage.totalBytes
            << " chunkBytes=" << storage.chunkBytes
            << " squares=" << snapshotSquares << " snapshotMax="
            << maxSnapshotMs << "ms queue=" << queueMs
            << "ms writeTotal=" << totalWriteMs << "ms writeMax="
            << writeStats.maxMs << "ms batchAvg="
            << (writeStats.totalMs / static_cast<double>(kChunks))
            << "ms load=" << loadMs << "ms";
    MESSAGE(message.str());

#ifdef NDEBUG
    constexpr double kSnapshotBudgetMs = 5.0;
#else
    constexpr double kSnapshotBudgetMs = 20.0;
#endif
    CHECK(maxSnapshotMs < kSnapshotBudgetMs);
    CHECK(queueMs < 100.0);
    CHECK(totalWriteMs < 15000.0);
    CHECK(loadMs < 8000.0);
    CHECK(storage.totalBytes < 4u * 1024u * 1024u);
}

TEST_CASE("M31-T9 36x36 一小时加速 soak：巡游、建造与战斗") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    const std::filesystem::path dir = TempDir("one_hour_soak");
    ScopedWorkingDirectory cwd(root);
    ScopedLogLevel logLevel("warn");

    std::vector<std::string> args{
        "ZomboidLikeTests",
        "--world.mapDir=assets/maps/village_demo_m31",
        "--world.loadRadius=3",
        "--infection.enabled=false",
        "--save.root=" + (dir / "saves").string(),
        "--save.autoSaveOnExit=false"};
    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = true;
    options.playerTargetX = 55.5f;
    options.playerTargetY = 55.5f;
    options.network = false;
    options.enablePlayerProfiles = false;
    options.argc = static_cast<int>(argv.size());
    options.argv = argv.data();

    server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    core::SetLogLevel("warn");
    for (int i = 0; i < 1800 && host.Simulation().Player() == entt::null;
         ++i) {
        host.Tick(1.0f / 60.0f);
    }
    ecs::Entity player = host.Simulation().Player();
    REQUIRE(static_cast<bool>(player != entt::null));
    auto& playerRegistry = host.Simulation().Ecs().Registry();
    std::uint64_t playerId = 0;
    const auto refreshPlayer = [&]() {
        const ecs::Entity current = host.Simulation().Player();
        if (current == entt::null ||
            !host.Simulation().Ecs().Alive(current)) {
            return false;
        }
        player = current;
        if (auto* health = playerRegistry.try_get<ecs::Health>(player);
            health != nullptr) {
            health->max = 1000000.0f;
            health->current = health->max;
        }
        if (auto* infection = playerRegistry.try_get<ecs::Infection>(player);
            infection != nullptr) {
            *infection = {};
        }
        playerId = host.Simulation().Ecs().IdOf(player).id;
        return true;
    };
    REQUIRE(refreshPlayer());

    constexpr int kStationsPerLap = 36 * 36;
    constexpr int kTicksPerStation = 3;
    constexpr int kLaps = 1;
    constexpr int kCombatInterval = 100;
    constexpr int kBuildInterval = 600;
    constexpr int kTotalTicks =
        kStationsPerLap * kTicksPerStation * kLaps;
    constexpr float kTickSeconds = 1.0f;

    std::unordered_set<std::uint64_t> uniqueChunks;
    uniqueChunks.reserve(1296);
    std::vector<double> tickMs;
    tickMs.reserve(kTotalTicks);
    StressReport report;
    const Clock::time_point soakBegin = Clock::now();
    int tickIndex = 0;
    for (int lap = 0; lap < kLaps; ++lap) {
        for (int row = 0; row < 36; ++row) {
            const int start = row % 2 == 0 ? 0 : 35;
            const int step = row % 2 == 0 ? 1 : -1;
            for (int column = 0; column < 36; ++column) {
                const int chunkX = start + column * step;
                const int chunkY = row;
                if (refreshPlayer()) {
                    TeleportPlayerDirect(host, player,
                                         static_cast<float>(chunkX * 10 + 5),
                                         static_cast<float>(chunkY * 10 + 5));
                }
                for (int hold = 0; hold < kTicksPerStation; ++hold) {
                    const Clock::time_point tickBegin = Clock::now();
                    host.Tick(kTickSeconds);
                    tickMs.push_back(MsSince(tickBegin));
                    ++tickIndex;

                    if (tickIndex % kCombatInterval == 0 && refreshPlayer()) {
                        const auto& position =
                            host.Simulation().Ecs().Registry().get<ecs::Position>(
                                player);
                        const ecs::Entity zombie =
                            host.Simulation().SpawnZombieAt(
                                position.x + 1.5f, position.y);
                        if (zombie != entt::null) {
                            ++report.combats;
                            ecs::DamageEvent damage;
                            damage.sourceId = playerId;
                            damage.type = ecs::DamageType::Blunt;
                            damage.amount = 1000.0f;
                            damage.knockback = 1.0f;
                            damage.staggerSeconds = 0.1f;
                            if (host.Simulation().Combat()->ApplyDamage(
                                    host.Simulation().Ecs(), zombie, damage)) {
                                ++report.zombieDamage;
                            }
                        }
                    }
                    if (tickIndex % kBuildInterval == 0 && refreshPlayer()) {
                        const auto& position =
                            host.Simulation().Ecs().Registry().get<ecs::Position>(
                                player);
                        const int wx = static_cast<int>(std::floor(position.x));
                        const int wy = static_cast<int>(std::floor(position.y));
                        world::GridSquare* square =
                            host.Simulation().World().GetSquare(wx, wy, 0);
                        if (square != nullptr) {
                            world::SquareObject wall{"object.wall_h", 0, 0,
                                                     true};
                            wall.hp = 100.0f;
                            wall.playerBuilt = true;
                            square->AddObject(world::SquareLayer::Wall, wall);
                            ++report.builds;
                        }
                    }
                }
                host.Simulation().Chunks().ForEachLoadedChunk(
                    [&](const world::Chunk& chunk) {
                        uniqueChunks.insert(
                            PackChunk(chunk.ChunkX(), chunk.ChunkY()));
                    });
                const auto chunkStats =
                    host.Simulation().Chunks().GetStats();
                report.maxLoaded = std::max(report.maxLoaded,
                                            chunkStats.loadedChunks);
            }
        }
    }
    report.wallMs = MsSince(soakBegin);
    const auto chunkStats = host.Simulation().Chunks().GetStats();
    report.uniqueChunks = uniqueChunks.size();
    report.totalLoaded = chunkStats.totalLoaded;
    report.totalUnloaded = chunkStats.totalUnloaded;
    report.totalFailed = chunkStats.totalFailed;
    report.tickP50Ms = Percentile(tickMs, 0.50);
    report.tickP95Ms = Percentile(tickMs, 0.95);
    report.tickMaxMs = Percentile(tickMs, 1.0);

    constexpr double kSimulatedMinutes =
        static_cast<double>(kTotalTicks) *
        static_cast<double>(kTickSeconds) / 60.0;
    std::ostringstream message;
    message << std::fixed << std::setprecision(2)
            << "M31-T9 soak: ticks=" << kTotalTicks
            << " simulatedMinutes=" << kSimulatedMinutes
            << " uniqueChunks=" << report.uniqueChunks
            << "/1296 maxLoaded=" << report.maxLoaded
            << " loaded=" << report.totalLoaded
            << " unloaded=" << report.totalUnloaded
            << " failed=" << report.totalFailed
            << " builds=" << report.builds
            << " combats=" << report.combats
            << " zombieDamage=" << report.zombieDamage
            << " tickP50=" << report.tickP50Ms
            << "ms tickP95=" << report.tickP95Ms
            << "ms tickMax=" << report.tickMaxMs
            << "ms wall=" << report.wallMs << "ms";
    MESSAGE(message.str());

    CHECK(report.uniqueChunks == 1296u);
    CHECK(report.maxLoaded > 0);
    CHECK(report.maxLoaded <= 98);
    CHECK(report.totalFailed == 0u);
    CHECK(report.builds > 0);
    CHECK(report.combats > 0);
    CHECK(report.zombieDamage > 0);
    CHECK(report.tickP95Ms < 100.0);
    CHECK(report.tickMaxMs < 500.0);
    CHECK(host.Running());

    host.Shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(dir, cleanupError);
}
