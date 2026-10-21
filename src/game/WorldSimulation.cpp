#include "game/WorldSimulation.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

#include "ai/Passability.h"
#include "ai/PathfindingService.h"
#include "ai/VirtualHorde.h"
#include "ai/components/AiComponents.h"
#include "ai/systems/AiSystem.h"
#include "ai/systems/BoidsSystem.h"
#include "ai/systems/LodSystem.h"
#include "ai/systems/PathFollowSystem.h"
#include "ai/systems/PerceptionSystem.h"
#include "ai/systems/SpawnLodSystem.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/ActionPresentationSystem.h"
#include "characters/systems/AnimationSystem.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/DeathSystem.h"
#include "characters/systems/InputSystem.h"
#include "characters/systems/MovementSystem.h"
#include "core/Config.h"
#include "core/Constants.h"
#include "core/EventBus.h"
#include "core/Log.h"
#include "core/TaskScheduler.h"
#include "ecs/SpatialIndex.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/WorldQueries.h"
#include "game/ZombieFactory.h"
#include "items/ItemComponents.h"
#include "physics/PhysicsSyncSystem.h"
#include "physics/PhysicsWorld.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "world/Chunk.h"
#include "world/ChunkManager.h"
#include "world/World.h"

namespace zl::game {
namespace {

void SetWarning(std::string* warning, const std::string& text) {
    if (warning != nullptr) {
        *warning = text;
    }
}

}  // namespace

struct WorldSimulation::Impl {
    explicit Impl(Options value) : options(std::move(value)) {}

    Options options;
    bool initialized = false;
    glm::vec2 focus{70.0f, 18.0f};

    std::unique_ptr<world::World> world;
    std::unique_ptr<world::ChunkManager> chunks;
    std::unique_ptr<physics::PhysicsWorld> physics;
    std::unique_ptr<ai::PathfindingService> pathfinding;
    std::unique_ptr<ecs::EcsWorld> ecs;
    ecs::SpatialIndex spatial;

    std::unique_ptr<characters::InputSystem> inputSystem;
    std::unique_ptr<characters::MovementSystem> movementSystem;
    std::unique_ptr<physics::PhysicsSyncSystem> physicsSyncSystem;
    std::unique_ptr<characters::ActionPresentationSystem> actionPresentationSystem;
    std::unique_ptr<characters::AnimationSystem> animationSystem;
    std::unique_ptr<characters::DeathSystem> deathSystem;
    std::unique_ptr<characters::CombatSystem> combatSystem;

    std::unique_ptr<ai::PathFollowSystem> pathFollowSystem;
    std::unique_ptr<ai::AiSystem> aiSystem;
    std::unique_ptr<ai::PerceptionSystem> perceptionSystem;
    std::unique_ptr<ai::LodSystem> lodSystem;
    std::unique_ptr<ai::BoidsSystem> boidsSystem;
    ai::VirtualHordeStore virtualHordes;
    std::unique_ptr<ai::SpawnLodSystem> spawnLodSystem;

    core::EventBus::SubscriptionId soundSubscription =
        core::EventBus::kInvalidSubscription;
    ecs::Entity player = entt::null;
    float playerMaxHp = 100.0f;
    float playerStartHp = 100.0f;
    float playerCapacityKg = 20.0f;

    ecs::Entity SpawnPlayer(float px, float py, std::uint64_t stableId = 0) {
        if (ecs == nullptr) {
            return entt::null;
        }
        entt::registry& registry = ecs->Registry();
        const ecs::Entity entity =
            stableId != 0 ? ecs->CreateWithStableId(stableId) : ecs->Create();
        if (entity == entt::null) {
            return entt::null;
        }
        registry.emplace<ecs::Position>(entity, ecs::Position{px, py, 0});
        registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
        registry.emplace<ecs::Collider>(entity, ecs::Collider{});
        registry.emplace<ecs::SpriteRef>(
            entity, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0, 0xFFFFFFFFu});
        registry.emplace<ecs::Facing>(entity, ecs::Facing{});
        registry.emplace<ecs::AnimState>(entity, ecs::AnimState{});
        registry.emplace<ecs::ActionState>(entity, ecs::ActionState{});

        ecs::Health health;
        health.max = playerMaxHp > 0.0f ? playerMaxHp : 100.0f;
        health.current = std::min(playerStartHp, health.max);
        registry.emplace<ecs::Health>(entity, health);
        registry.emplace<ecs::Needs>(entity, ecs::Needs{});

        if (physics != nullptr) {
            const ecs::Collider collision;
            ecs::RigidBody rigid;
            rigid.body = physics->CreateCharacterBody(collision.radius, collision.height, px,
                                                       py, 0);
            registry.emplace<ecs::RigidBody>(entity, rigid);
        }

        registry.emplace<ecs::PlayerTag>(entity);
        registry.emplace<ecs::NetworkOwner>(entity,
                                             ecs::NetworkOwner{ecs->IdOf(entity).id});
        registry.emplace<ecs::LocalControl>(entity, ecs::LocalControl{});
        registry.emplace<ecs::MoveInput>(entity, ecs::MoveInput{});
        registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
        registry.emplace<ecs::PathRequest>(entity, ecs::PathRequest{});
        registry.emplace<ecs::PathFollow>(entity, ecs::PathFollow{});
        registry.emplace<ecs::AttackIntent>(entity, ecs::AttackIntent{});
        registry.emplace<ecs::Container>(entity, ecs::Container{{}, playerCapacityKg});
        registry.emplace<ecs::Equipment>(entity, ecs::Equipment{});
        registry.emplace<ecs::KnownRecipes>(entity);
        registry.emplace<ecs::CraftingJob>(entity);
        return entity;
    }

    bool IsBlocked(int wx, int wy) const {
        return world == nullptr || IsSquareBlocked(*world, wx, wy, 0);
    }

    void ClearEntities() {
        if (ecs == nullptr) {
            player = entt::null;
            return;
        }
        entt::registry& registry = ecs->Registry();
        std::vector<ecs::Entity> entities;
        registry.view<ecs::StableId>().each(
            [&](const ecs::Entity entity, const ecs::StableId&) {
                if (ecs->Alive(entity)) {
                    entities.push_back(entity);
                }
            });
        for (const ecs::Entity entity : entities) {
            if (physics != nullptr) {
                if (const auto* rigid = registry.try_get<ecs::RigidBody>(entity);
                    rigid != nullptr && rigid->body != nullptr) {
                    physics->DestroyBody(rigid->body);
                }
            }
            ecs->Destroy(entity);
        }
        ecs->Flush();
        player = entt::null;
        virtualHordes.Clear();
    }

    bool RestoreRuntimeComponents(const std::vector<ecs::Entity>& loaded) {
        if (ecs == nullptr) {
            return false;
        }
        entt::registry& registry = ecs->Registry();
        for (const ecs::Entity entity : loaded) {
            if (!ecs->Alive(entity)) {
                continue;
            }
            const auto* position = registry.try_get<ecs::Position>(entity);
            if (position == nullptr) {
                continue;
            }
            const bool isPlayer = registry.all_of<ecs::PlayerTag>(entity);
            const bool isGroundItem = registry.all_of<ecs::ItemInstance>(entity);
            const bool isCorpse = registry.all_of<ecs::Corpse>(entity);
            const bool isCharacter =
                isPlayer || (registry.all_of<ecs::Health>(entity) && !isGroundItem &&
                             !isCorpse);
            if (!isCharacter) {
                continue;
            }

            if (!registry.all_of<ecs::Collider>(entity)) {
                registry.emplace<ecs::Collider>(entity);
            }
            if (physics != nullptr && !registry.all_of<ecs::RigidBody>(entity)) {
                const auto* collider = registry.try_get<ecs::Collider>(entity);
                const ecs::Collider defaults;
                const float radius = collider != nullptr ? collider->radius : defaults.radius;
                const float height = collider != nullptr ? collider->height : defaults.height;
                ecs::RigidBody rigid;
                rigid.body = physics->CreateCharacterBody(radius, height, position->x,
                                                           position->y, position->z);
                registry.emplace<ecs::RigidBody>(entity, rigid);
            }
            if (!registry.all_of<ecs::AnimState>(entity)) {
                registry.emplace<ecs::AnimState>(entity);
            }
            if (!registry.all_of<ecs::ActionState>(entity)) {
                registry.emplace<ecs::ActionState>(entity);
            }
            if (!registry.all_of<ecs::MoveInput>(entity)) {
                registry.emplace<ecs::MoveInput>(entity);
            }
            if (!registry.all_of<ecs::MoveIntent>(entity)) {
                registry.emplace<ecs::MoveIntent>(entity);
            }
            if (!registry.all_of<ecs::PathRequest>(entity)) {
                registry.emplace<ecs::PathRequest>(entity);
            }
            if (!registry.all_of<ecs::PathFollow>(entity)) {
                registry.emplace<ecs::PathFollow>(entity);
            }
            if (!registry.all_of<ecs::AttackIntent>(entity)) {
                registry.emplace<ecs::AttackIntent>(entity);
            }

            if (isPlayer) {
                if (!registry.all_of<ecs::LocalControl>(entity)) {
                    registry.emplace<ecs::LocalControl>(entity);
                }
                if (!registry.all_of<ecs::Container>(entity)) {
                    registry.emplace<ecs::Container>(entity,
                                                     ecs::Container{{}, playerCapacityKg});
                }
                if (!registry.all_of<ecs::Equipment>(entity)) {
                    registry.emplace<ecs::Equipment>(entity);
                }
                if (!registry.all_of<ecs::KnownRecipes>(entity)) {
                    registry.emplace<ecs::KnownRecipes>(entity);
                }
                if (!registry.all_of<ecs::CraftingJob>(entity)) {
                    registry.emplace<ecs::CraftingJob>(entity);
                }
                if (player == entt::null) {
                    player = entity;
                }
            } else {
                if (!registry.all_of<ecs::AiState>(entity)) {
                    registry.emplace<ecs::AiState>(entity);
                }
                if (!registry.all_of<ecs::PerceptionState>(entity)) {
                    ecs::PerceptionState perception;
                    perception.lastKnownX = position->x;
                    perception.lastKnownY = position->y;
                    registry.emplace<ecs::PerceptionState>(entity, perception);
                }
                if (!registry.all_of<ecs::LodState>(entity)) {
                    registry.emplace<ecs::LodState>(entity);
                }
            }
        }
        return true;
    }
};

WorldSimulation::WorldSimulation(Options options)
    : m_impl(std::make_unique<Impl>(std::move(options))) {}

WorldSimulation::~WorldSimulation() {
    Shutdown();
}

bool WorldSimulation::Init(std::string* error) {
    Impl& s = *m_impl;
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        ZL_ERROR("sim", "{}", message);
        return false;
    };
    if (s.initialized) {
        return true;
    }
    if (s.options.config == nullptr || s.options.events == nullptr ||
        s.options.tasks == nullptr) {
        return fail("模拟初始化缺少 config/events/tasks");
    }

    const core::Config& config = *s.options.config;
    const std::string mapDir =
        config.Get<std::string>("world.mapDir", "assets/maps/village_demo");
    world::MapLoader::Meta mapMeta;
    std::string mapError;
    if (!world::MapLoader::LoadMeta(mapDir, &mapMeta, &mapError)) {
        ZL_ERROR("world", "地图元数据加载失败（降级为空世界）: {}", mapError);
        mapMeta = world::MapLoader::Meta{};
        mapMeta.rootDir = mapDir;
        mapMeta.chunksX = 0;
        mapMeta.chunksY = 0;
    } else {
        ZL_INFO("world", "地图已加载: {}（{}×{} chunks，默认地面 {}）", mapMeta.name,
                mapMeta.chunksX, mapMeta.chunksY, mapMeta.defaultFloor);
    }

    const int loadRadius = config.Get<int>("world.loadRadius", core::kChunkLoadRadius);
    s.world = std::make_unique<world::World>();
    s.chunks = std::make_unique<world::ChunkManager>(
        *s.world, std::move(mapMeta), *s.options.tasks, loadRadius,
        std::max(1, config.Get<int>("world.chunkRequestBudget", 4)),
        std::max(1, config.Get<int>("world.chunkAttachBudget", 8)),
        std::max(0.0, config.Get<double>("world.chunkAttachBudgetMs", 2.0)));

    s.physics = std::make_unique<physics::PhysicsWorld>();
    const world::MapLoader::Meta& loadedMeta = s.chunks->Map();
    ai::AStarOptions pathOptions;
    pathOptions.maxExpansions = config.Get<int>("pathfinding.maxExpansions", 200000);
    s.pathfinding = std::make_unique<ai::PathfindingService>(
        s.options.tasks,
        ai::MapChunkBounds{loadedMeta.chunkX0, loadedMeta.chunkY0, loadedMeta.chunksX,
                           loadedMeta.chunksY},
        pathOptions);

    s.chunks->SetChunkCallbacks(
        [&s](world::Chunk& chunk, int cx, int cy) {
            if (s.options.activeSaveId != nullptr && !s.options.activeSaveId->empty() &&
                s.options.saveManager != nullptr) {
                save::SaveManager::LoadReport report;
                std::string chunkError;
                const save::SaveManager::LoadResult loaded =
                    s.options.activeSaveUseBackup != nullptr &&
                            *s.options.activeSaveUseBackup
                        ? s.options.saveManager->LoadChunkFromBackup(
                              *s.options.activeSaveId, chunk, &chunkError)
                        : s.options.saveManager->LoadChunk(*s.options.activeSaveId, chunk,
                                                           &chunkError, &report);
                if (report.usedBackup) {
                    SetWarning(s.options.saveWarning, report.warning);
                    ZL_WARN("save", "chunk ({},{}) {}", cx, cy, report.warning);
                }
                if (loaded != save::SaveManager::LoadResult::Ok &&
                    loaded != save::SaveManager::LoadResult::NotFound) {
                    SetWarning(s.options.saveWarning,
                               "chunk (" + std::to_string(cx) + "," + std::to_string(cy) +
                                   ") 加载失败：" + chunkError);
                    ZL_ERROR("save", "chunk ({},{}) 加载失败：{}", cx, cy, chunkError);
                }
            }
            s.physics->BuildChunkStatics(cx, cy, chunk);
            s.pathfinding->OnChunkLoaded(chunk);
            s.options.events->EmitQueued(core::kOnChunkLoaded, core::ChunkEvent{cx, cy});
        },
        [&s](world::Chunk& chunk, int cx, int cy) {
            if (s.options.activeSaveId != nullptr && !s.options.activeSaveId->empty() &&
                (s.options.activeSaveUseBackup == nullptr ||
                 !*s.options.activeSaveUseBackup) &&
                s.options.saveManager != nullptr) {
                std::string saveError;
                if (!s.options.saveManager->SaveChunk(*s.options.activeSaveId, chunk,
                                                       &saveError)) {
                    ZL_WARN("save", "chunk ({},{}) 卸载存档失败：{}", cx, cy, saveError);
                }
            }
            s.physics->RemoveChunkStatics(cx, cy);
            s.pathfinding->OnChunkUnloading(cx, cy);
            s.options.events->EmitQueued(core::kOnChunkUnloaded, core::ChunkEvent{cx, cy});
        });

    const auto walkable = [&s](int wx, int wy) { return !s.IsBlocked(wx, wy); };

    ai::AiSystem::AttackParams aiAttack;
    aiAttack.damage = static_cast<float>(config.Get<double>("combat.zombieDamage", 8.0));
    aiAttack.knockback =
        static_cast<float>(config.Get<double>("combat.zombieKnockback", 2.0));
    aiAttack.staggerSeconds =
        static_cast<float>(config.Get<double>("combat.zombieStaggerSeconds", 0.3));
    aiAttack.range = static_cast<float>(config.Get<double>("combat.zombieRange", 1.3));
    aiAttack.arcDegrees =
        static_cast<float>(config.Get<double>("combat.zombieArcDegrees", 120.0));
    s.aiSystem = std::make_unique<ai::AiSystem>(walkable, aiAttack);

    ai::PerceptionParams perceptionParams;
    perceptionParams.sightRadius =
        static_cast<float>(config.Get<double>("perception.sightRadius", 12.0));
    perceptionParams.closeRadius =
        static_cast<float>(config.Get<double>("perception.closeRadius", 2.5));
    perceptionParams.fovDegrees =
        static_cast<float>(config.Get<double>("perception.fovDegrees", 110.0));
    perceptionParams.alertDecaySeconds =
        static_cast<float>(config.Get<double>("perception.alertDecaySeconds", 8.0));
    perceptionParams.wallAttenuation =
        static_cast<float>(config.Get<double>("perception.wallAttenuation", 0.6));
    s.perceptionSystem =
        std::make_unique<ai::PerceptionSystem>(walkable, perceptionParams);
    s.soundSubscription = s.options.events->Subscribe(
        core::kOnSound, [&s](const core::Event& event) {
            if (s.perceptionSystem == nullptr) {
                return;
            }
            if (const auto* sound = std::get_if<core::SoundEvent>(&event.payload)) {
                s.perceptionSystem->PushSound(*sound);
            }
        });

    ai::LodSystem::Params lodParams;
    lodParams.fullRadius = static_cast<float>(config.Get<double>("lod.fullRadius", 25.0));
    lodParams.midRadius = static_cast<float>(config.Get<double>("lod.midRadius", 60.0));
    s.lodSystem = std::make_unique<ai::LodSystem>(lodParams);

    ai::BoidsSystem::Params boidsParams;
    boidsParams.neighborRadius =
        static_cast<float>(config.Get<double>("boids.neighborRadius", 2.5));
    boidsParams.separationRadius =
        static_cast<float>(config.Get<double>("boids.separationRadius", 0.8));
    boidsParams.separationWeight =
        static_cast<float>(config.Get<double>("boids.separationWeight", 1.6));
    boidsParams.cohesionWeight =
        static_cast<float>(config.Get<double>("boids.cohesionWeight", 0.5));
    boidsParams.alignmentWeight =
        static_cast<float>(config.Get<double>("boids.alignmentWeight", 0.4));
    s.boidsSystem = std::make_unique<ai::BoidsSystem>(boidsParams);

    s.combatSystem = std::make_unique<characters::CombatSystem>();
    const float deathFallSeconds =
        static_cast<float>(config.Get<double>("combat.deathFallSeconds", 0.5));

    ai::SpawnLodSystem::Params spawnParams;
    spawnParams.populationTarget = config.Get<int>("spawner.populationTarget", 240);
    spawnParams.maxLive = config.Get<int>("spawner.maxLive", 320);
    spawnParams.materializeRadius =
        static_cast<float>(config.Get<double>("spawner.materializeRadius", 32.0));
    spawnParams.materializePerSecond =
        static_cast<float>(config.Get<double>("spawner.materializePerSecond", 16.0));
    spawnParams.minSpawnDistance =
        static_cast<float>(config.Get<double>("spawner.minSpawnDistance", 30.0));
    spawnParams.virtualizeRadius =
        static_cast<float>(config.Get<double>("spawner.virtualizeRadius", 48.0));
    spawnParams.virtualizePerSecond =
        static_cast<float>(config.Get<double>("spawner.virtualizePerSecond", 12.0));
    spawnParams.refillPerSecond =
        static_cast<float>(config.Get<double>("spawner.refillPerSecond", 2.0));
    spawnParams.refillRadius =
        static_cast<float>(config.Get<double>("spawner.refillRadius", 40.0));
    spawnParams.refillBandWidth =
        static_cast<float>(config.Get<double>("spawner.refillBandWidth", 20.0));
    spawnParams.mergeRadius =
        static_cast<float>(config.Get<double>("spawner.mergeRadius", 15.0));
    s.spawnLodSystem = std::make_unique<ai::SpawnLodSystem>(
        walkable,
        [&s](ecs::EcsWorld& world, float px, float py) {
            return SpawnZombie(world, s.physics.get(), px, py, s.options.events);
        },
        [&s](ecs::EcsWorld& world, ecs::Entity entity) {
            if (s.physics == nullptr) {
                return;
            }
            if (const auto* rigid = world.Registry().try_get<ecs::RigidBody>(entity);
                rigid != nullptr && rigid->body != nullptr) {
                s.physics->DestroyBody(rigid->body);
            }
        },
        s.virtualHordes, spawnParams);

    s.playerMaxHp = static_cast<float>(config.Get<double>("health.playerMaxHp", 100.0));
    s.playerStartHp = static_cast<float>(
        config.Get<double>("health.playerStartHp", static_cast<double>(s.playerMaxHp)));
    s.playerCapacityKg =
        static_cast<float>(config.Get<double>("inventory.playerCapacityKg", 20.0));

    s.ecs = std::make_unique<ecs::EcsWorld>();
    s.inputSystem = std::make_unique<characters::InputSystem>();
    s.movementSystem = std::make_unique<characters::MovementSystem>();
    s.physicsSyncSystem = std::make_unique<physics::PhysicsSyncSystem>(s.physics.get());
    s.actionPresentationSystem =
        std::make_unique<characters::ActionPresentationSystem>();
    s.animationSystem = std::make_unique<characters::AnimationSystem>();
    s.pathFollowSystem = std::make_unique<ai::PathFollowSystem>(s.pathfinding.get());
    s.deathSystem = std::make_unique<characters::DeathSystem>(
        s.physics.get(), deathFallSeconds, s.options.events);

    ecs::SystemScheduler& systems = s.ecs->Systems();
    systems.AddSystem("SpatialIndexSystem", [&s](ecs::EcsWorld& value, float) {
        s.spatial.Update(value);
    });
    systems.AddSystem("LodSystem", [&s](ecs::EcsWorld& value, float) {
        s.lodSystem->SetFocus(s.focus.x, s.focus.y);
        s.lodSystem->Update(value);
    });
    systems.AddSystem("InputSystem",
                      [&s](ecs::EcsWorld& value, float) { s.inputSystem->Update(value); });
    systems.AddSystem("PerceptionSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.perceptionSystem->Update(value, dt);
    });
    systems.AddSystem("AiSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.aiSystem->Update(value, dt);
    });
    systems.AddSystem("PathFollowSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.pathFollowSystem->Update(value, dt);
    });
    systems.AddSystem("BoidsSystem",
                      [&s](ecs::EcsWorld& value, float) { s.boidsSystem->Update(value); });
    systems.AddSystem("CombatSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.combatSystem->Update(value, dt);
    });
    systems.AddSystem("VirtualHordeSystem", [&s](ecs::EcsWorld& value, float dt) {
        float px = 0.0f;
        float py = 0.0f;
        bool hasPlayer = false;
        value.Registry().view<ecs::PlayerTag, ecs::Position>().each(
            [&](const ecs::Entity, const ecs::Position& position) {
                if (!hasPlayer) {
                    px = position.x;
                    py = position.y;
                    hasPlayer = true;
                }
            });
        if (hasPlayer) {
            s.virtualHordes.Update(dt, px, py, /*stopDistance=*/12.0f);
        }
    });
    systems.AddSystem("MovementSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.movementSystem->Update(value, dt);
    });
    systems.AddSystem("PhysicsSyncSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.physicsSyncSystem->Update(value, dt);
    });
    systems.AddSystem("DeathSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.deathSystem->Update(value, dt);
    });
    systems.AddSystem("SpawnLodSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.spawnLodSystem->SetFocus(s.focus.x, s.focus.y);
        s.spawnLodSystem->Update(value, dt);
    });
    systems.AddSystem("AnimationSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.animationSystem->Update(value, dt);
    });
    systems.AddSystem("ActionPresentationSystem",
                      [&s](ecs::EcsWorld& value, float dt) {
                          s.actionPresentationSystem->Update(value, dt);
                      });

    s.focus = s.options.focus;
    s.initialized = true;
    ZL_INFO("sim", "模拟已就绪（无窗口可运行）：{} 个系统，世界 {}×{} chunks",
            systems.Count(), loadedMeta.chunksX, loadedMeta.chunksY);
    return true;
}

void WorldSimulation::Shutdown() {
    Impl& s = *m_impl;
    const bool hadResources = s.initialized || s.world != nullptr ||
                              s.chunks != nullptr || s.ecs != nullptr;
    if (!hadResources) {
        return;
    }
    if (s.options.events != nullptr &&
        s.soundSubscription != core::EventBus::kInvalidSubscription) {
        s.options.events->Unsubscribe(s.soundSubscription);
    }
    s.soundSubscription = core::EventBus::kInvalidSubscription;
    s.spawnLodSystem.reset();
    s.actionPresentationSystem.reset();
    s.animationSystem.reset();
    s.deathSystem.reset();
    s.combatSystem.reset();
    s.physicsSyncSystem.reset();
    s.movementSystem.reset();
    s.inputSystem.reset();
    s.pathFollowSystem.reset();
    s.boidsSystem.reset();
    s.lodSystem.reset();
    s.aiSystem.reset();
    s.perceptionSystem.reset();
    s.pathfinding.reset();
    s.physics.reset();
    s.chunks.reset();
    s.world.reset();
    s.ecs.reset();
    s.initialized = false;
    ZL_INFO("sim", "模拟已关闭");
}

void WorldSimulation::SetFocus(float gridX, float gridY) {
    if (std::isfinite(gridX) && std::isfinite(gridY)) {
        m_impl->focus = glm::vec2(gridX, gridY);
    }
}

glm::vec2 WorldSimulation::Focus() const {
    return m_impl->focus;
}

void WorldSimulation::Stream() {
    Impl& s = *m_impl;
    if (!s.initialized || s.chunks == nullptr) {
        return;
    }
    s.chunks->Update(s.focus.x, s.focus.y);
    s.chunks->ProcessResults();
}

void WorldSimulation::Tick(float dt) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr) {
        return;
    }
    s.ecs->Update(std::max(0.0f, dt));
}

void WorldSimulation::Flush() {
    if (m_impl->initialized && m_impl->ecs != nullptr) {
        m_impl->ecs->Flush();
    }
}

ecs::Entity WorldSimulation::SpawnNetworkPlayer(float targetX, float targetY,
                                                int searchRadius,
                                                std::uint64_t stableId) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr || s.world == nullptr) {
        return entt::null;
    }
    const int targetGridX = static_cast<int>(std::floor(targetX));
    const int targetGridY = static_cast<int>(std::floor(targetY));
    const world::ChunkCoord targetChunk = world::ChunkOf(targetGridX, targetGridY);
    const world::Chunk* chunk = FindChunkInWorld(*s.world, targetChunk.cx, targetChunk.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return entt::null;
    }
    int spawnX = 0;
    int spawnY = 0;
    if (!ai::FindNearestWalkable(
            targetGridX, targetGridY, std::max(1, searchRadius),
            [&s](int wx, int wy) { return !s.IsBlocked(wx, wy); }, &spawnX, &spawnY)) {
        return entt::null;
    }
    const ecs::Entity entity =
        s.SpawnPlayer(static_cast<float>(spawnX) + 0.5f,
                      static_cast<float>(spawnY) + 0.5f, stableId);
    if (entity == entt::null) {
        return entt::null;
    }
    if (s.player == entt::null || !s.ecs->Alive(s.player)) {
        s.player = entity;
    }
    s.focus = glm::vec2(static_cast<float>(spawnX) + 0.5f,
                        static_cast<float>(spawnY) + 0.5f);
    ZL_INFO("sim", "网络玩家生成：stableId={}，格 ({}, {})（目标 ({}, {})）",
            s.ecs->IdOf(entity).id, spawnX, spawnY, targetGridX, targetGridY);
    return entity;
}

ecs::Entity WorldSimulation::RestoreNetworkPlayer(const save::SavedEntity& snapshot,
                                                  bool resetHealth,
                                                  std::string* error) {
    Impl& s = *m_impl;
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return entt::null;
    };
    if (!s.initialized || s.ecs == nullptr || s.options.saveComponents == nullptr) {
        return fail("恢复玩家失败：模拟或存档组件未初始化");
    }
    if (snapshot.stableId == 0) {
        return fail("恢复玩家失败：StableId 为 0");
    }
    if (s.ecs->FindById(snapshot.stableId) != entt::null) {
        return fail("恢复玩家失败：StableId 已存在");
    }

    const ecs::Entity entity = s.ecs->CreateWithStableId(snapshot.stableId);
    if (entity == entt::null) {
        return fail("恢复玩家失败：无法创建 StableId " +
                    std::to_string(snapshot.stableId));
    }
    std::string applyError;
    if (!save::ApplyEntity(snapshot, *s.ecs, entity, *s.options.saveComponents,
                           &applyError)) {
        s.ecs->Destroy(entity);
        s.ecs->Flush();
        return fail("恢复玩家失败：" + applyError);
    }

    if (!s.RestoreRuntimeComponents({entity})) {
        s.ecs->Destroy(entity);
        s.ecs->Flush();
        return fail("恢复玩家失败：运行时组件恢复失败");
    }

    entt::registry& registry = s.ecs->Registry();
    if (!registry.all_of<ecs::PlayerTag>(entity)) {
        registry.emplace<ecs::PlayerTag>(entity);
    }
    registry.emplace_or_replace<ecs::NetworkOwner>(
        entity, ecs::NetworkOwner{snapshot.stableId});
    if (!registry.all_of<ecs::LocalControl>(entity)) {
        registry.emplace<ecs::LocalControl>(entity, ecs::LocalControl{});
    }
    if (!registry.all_of<ecs::Container>(entity)) {
        registry.emplace<ecs::Container>(entity,
                                         ecs::Container{{}, s.playerCapacityKg});
    }
    if (!registry.all_of<ecs::Equipment>(entity)) {
        registry.emplace<ecs::Equipment>(entity, ecs::Equipment{});
    }
    if (!registry.all_of<ecs::KnownRecipes>(entity)) {
        registry.emplace<ecs::KnownRecipes>(entity);
    }
    if (!registry.all_of<ecs::CraftingJob>(entity)) {
        registry.emplace<ecs::CraftingJob>(entity);
    }
    registry.remove<ecs::Dying>(entity);
    if (resetHealth) {
        if (auto* health = registry.try_get<ecs::Health>(entity);
            health != nullptr) {
            health->current = health->max;
        } else {
            ecs::Health value;
            value.max = s.playerMaxHp > 0.0f ? s.playerMaxHp : 100.0f;
            value.current = value.max;
            registry.emplace<ecs::Health>(entity, value);
        }
    }

    if (s.player == entt::null || !s.ecs->Alive(s.player)) {
        s.player = entity;
    }
    if (const auto* position = registry.try_get<ecs::Position>(entity);
        position != nullptr) {
        s.focus = glm::vec2(position->x, position->y);
    }
    ZL_INFO("sim", "网络玩家已从档案恢复：stableId={}（生命 {:.1f}/{:.1f}）",
            snapshot.stableId,
            registry.try_get<ecs::Health>(entity) != nullptr
                ? registry.get<ecs::Health>(entity).current
                : 0.0f,
            registry.try_get<ecs::Health>(entity) != nullptr
                ? registry.get<ecs::Health>(entity).max
                : 0.0f);
    return entity;
}

ecs::Entity WorldSimulation::RespawnNetworkPlayer(const save::SavedEntity& snapshot,
                                                  float targetX, float targetY,
                                                  int searchRadius, bool resetHealth,
                                                  std::string* error) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr || s.world == nullptr) {
        if (error != nullptr) {
            *error = "复活玩家失败：模拟未初始化";
        }
        return entt::null;
    }
    const int targetGridX = static_cast<int>(std::floor(targetX));
    const int targetGridY = static_cast<int>(std::floor(targetY));
    const world::ChunkCoord targetChunk = world::ChunkOf(targetGridX, targetGridY);
    const world::Chunk* chunk = FindChunkInWorld(*s.world, targetChunk.cx, targetChunk.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        if (error != nullptr) {
            *error = "复活点 chunk 尚未加载";
        }
        return entt::null;
    }
    int spawnX = 0;
    int spawnY = 0;
    if (!ai::FindNearestWalkable(
            targetGridX, targetGridY, std::max(1, searchRadius),
            [&s](int wx, int wy) { return !s.IsBlocked(wx, wy); }, &spawnX,
            &spawnY)) {
        if (error != nullptr) {
            *error = "复活点附近没有可行走格";
        }
        return entt::null;
    }

    const ecs::Entity entity =
        RestoreNetworkPlayer(snapshot, resetHealth, error);
    if (entity == entt::null) {
        return entt::null;
    }
    entt::registry& registry = s.ecs->Registry();
    if (auto* position = registry.try_get<ecs::Position>(entity);
        position != nullptr) {
        position->x = static_cast<float>(spawnX) + 0.5f;
        position->y = static_cast<float>(spawnY) + 0.5f;
        position->z = 0;
    }
    if (auto* rigid = registry.try_get<ecs::RigidBody>(entity);
        rigid != nullptr && rigid->body != nullptr && s.physics != nullptr) {
        s.physics->TeleportBody(rigid->body, static_cast<float>(spawnX) + 0.5f,
                                static_cast<float>(spawnY) + 0.5f, 0);
    }
    s.focus = glm::vec2(static_cast<float>(spawnX) + 0.5f,
                        static_cast<float>(spawnY) + 0.5f);
    ZL_INFO("sim", "网络玩家复活：stableId={}，出生格 ({}, {})", snapshot.stableId,
            spawnX, spawnY);
    return entity;
}

ecs::Entity WorldSimulation::SpawnZombieAt(float x, float y) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return entt::null;
    }
    return SpawnZombie(*s.ecs, s.physics.get(), x, y, s.options.events);
}

void WorldSimulation::DespawnEntity(ecs::Entity entity) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr || entity == entt::null ||
        !s.ecs->Alive(entity)) {
        return;
    }
    entt::registry& registry = s.ecs->Registry();
    if (s.physics != nullptr) {
        if (const auto* rigid = registry.try_get<ecs::RigidBody>(entity);
            rigid != nullptr && rigid->body != nullptr) {
            s.physics->DestroyBody(rigid->body);
        }
    }
    const std::uint64_t stableId = s.ecs->IdOf(entity).id;
    s.ecs->Destroy(entity);
    if (s.player == entity) {
        s.player = entt::null;
    }
    ZL_INFO("sim", "实体已从服务器移除：stableId={}", stableId);
}

ecs::Entity WorldSimulation::EnsureLocalPlayer(float targetX, float targetY,
                                               int searchRadius) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr) {
        return entt::null;
    }
    if (s.player != entt::null && !s.ecs->Alive(s.player)) {
        s.player = entt::null;
    }
    if (s.player != entt::null) {
        return s.player;
    }

    const int targetGridX = static_cast<int>(std::floor(targetX));
    const int targetGridY = static_cast<int>(std::floor(targetY));
    const world::ChunkCoord targetChunk = world::ChunkOf(targetGridX, targetGridY);
    const world::Chunk* chunk = FindChunkInWorld(*s.world, targetChunk.cx, targetChunk.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return entt::null;
    }

    int spawnX = 0;
    int spawnY = 0;
    if (!ai::FindNearestWalkable(
            targetGridX, targetGridY, std::max(1, searchRadius),
            [&s](int wx, int wy) { return !s.IsBlocked(wx, wy); }, &spawnX, &spawnY)) {
        return entt::null;
    }

    s.player = s.SpawnPlayer(static_cast<float>(spawnX) + 0.5f,
                             static_cast<float>(spawnY) + 0.5f);
    if (s.player != entt::null) {
        s.focus = glm::vec2(static_cast<float>(spawnX) + 0.5f,
                            static_cast<float>(spawnY) + 0.5f);
        const int radius =
            std::max(std::abs(spawnX - targetGridX), std::abs(spawnY - targetGridY));
        ZL_INFO("sim", "玩家生成：格 ({}, {})（目标 ({}, {})，搜索半径 {}）", spawnX, spawnY,
                targetGridX, targetGridY, radius);
    }
    return s.player;
}

void WorldSimulation::SpawnDemoPopulation() {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr) {
        return;
    }
    std::mt19937 rng(20261016u);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    constexpr float kBoxX0 = 44.0f;
    constexpr float kBoxY0 = 4.0f;
    constexpr float kBoxX1 = 96.0f;
    constexpr float kBoxY1 = 32.0f;
    constexpr int kCount = 40;
    for (int i = 0; i < kCount; ++i) {
        const float px = kBoxX0 + u01(rng) * (kBoxX1 - kBoxX0);
        const float py = kBoxY0 + u01(rng) * (kBoxY1 - kBoxY0);
        SpawnZombie(*s.ecs, s.physics.get(), px, py, s.options.events);
    }
    ZL_INFO("sim", "生成 {} 个 AI 游荡者（区域 x[{:.0f},{:.0f}] y[{:.0f},{:.0f}]）",
            kCount, kBoxX0, kBoxX1, kBoxY0, kBoxY1);

    s.virtualHordes.Clear();
    constexpr int kHordes = 5;
    constexpr int kPerHorde = 40;
    for (int i = 0; i < kHordes; ++i) {
        const float angle = static_cast<float>(i) * (6.2831853f / kHordes);
        const float radius = 55.0f + static_cast<float>(i) * 5.0f;
        s.virtualHordes.AddHorde(70.0f + std::cos(angle) * radius,
                                 18.0f + std::sin(angle) * radius, kPerHorde,
                                 /*spread=*/8.0f);
    }
    const auto stats = s.virtualHordes.GetStats(70.0f, 18.0f);
    ZL_INFO("sim", "虚拟群落种子：{} 群 / {} 只（最近 {:.0f} 格）", stats.hordes,
            stats.zombies, stats.nearestDist);
}

bool WorldSimulation::LoadSave(const std::string& saveId, std::string* error) {
    Impl& s = *m_impl;
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        ZL_ERROR("save", "{}", message);
        return false;
    };
    if (!s.initialized || s.ecs == nullptr || s.options.saveManager == nullptr ||
        s.options.saveComponents == nullptr) {
        return fail("读档失败：模拟或存档服务未初始化");
    }
    if (saveId.empty()) {
        return fail("读档失败：存档 id 为空");
    }

    save::SaveMetadata metadata;
    save::EntitySnapshot snapshot;
    save::SaveManager::LoadReport report;
    std::string loadError;
    const save::SaveManager::LoadResult result =
        s.options.saveManager->Load(saveId, *s.options.saveComponents, &snapshot,
                                    &metadata, &loadError, &report);
    if (result != save::SaveManager::LoadResult::Ok) {
        return fail("读档失败（" + saveId + "）：" + loadError);
    }
    const std::string currentMap = s.chunks != nullptr ? s.chunks->Map().name : std::string();
    if (!metadata.mapId.empty() && !currentMap.empty() && metadata.mapId != currentMap) {
        return fail("存档地图不匹配：" + metadata.mapId + "（当前 " + currentMap + "）");
    }

    s.ClearEntities();
    std::vector<ecs::Entity> loaded;
    std::string entityError;
    if (!save::LoadEntities(snapshot, *s.ecs, *s.options.saveComponents, &loaded,
                            &entityError)) {
        return fail("读取实体失败：" + entityError);
    }
    if (!s.RestoreRuntimeComponents(loaded)) {
        return fail("恢复运行时组件失败");
    }

    if (s.options.activeSaveId != nullptr) {
        *s.options.activeSaveId = saveId;
    }
    if (s.options.activeSaveUseBackup != nullptr) {
        *s.options.activeSaveUseBackup = report.usedBackup;
    }
    if (s.options.saveWarning != nullptr) {
        s.options.saveWarning->clear();
    }
    if (report.usedBackup) {
        SetWarning(s.options.saveWarning, report.warning);
    }
    if (s.player != entt::null) {
        if (const auto* position = s.ecs->Registry().try_get<ecs::Position>(s.player);
            position != nullptr) {
            s.focus = glm::vec2(position->x, position->y);
        }
    }
    ZL_INFO("save", "已读取存档 '{}'：实体 {}，玩家 {}，缩略图 {}", saveId, loaded.size(),
            s.player != entt::null ? 1 : 0,
            metadata.thumbnail.empty() ? "无" : metadata.thumbnail);
    return true;
}

ecs::Entity WorldSimulation::Player() const {
    const Impl& s = *m_impl;
    if (s.ecs == nullptr) {
        return entt::null;
    }
    if (s.player != entt::null && s.ecs->Alive(s.player)) {
        return s.player;
    }
    // 多玩家服务器：首个玩家离开后，缓存句柄会失效；按 PlayerTag 找任一存活玩家。
    ecs::Entity found = entt::null;
    s.ecs->Registry().view<ecs::PlayerTag>().each(
        [&](const ecs::Entity entity) {
            if (found == entt::null && s.ecs->Alive(entity)) {
                found = entity;
            }
        });
    return found;
}

ecs::EcsWorld& WorldSimulation::Ecs() {
    return *m_impl->ecs;
}

ecs::SpatialIndex& WorldSimulation::Spatial() {
    return m_impl->spatial;
}

world::World& WorldSimulation::World() {
    return *m_impl->world;
}

world::ChunkManager& WorldSimulation::Chunks() {
    return *m_impl->chunks;
}

physics::PhysicsWorld& WorldSimulation::Physics() {
    return *m_impl->physics;
}

ai::PathfindingService* WorldSimulation::Pathfinding() {
    return m_impl->pathfinding.get();
}

ai::AiSystem* WorldSimulation::Ai() {
    return m_impl->aiSystem.get();
}

ai::PerceptionSystem* WorldSimulation::Perception() {
    return m_impl->perceptionSystem.get();
}

ai::LodSystem* WorldSimulation::Lod() {
    return m_impl->lodSystem.get();
}

ai::BoidsSystem* WorldSimulation::Boids() {
    return m_impl->boidsSystem.get();
}

ai::VirtualHordeStore* WorldSimulation::VirtualHordes() {
    return &m_impl->virtualHordes;
}

ai::SpawnLodSystem* WorldSimulation::SpawnLod() {
    return m_impl->spawnLodSystem.get();
}

characters::CombatSystem* WorldSimulation::Combat() {
    return m_impl->combatSystem.get();
}

}  // namespace zl::game
