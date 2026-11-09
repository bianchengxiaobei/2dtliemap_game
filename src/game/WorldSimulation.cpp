#include "game/WorldSimulation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <string>
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
#include "ai/systems/ZombieSpecialSystem.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "characters/systems/ActionPresentationSystem.h"
#include "characters/systems/AnimationSystem.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/CombatRules.h"
#include "characters/systems/InfectionSystem.h"
#include "characters/systems/PainSystem.h"
#include "characters/systems/WoundSystem.h"
#include "characters/systems/DeathSystem.h"
#include "characters/systems/InputSystem.h"
#include "characters/systems/MovementSystem.h"
#include "characters/systems/NeedsSystem.h"
#include "core/Config.h"
#include "core/Constants.h"
#include "core/EventBus.h"
#include "core/GameTime.h"
#include "core/Log.h"
#include "core/TaskScheduler.h"
#include "ecs/SpatialIndex.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/Armor.h"
#include "game/Climate.h"
#include "game/FoodSafety.h"
#include "game/FireSystem.h"
#include "game/GroundItems.h"
#include "game/HomesteadComponents.h"
#include "game/Objectives.h"
#include "game/MissionComponents.h"
#include "game/PoiComponents.h"
#include "game/WeatherStatus.h"
#include "game/AnimalSystem.h"
#include "game/Animals.h"
#include "game/Poi.h"
#include "game/SettlementNpc.h"
#include "game/WorldQueries.h"
#include "game/ZombieFactory.h"
#include "game/ZombieVariants.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/Spoilage.h"
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

int CurrentZombieSpawnHour(const core::GameTime* gameTime) {
    return gameTime != nullptr ? gameTime->Now().hour : 12;
}

ecs::DamageType ParseAttackType(const std::string& value, ecs::DamageType fallback) {
    if (value == "claw") {
        return ecs::DamageType::Claw;
    }
    if (value == "bite") {
        return ecs::DamageType::Bite;
    }
    if (value == "blunt") {
        return ecs::DamageType::Blunt;
    }
    return fallback;
}

ai::AiSystem::AttackProfile LoadZombieAttack(const core::Config& config,
                                             const std::string& key,
                                             ai::AiSystem::AttackProfile profile) {
    profile.damage =
        static_cast<float>(config.Get<double>(key + ".damage", profile.damage));
    profile.knockback =
        static_cast<float>(config.Get<double>(key + ".knockback", profile.knockback));
    profile.staggerSeconds = static_cast<float>(
        config.Get<double>(key + ".staggerSeconds", profile.staggerSeconds));
    profile.range = static_cast<float>(config.Get<double>(key + ".range", profile.range));
    profile.arcDegrees =
        static_cast<float>(config.Get<double>(key + ".arcDegrees", profile.arcDegrees));
    profile.windupSeconds = static_cast<float>(
        config.Get<double>(key + ".windupSeconds", profile.windupSeconds));
    profile.cooldownSeconds = static_cast<float>(
        config.Get<double>(key + ".cooldownSeconds", profile.cooldownSeconds));
    profile.type = ParseAttackType(
        config.Get<std::string>(key + ".type", ""), profile.type);
    return profile;
}

}  // namespace

characters::CombatRuleConfig LoadCombatRuleConfig(const core::Config& config) {
    characters::CombatRuleConfig rules =
        characters::DefaultCombatRuleConfig();
    const auto read = [&config](const char* key, float fallback) {
        return static_cast<float>(
            config.Get<double>(std::string("combat.rules.") + key, fallback));
    };

    rules.staminaMax = read("staminaMax", rules.staminaMax);
    rules.staminaRegenPerSecond =
        read("staminaRegenPerSecond", rules.staminaRegenPerSecond);
    rules.staminaRegenDelaySeconds =
        read("staminaRegenDelaySeconds", rules.staminaRegenDelaySeconds);
    rules.staminaExhaustedThreshold =
        read("staminaExhaustedThreshold", rules.staminaExhaustedThreshold);
    rules.staminaExhaustedSeconds =
        read("staminaExhaustedSeconds", rules.staminaExhaustedSeconds);
    rules.fatigueMaxPenalty = read("fatigueMaxPenalty", rules.fatigueMaxPenalty);
    rules.fatigueRegenPenalty =
        read("fatigueRegenPenalty", rules.fatigueRegenPenalty);

    rules.heavyDamageScale = read("heavyDamageScale", rules.heavyDamageScale);
    rules.heavyCooldownScale =
        read("heavyCooldownScale", rules.heavyCooldownScale);
    rules.heavyRangeBonus = read("heavyRangeBonus", rules.heavyRangeBonus);
    rules.heavyArcScale = read("heavyArcScale", rules.heavyArcScale);
    rules.heavyKnockbackScale =
        read("heavyKnockbackScale", rules.heavyKnockbackScale);
    rules.heavyStaggerScale =
        read("heavyStaggerScale", rules.heavyStaggerScale);
    rules.heavyWearScale = read("heavyWearScale", rules.heavyWearScale);
    rules.heavyStaminaScale =
        read("heavyStaminaScale", rules.heavyStaminaScale);
    rules.heavyPoiseScale = read("heavyPoiseScale", rules.heavyPoiseScale);

    rules.blockMoveScale = read("blockMoveScale", rules.blockMoveScale);
    rules.blockArcDegrees = read("blockArcDegrees", rules.blockArcDegrees);
    rules.blockDamageScale = read("blockDamageScale", rules.blockDamageScale);
    rules.blockKnockbackScale =
        read("blockKnockbackScale", rules.blockKnockbackScale);
    rules.blockBaseStaminaCost =
        read("blockBaseStaminaCost", rules.blockBaseStaminaCost);
    rules.blockStaminaPerWeight =
        read("blockStaminaPerWeight", rules.blockStaminaPerWeight);
    rules.blockMinStaminaCost =
        read("blockMinStaminaCost", rules.blockMinStaminaCost);
    rules.blockMaxStaminaCost =
        read("blockMaxStaminaCost", rules.blockMaxStaminaCost);

    rules.parryWindowSeconds =
        read("parryWindowSeconds", rules.parryWindowSeconds);
    rules.parryStaminaCost =
        read("parryStaminaCost", rules.parryStaminaCost);
    rules.parryCooldownSeconds =
        read("parryCooldownSeconds", rules.parryCooldownSeconds);
    rules.parryStaggerSeconds =
        read("parryStaggerSeconds", rules.parryStaggerSeconds);
    rules.guardBreakSeconds =
        read("guardBreakSeconds", rules.guardBreakSeconds);

    rules.dodgeStaminaCost = read("dodgeStaminaCost", rules.dodgeStaminaCost);
    rules.dodgeDurationSeconds =
        read("dodgeDurationSeconds", rules.dodgeDurationSeconds);
    rules.dodgeInvulnSeconds =
        read("dodgeInvulnSeconds", rules.dodgeInvulnSeconds);
    rules.dodgeCooldownSeconds =
        read("dodgeCooldownSeconds", rules.dodgeCooldownSeconds);
    rules.dodgeDistance = read("dodgeDistance", rules.dodgeDistance);

    rules.poiseRegenPerSecond =
        read("poiseRegenPerSecond", rules.poiseRegenPerSecond);
    rules.poiseRegenDelaySeconds =
        read("poiseRegenDelaySeconds", rules.poiseRegenDelaySeconds);
    rules.poiseBreakStaggerSeconds =
        read("poiseBreakStaggerSeconds", rules.poiseBreakStaggerSeconds);
    rules.poiseResetRatio = read("poiseResetRatio", rules.poiseResetRatio);
    rules.poiseMaxNormal = read("poiseMaxNormal", rules.poiseMaxNormal);
    rules.poiseMaxFast = read("poiseMaxFast", rules.poiseMaxFast);
    rules.poiseMaxStrong = read("poiseMaxStrong", rules.poiseMaxStrong);
    rules.poiseMaxArmored = read("poiseMaxArmored", rules.poiseMaxArmored);
    rules.armoredStaggerPoiseThreshold = read(
        "armoredStaggerPoiseThreshold", rules.armoredStaggerPoiseThreshold);

    return characters::SanitizeCombatRuleConfig(rules);
}

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
    std::unique_ptr<characters::NeedsSystem> needsSystem;
    std::unique_ptr<characters::MovementSystem> movementSystem;
    std::unique_ptr<physics::PhysicsSyncSystem> physicsSyncSystem;
    std::unique_ptr<characters::ActionPresentationSystem> actionPresentationSystem;
    std::unique_ptr<characters::AnimationSystem> animationSystem;
    std::unique_ptr<characters::DeathSystem> deathSystem;
    std::unique_ptr<characters::CombatSystem> combatSystem;
    characters::CombatRuleConfig combatRules =
        characters::DefaultCombatRuleConfig();
    std::vector<characters::CombatSystem::BlockFeedback> blockFeedbacks;
    std::vector<characters::CombatSystem::HitFeedback> hitFeedbacks;
    std::unique_ptr<characters::WoundSystem> woundSystem;
    std::unique_ptr<characters::PainSystem> painSystem;
    std::unique_ptr<characters::InfectionSystem> infectionSystem;
    characters::InfectionParams infectionParams;
    std::mt19937 infectionRng{20261024u};
    std::unique_ptr<game::WeatherSystem> weatherSystem;
    std::unique_ptr<game::AnimalSystem> animalSystem;
    std::unique_ptr<game::SettlementNpcSystem> settlementNpcSystem;
    std::unique_ptr<game::FireSystem> fireSystem;
    float fireDamageAccumulator = 0.0f;
    game::ClimateParams climateParams;
    WeatherState currentWeather;
    float campfireWarmthRadius = 3.0f;
    WeatherKind lastWeatherKind = WeatherKind::Clear;
    bool weatherLogged = false;

    std::unique_ptr<ai::PathFollowSystem> pathFollowSystem;
    std::unique_ptr<ai::AiSystem> aiSystem;
    std::unique_ptr<ai::PerceptionSystem> perceptionSystem;
    std::unique_ptr<ai::ZombieSpecialSystem> zombieSpecialSystem;
    std::unique_ptr<ai::LodSystem> lodSystem;
    std::unique_ptr<ai::BoidsSystem> boidsSystem;
    ai::VirtualHordeStore virtualHordes;
    std::unique_ptr<ai::SpawnLodSystem> spawnLodSystem;

    core::EventBus::SubscriptionId soundSubscription =
        core::EventBus::kInvalidSubscription;
    ecs::Entity player = entt::null;
    std::uint64_t zombieSpawnSerial = 0;
    float playerMaxHp = 100.0f;
    float playerStartHp = 100.0f;
    float playerCapacityKg = 20.0f;
    float foodSpoilScale = 1.0f;
    float spoilAccumulatorMinutes = 0.0f;
    double lastSpoilGameMinute = -1.0;
    FoodSafetyParams foodSafety;
    double lastFoodGameMinute = -1.0;

    void AdvanceFoodSpoilage(float minutes) {
        if (options.items == nullptr || ecs == nullptr || !(minutes > 0.0f)) {
            return;
        }
        entt::registry& registry = ecs->Registry();
        registry.view<ecs::Container>().each(
            [&](const ecs::Entity entity, ecs::Container& container) {
                const auto* cold = registry.try_get<ecs::ColdStorage>(entity);
                if (cold != nullptr && cold->powered) {
                    return;
                }
                items::AdvanceSpoilage(container, *options.items, minutes,
                                       foodSpoilScale);
            });
        registry.view<ecs::ItemInstance>().each(
            [&](const ecs::Entity, ecs::ItemInstance& item) {
                const items::ItemDef* def = options.items->Find(item.defId);
                if (def == nullptr || !items::IsPerishable(*def) ||
                    !(item.condition > 0.0f)) {
                    return;
                }
                const float life = std::max(def->spoilDays * 1440.0f, 1.0f);
                item.condition = std::max(
                    0.0f, item.condition -
                              minutes * foodSpoilScale / life);
            });
    }

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
        registry.emplace<ecs::Pain>(entity, ecs::Pain{});
        registry.emplace<ecs::Infection>(entity, ecs::Infection{});
        registry.emplace<ecs::Climate>(entity, ecs::Climate{});

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
        const float staminaMax = characters::EffectiveMaxStamina(
            0.0f, combatRules);
        registry.emplace<ecs::CombatStamina>(
            entity, ecs::CombatStamina{staminaMax, staminaMax, 0.0f, 0.0f});
        registry.emplace<ecs::CombatStance>(entity, ecs::CombatStance{});
        registry.emplace<ecs::Container>(entity, ecs::Container{{}, playerCapacityKg});
        registry.emplace<ecs::Equipment>(entity, ecs::Equipment{});
        registry.emplace<ecs::DamageResist>(entity, ecs::DamageResist{});
        registry.emplace<ecs::KnownRecipes>(entity);
        registry.emplace<ecs::CraftingJob>(entity);
        registry.emplace<ecs::Progression>(entity);
        registry.emplace<ecs::Objective>(entity);
        registry.emplace<ecs::PoiTasks>(entity);
        registry.emplace<ecs::MissionLog>(entity);
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
            if (registry.all_of<ecs::Animal>(entity)) {
                continue;
            }
            const bool isPlayer = registry.all_of<ecs::PlayerTag>(entity);
            const bool isGroundItem = registry.all_of<ecs::ItemInstance>(entity);
            const bool isCorpse = registry.all_of<ecs::Corpse>(entity);
            const bool isFacility = registry.any_of<
                ecs::FarmPlot, ecs::Generator, ecs::ColdStorage,
                ecs::Floodlight, ecs::SpikeTrap, ecs::PlayerStorage,
                ecs::MerchantStock>(entity);
            const bool isCharacter =
                isPlayer || (registry.all_of<ecs::Health>(entity) && !isGroundItem &&
                             !isCorpse && !isFacility);
            if (!isCharacter) {
                continue;
            }

            float visualScale = 1.0f;
            if (!isPlayer) {
                if (const auto* variant = registry.try_get<ecs::ZombieVariant>(entity);
                    variant != nullptr) {
                    visualScale = std::clamp(variant->visualScale, 0.5f, 2.0f);
                    // M30-T5：硬直条不存档；读档时按变体重新初始化。
                    EnsureZombiePoise(*ecs, entity, variant->kind, &combatRules);
                }
            }
            if (!registry.all_of<ecs::Collider>(entity)) {
                const ecs::Collider defaults;
                registry.emplace<ecs::Collider>(
                    entity, ecs::Collider{defaults.radius * visualScale,
                                          defaults.height * visualScale, defaults.category,
                                          defaults.mask});
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
                if (!registry.all_of<ecs::CombatStamina>(entity)) {
                    const auto* needs = registry.try_get<ecs::Needs>(entity);
                    const float fatigue = needs != nullptr ? needs->fatigue : 0.0f;
                    const float staminaMax = characters::EffectiveMaxStamina(
                        fatigue, combatRules);
                    registry.emplace<ecs::CombatStamina>(
                        entity,
                        ecs::CombatStamina{staminaMax, staminaMax, 0.0f, 0.0f});
                }
                if (!registry.all_of<ecs::CombatStance>(entity)) {
                    registry.emplace<ecs::CombatStance>(entity);
                }
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
                if (!registry.all_of<ecs::DamageResist>(entity)) {
                    registry.emplace<ecs::DamageResist>(entity);
                }
                if (!registry.all_of<ecs::KnownRecipes>(entity)) {
                    registry.emplace<ecs::KnownRecipes>(entity);
                }
                if (!registry.all_of<ecs::CraftingJob>(entity)) {
                    registry.emplace<ecs::CraftingJob>(entity);
                }
                if (!registry.all_of<ecs::Progression>(entity)) {
                    registry.emplace<ecs::Progression>(entity);
                }
                if (!registry.all_of<ecs::Pain>(entity)) {
                    registry.emplace<ecs::Pain>(entity);
                }
                if (!registry.all_of<ecs::Infection>(entity)) {
                    registry.emplace<ecs::Infection>(entity);
                }
                if (!registry.all_of<ecs::Climate>(entity)) {
                    registry.emplace<ecs::Climate>(entity);
                }
                if (!registry.all_of<ecs::Objective>(entity)) {
                    registry.emplace<ecs::Objective>(entity);
                }
                if (!registry.all_of<ecs::PoiTasks>(entity)) {
                    registry.emplace<ecs::PoiTasks>(entity);
                }
                if (!registry.all_of<ecs::MissionLog>(entity)) {
                    registry.emplace<ecs::MissionLog>(entity);
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

    WeatherState SampleWeather() const {
        if (!weatherSystem) {
            return WeatherState{};
        }
        const double minutes =
            options.gameTime != nullptr ? options.gameTime->TotalGameMinutes() : 0.0;
        const int hour =
            options.gameTime != nullptr ? options.gameTime->Now().hour : 12;
        return weatherSystem->Sample(minutes, hour);
    }

    void UpdateWeather() {
        currentWeather = SampleWeather();
        if (perceptionSystem != nullptr) {
            perceptionSystem->SetEnvironmentSightScale(currentWeather.sightScale);
        }
        if (pathFollowSystem != nullptr) {
            pathFollowSystem->SetEnvironmentMoveScale(currentWeather.moveScale);
        }
        if (!weatherLogged || currentWeather.kind != lastWeatherKind) {
            lastWeatherKind = currentWeather.kind;
            weatherLogged = true;
            ZL_INFO("weather", "天气切换：{}，环境 {:.1f}°C，能见度 x{:.2f}，尸速 x{:.2f}，游戏分钟 {:.1f}，周期 {:.1f}",
                    WeatherSystem::KindName(currentWeather.kind),
                    static_cast<double>(currentWeather.ambientC),
                    static_cast<double>(currentWeather.sightScale),
                    static_cast<double>(currentWeather.moveScale),
                    options.gameTime != nullptr
                        ? options.gameTime->TotalGameMinutes()
                        : 0.0,
                    static_cast<double>(weatherSystem->Params().periodMinutes));
        }
    }

    void UpdateClimate(ecs::EcsWorld& value, float dt) {
        if (ecs == nullptr || !(dt > 0.0f)) {
            return;
        }
        entt::registry& registry = value.Registry();
        registry.view<ecs::PlayerTag, ecs::Position>().each(
            [&](const ecs::Entity entity, const ecs::Position& position) {
                auto* health = registry.try_get<ecs::Health>(entity);
                if (health == nullptr || health->current <= 0.0f ||
                    registry.any_of<ecs::Dying>(entity)) {
                    return;
                }
                ecs::Climate& climate =
                    registry.get_or_emplace<ecs::Climate>(entity);
                game::PlayerArmor armor;
                if (options.items != nullptr) {
                    if (const auto* equipment =
                            registry.try_get<ecs::Equipment>(entity);
                        equipment != nullptr) {
                        armor = game::ResolvePlayerArmor(*equipment, *options.items);
                    }
                }
                game::ClimateEnvironment environment;
                environment.ambientC = currentWeather.ambientC;
                environment.rainIntensity = currentWeather.wetnessIntensity;
                environment.insulation = armor.insulation;
                environment.waterproof = armor.waterproof;
                environment.indoor =
                    world != nullptr &&
                    game::IsIndoors(*world, position.x, position.y, position.z);
                environment.nearFire =
                    fireSystem != nullptr
                        ? fireSystem->HasActiveFireNear(value, position.x,
                                                        position.y,
                                                        position.z)
                        : (world != nullptr &&
                           game::HasCampfireNearby(
                               *world, position.x, position.y, position.z,
                               campfireWarmthRadius));
                const std::uint8_t previousWarning = climate.warning;
                const game::ClimateTick tick = game::UpdateClimate(
                    climate, environment, climateParams, dt);

                if (tick.fatigueDelta > 0.0f) {
                    if (auto* needs = registry.try_get<ecs::Needs>(entity);
                        needs != nullptr) {
                        needs->fatigue = std::clamp(
                            needs->fatigue + tick.fatigueDelta, 0.0f, 1.0f);
                    }
                }
                if (tick.damaging) {
                    const bool wasAlive = health->current > 0.0f;
                    health->current =
                        std::max(0.0f, health->current - tick.healthDamage);
                    ecs::LastDamage& last =
                        registry.get_or_emplace<ecs::LastDamage>(entity);
                    last.cause = ecs::DamageCause::Hypothermia;
                    last.amount = tick.healthDamage;
                    if (wasAlive && health->current <= 0.0f) {
                        ZL_INFO("weather", "玩家因失温死亡（体温 {:.1f}°C）",
                                static_cast<double>(climate.bodyC));
                    }
                }
                if (climate.warning != previousWarning) {
                    ZL_INFO("weather",
                            "玩家气候状态 {}：体温 {:.1f}°C，潮湿 {:.0f}%，"
                            "体感 {:.1f}°C，室内 {}，篝火 {}",
                            static_cast<int>(climate.warning),
                            static_cast<double>(climate.bodyC),
                            static_cast<double>(climate.wetness * 100.0f),
                            static_cast<double>(climate.effectiveC),
                            climate.indoor ? "是" : "否",
                            climate.nearFire ? "是" : "否");
                }
            });
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
            if (s.animalSystem != nullptr) {
                s.animalSystem->OnChunkLoaded(cx, cy);
            }
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
            if (s.animalSystem != nullptr) {
                s.animalSystem->OnChunkUnloaded(cx, cy);
            }
            s.physics->RemoveChunkStatics(cx, cy);
            s.pathfinding->OnChunkUnloading(cx, cy);
            if (s.fireSystem != nullptr && s.ecs != nullptr) {
                (void)s.fireSystem->UnloadChunk(*s.ecs, cx, cy);
            }
            s.options.events->EmitQueued(core::kOnChunkUnloaded, core::ChunkEvent{cx, cy});
        });

    const auto walkable = [&s](int wx, int wy) { return !s.IsBlocked(wx, wy); };

    ai::AiSystem::AttackParams aiAttack;
    aiAttack.normal = LoadZombieAttack(config, "combat.zombieAttacks.normal",
                                      aiAttack.normal);
    aiAttack.fast =
        LoadZombieAttack(config, "combat.zombieAttacks.fast", aiAttack.fast);
    aiAttack.strong =
        LoadZombieAttack(config, "combat.zombieAttacks.strong", aiAttack.strong);
    aiAttack.armored = LoadZombieAttack(config, "combat.zombieAttacks.armored",
                                        aiAttack.armored);
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
            const auto* sound = std::get_if<core::SoundEvent>(&event.payload);
            if (sound == nullptr) {
                return;
            }
            if (s.perceptionSystem != nullptr) {
                s.perceptionSystem->PushSound(*sound);
            }
            if (s.animalSystem != nullptr) {
                s.animalSystem->PushSound(*sound);
            }
        });

    ai::ZombieSpecialParams specialParams;
    specialParams.howlRange = static_cast<float>(
        config.Get<double>("baseEvent.howlerRange", specialParams.howlRange));
    specialParams.howlAbortRange = static_cast<float>(config.Get<double>(
        "baseEvent.howlerAbortRange", specialParams.howlAbortRange));
    specialParams.howlWindupSeconds = static_cast<float>(config.Get<double>(
        "baseEvent.howlerWindupSeconds", specialParams.howlWindupSeconds));
    specialParams.howlCooldownSeconds = static_cast<float>(config.Get<double>(
        "baseEvent.howlerCooldownSeconds", specialParams.howlCooldownSeconds));
    specialParams.howlRadius = static_cast<float>(
        config.Get<double>("baseEvent.howlerRadius", specialParams.howlRadius));
    s.zombieSpecialSystem = std::make_unique<ai::ZombieSpecialSystem>(
        specialParams, [&s](const core::SoundEvent& sound) {
            if (s.options.events != nullptr) {
                s.options.events->EmitQueued(core::kOnSound, sound);
            } else if (s.perceptionSystem != nullptr) {
                s.perceptionSystem->PushSound(sound);
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

    characters::WoundParams woundParams;
    woundParams.severityPerDamage =
        static_cast<float>(config.Get<double>("wounds.severityPerDamage", 0.02));
    woundParams.clawSeverityScale =
        static_cast<float>(config.Get<double>("wounds.clawSeverityScale", 1.2));
    woundParams.biteSeverityScale =
        static_cast<float>(config.Get<double>("wounds.biteSeverityScale", 1.6));
    woundParams.bluntSeverityScale =
        static_cast<float>(config.Get<double>("wounds.bluntSeverityScale", 1.0));
    woundParams.clawBleedPerDamage =
        static_cast<float>(config.Get<double>("wounds.clawBleedPerDamage", 0.035));
    woundParams.biteBleedPerDamage =
        static_cast<float>(config.Get<double>("wounds.biteBleedPerDamage", 0.08));
    woundParams.clawBleedSeconds =
        static_cast<float>(config.Get<double>("wounds.clawBleedSeconds", 18.0));
    woundParams.biteBleedSeconds =
        static_cast<float>(config.Get<double>("wounds.biteBleedSeconds", 30.0));
    woundParams.maxBleedRate =
        static_cast<float>(config.Get<double>("wounds.maxBleedRate", 8.0));
    s.combatRules = LoadCombatRuleConfig(config);
    s.combatSystem =
        std::make_unique<characters::CombatSystem>(woundParams, s.combatRules);
    s.combatSystem->SetBlockFeedbackCallback(
        [&s](const characters::CombatSystem::BlockFeedback& feedback) {
            constexpr std::size_t kMaxPendingFeedback = 64;
            if (s.blockFeedbacks.size() >= kMaxPendingFeedback) {
                s.blockFeedbacks.erase(s.blockFeedbacks.begin());
            }
            s.blockFeedbacks.push_back(feedback);
        });
    s.combatSystem->SetHitFeedbackCallback(
        [&s](const characters::CombatSystem::HitFeedback& feedback) {
            constexpr std::size_t kMaxPendingFeedback = 64;
            if (s.hitFeedbacks.size() >= kMaxPendingFeedback) {
                s.hitFeedbacks.erase(s.hitFeedbacks.begin());
            }
            s.hitFeedbacks.push_back(feedback);
        });
    s.woundSystem = std::make_unique<characters::WoundSystem>();
    characters::PainParams painParams;
    painParams.perSeverity =
        static_cast<float>(config.Get<double>("pain.perSeverity", 0.65));
    painParams.bandagedScale =
        static_cast<float>(config.Get<double>("pain.bandagedScale", 0.5));
    painParams.maxLevel = static_cast<float>(config.Get<double>("pain.maxLevel", 0.9));
    painParams.maxMoveSlow =
        static_cast<float>(config.Get<double>("pain.maxMoveSlow", 0.35));
    painParams.maxAttackCooldownIncrease = static_cast<float>(
        config.Get<double>("pain.maxAttackCooldownIncrease", 0.5));
    s.painSystem = std::make_unique<characters::PainSystem>(painParams);
    characters::InfectionParams infectionParams;
    infectionParams.enabled = config.Get<bool>("infection.enabled", true);
    infectionParams.clawChance =
        static_cast<float>(config.Get<double>("infection.clawChance", 0.06));
    infectionParams.biteChance =
        static_cast<float>(config.Get<double>("infection.biteChance", 0.20));
    infectionParams.woundRiskBonus =
        static_cast<float>(config.Get<double>("infection.woundRiskBonus", 0.55));
    infectionParams.maxChance =
        static_cast<float>(config.Get<double>("infection.maxChance", 0.75));
    infectionParams.totalSeconds =
        static_cast<float>(config.Get<double>("infection.totalSeconds", 900.0));
    infectionParams.incubatingEnd =
        static_cast<float>(config.Get<double>("infection.incubatingEnd", 0.30));
    infectionParams.feverEnd =
        static_cast<float>(config.Get<double>("infection.feverEnd", 0.70));
    infectionParams.woundProgressionBonus =
        static_cast<float>(config.Get<double>("infection.woundProgressionBonus", 0.80));
    infectionParams.feverFatiguePerSecond = static_cast<float>(
        config.Get<double>("infection.feverFatiguePerSecond", 0.0015));
    infectionParams.severeFatiguePerSecond = static_cast<float>(
        config.Get<double>("infection.severeFatiguePerSecond", 0.0030));
    infectionParams.severeHealthDrainPerSecond = static_cast<float>(
        config.Get<double>("infection.severeHealthDrainPerSecond", 0.08));
    s.infectionParams = infectionParams;
    s.infectionRng.seed(static_cast<std::uint32_t>(
        config.Get<int>("infection.seed", 20261024)));
    s.infectionSystem =
        std::make_unique<characters::InfectionSystem>(infectionParams);
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
            const int hour = CurrentZombieSpawnHour(s.options.gameTime);
            const ecs::ZombieVariantKind variant =
                PickZombieVariant(px, py, hour, ++s.zombieSpawnSerial);
            return SpawnZombie(world, s.physics.get(), px, py, s.options.events,
                               variant, 0, &s.combatRules);
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
    s.foodSpoilScale = std::clamp(
        static_cast<float>(config.Get<double>("food.spoilScale", 1.0)), 0.0f,
        100.0f);

    game::WeatherParams weatherParams;
    weatherParams.enabled = config.Get<bool>("weather.enabled", true);
    weatherParams.seed = static_cast<std::uint32_t>(
        std::max<std::int64_t>(0, config.Get<int>("weather.seed", 20261024)));
    weatherParams.periodMinutes = static_cast<float>(
        config.Get<double>("weather.periodMinutes", 180.0));
    weatherParams.transitionMinutes = static_cast<float>(
        config.Get<double>("weather.transitionMinutes", 30.0));
    weatherParams.clearWeight =
        static_cast<float>(config.Get<double>("weather.clearWeight", 0.45));
    weatherParams.rainWeight =
        static_cast<float>(config.Get<double>("weather.rainWeight", 0.25));
    weatherParams.fogWeight =
        static_cast<float>(config.Get<double>("weather.fogWeight", 0.17));
    weatherParams.coldSnapWeight =
        static_cast<float>(config.Get<double>("weather.coldSnapWeight", 0.13));
    weatherParams.clearTemperatureC = static_cast<float>(
        config.Get<double>("weather.clearTemperatureC", 24.0));
    weatherParams.rainTemperatureC = static_cast<float>(
        config.Get<double>("weather.rainTemperatureC", 13.0));
    weatherParams.fogTemperatureC = static_cast<float>(
        config.Get<double>("weather.fogTemperatureC", 16.0));
    weatherParams.coldSnapTemperatureC = static_cast<float>(
        config.Get<double>("weather.coldSnapTemperatureC", -6.0));
    weatherParams.nightTemperatureDropC = static_cast<float>(
        config.Get<double>("weather.nightTemperatureDropC", 6.0));
    weatherParams.rainSightScale = static_cast<float>(
        config.Get<double>("weather.rainSightScale", 0.80));
    weatherParams.fogSightScale = static_cast<float>(
        config.Get<double>("weather.fogSightScale", 0.55));
    weatherParams.coldSnapSightScale = static_cast<float>(
        config.Get<double>("weather.coldSnapSightScale", 0.90));
    weatherParams.rainMoveScale = static_cast<float>(
        config.Get<double>("weather.rainMoveScale", 0.95));
    weatherParams.fogMoveScale = static_cast<float>(
        config.Get<double>("weather.fogMoveScale", 0.90));
    weatherParams.coldSnapMoveScale = static_cast<float>(
        config.Get<double>("weather.coldSnapMoveScale", 0.88));
    weatherParams.rainWetness = static_cast<float>(
        config.Get<double>("weather.rainWetness", 0.80));
    weatherParams.fogWetness = static_cast<float>(
        config.Get<double>("weather.fogWetness", 0.18));
    weatherParams.coldSnapWetness = static_cast<float>(
        config.Get<double>("weather.coldSnapWetness", 0.08));
    s.weatherSystem = std::make_unique<game::WeatherSystem>(weatherParams);
    if (s.options.fire != nullptr) {
        game::FireParams fireParams;
        fireParams.ignitionSeconds = static_cast<float>(
            config.Get<double>("fire.ignitionSeconds", fireParams.ignitionSeconds));
        fireParams.wetnessPenalty = static_cast<float>(
            config.Get<double>("fire.wetnessPenalty", fireParams.wetnessPenalty));
        fireParams.minIgnitionChance = static_cast<float>(
            config.Get<double>("fire.minIgnitionChance", fireParams.minIgnitionChance));
        fireParams.smolderSeconds = static_cast<float>(
            config.Get<double>("fire.smolderSeconds", fireParams.smolderSeconds));
        fireParams.spreadIntervalSeconds = static_cast<float>(
            config.Get<double>("fire.spreadIntervalSeconds",
                               fireParams.spreadIntervalSeconds));
        fireParams.spreadChanceScale = static_cast<float>(
            config.Get<double>("fire.spreadChanceScale",
                               fireParams.spreadChanceScale));
        fireParams.rainBurnDecayScale = static_cast<float>(
            config.Get<double>("fire.rainBurnDecayScale",
                               fireParams.rainBurnDecayScale));
        fireParams.maxActiveSources =
            config.Get<int>("fire.maxActiveSources",
                            fireParams.maxActiveSources);
        fireParams.maxSpreadsPerTick =
            config.Get<int>("fire.maxSpreadsPerTick",
                            fireParams.maxSpreadsPerTick);
        fireParams.extinguishRange = static_cast<float>(
            config.Get<double>("fire.extinguishRange",
                               fireParams.extinguishRange));
        fireParams.bareHandsChance = static_cast<float>(
            config.Get<double>("fire.bareHandsChance",
                               fireParams.bareHandsChance));
        fireParams.bareHandsSelfDamage = static_cast<float>(
            config.Get<double>("fire.bareHandsSelfDamage",
                               fireParams.bareHandsSelfDamage));
        fireParams.fireDamagePerSecond = static_cast<float>(
            config.Get<double>("fire.fireDamagePerSecond",
                               fireParams.fireDamagePerSecond));
        fireParams.fireDamageRadius = static_cast<float>(
            config.Get<double>("fire.fireDamageRadius",
                               fireParams.fireDamageRadius));
        fireParams.fireDamageIntervalSeconds = static_cast<float>(
            config.Get<double>("fire.fireDamageIntervalSeconds",
                               fireParams.fireDamageIntervalSeconds));
        fireParams.zombieBurnSeconds = static_cast<float>(
            config.Get<double>("fire.zombieBurnSeconds",
                               fireParams.zombieBurnSeconds));
        fireParams.zombieBurnDamagePerSecond = static_cast<float>(
            config.Get<double>("fire.zombieBurnDamagePerSecond",
                               fireParams.zombieBurnDamagePerSecond));
        fireParams.zombieBurnDamageIntervalSeconds = static_cast<float>(
            config.Get<double>("fire.zombieBurnDamageIntervalSeconds",
                               fireParams.zombieBurnDamageIntervalSeconds));
        fireParams.zombieSpreadIntervalSeconds = static_cast<float>(
            config.Get<double>("fire.zombieSpreadIntervalSeconds",
                               fireParams.zombieSpreadIntervalSeconds));
        fireParams.zombieSpreadChance = static_cast<float>(
            config.Get<double>("fire.zombieSpreadChance",
                               fireParams.zombieSpreadChance));
        fireParams.zombiePanicSeconds = static_cast<float>(
            config.Get<double>("fire.zombiePanicSeconds",
                               fireParams.zombiePanicSeconds));
        fireParams.zombiePanicSpeedScale = static_cast<float>(
            config.Get<double>("fire.zombiePanicSpeedScale",
                               fireParams.zombiePanicSpeedScale));
        fireParams.zombiePanicChaseRadius = static_cast<float>(
            config.Get<double>("fire.zombiePanicChaseRadius",
                               fireParams.zombiePanicChaseRadius));
        s.fireSystem = std::make_unique<game::FireSystem>(
            *s.options.fire, fireParams,
            static_cast<std::uint32_t>(
                std::max<std::int64_t>(0, config.Get<int>("fire.seed", 20261029))));
    }

    game::ClimateParams climateParams;
    climateParams.rainWetnessPerSecond = static_cast<float>(
        config.Get<double>("climate.rainWetnessPerSecond", 0.025));
    climateParams.dryPerSecond =
        static_cast<float>(config.Get<double>("climate.dryPerSecond", 0.012));
    climateParams.fireDryPerSecond = static_cast<float>(
        config.Get<double>("climate.fireDryPerSecond", 0.05));
    climateParams.comfortC =
        static_cast<float>(config.Get<double>("climate.comfortC", 37.0));
    climateParams.coldStartC =
        static_cast<float>(config.Get<double>("climate.coldStartC", 18.0));
    climateParams.coldRate =
        static_cast<float>(config.Get<double>("climate.coldRate", 0.15));
    climateParams.bodyResponseSeconds = static_cast<float>(
        config.Get<double>("climate.bodyResponseSeconds", 120.0));
    climateParams.indoorWarmthC = static_cast<float>(
        config.Get<double>("climate.indoorWarmthC", 14.0));
    climateParams.fireWarmthC =
        static_cast<float>(config.Get<double>("climate.fireWarmthC", 18.0));
    climateParams.insulationWarmthC = static_cast<float>(
        config.Get<double>("climate.insulationWarmthC", 8.0));
    climateParams.wetCoolingC =
        static_cast<float>(config.Get<double>("climate.wetCoolingC", 6.0));
    climateParams.indoorMinBodyC = static_cast<float>(
        config.Get<double>("climate.indoorMinBodyC", 36.2));
    climateParams.fireMinBodyC = static_cast<float>(
        config.Get<double>("climate.fireMinBodyC", 36.8));
    climateParams.coldWarnC =
        static_cast<float>(config.Get<double>("climate.coldWarnC", 36.0));
    climateParams.coldFatigueThresholdC = static_cast<float>(
        config.Get<double>("climate.coldFatigueThresholdC", 36.0));
    climateParams.coldFatiguePerSecond = static_cast<float>(
        config.Get<double>("climate.coldFatiguePerSecond", 0.001));
    climateParams.hypothermiaThresholdC = static_cast<float>(
        config.Get<double>("climate.hypothermiaThresholdC", 35.0));
    climateParams.hypothermiaDamagePerSecond = static_cast<float>(
        config.Get<double>("climate.hypothermiaDamagePerSecond", 0.15));
    climateParams.coldMovePenalty = static_cast<float>(
        config.Get<double>("climate.coldMovePenalty", 0.12));
    climateParams.coldMoveMin =
        static_cast<float>(config.Get<double>("climate.coldMoveMin", 0.65));
    climateParams.wetWarnLevel =
        static_cast<float>(config.Get<double>("climate.wetWarnLevel", 0.60));
    s.climateParams = game::SanitizeClimateParams(climateParams);
    s.campfireWarmthRadius = static_cast<float>(
        std::max(0.5, config.Get<double>("climate.campfireRadius", 3.0)));

    characters::NeedsParams needsParams;
    needsParams.hungerPerDay =
        static_cast<float>(config.Get<double>("needs.hungerPerDay", 1.4));
    needsParams.thirstPerDay =
        static_cast<float>(config.Get<double>("needs.thirstPerDay", 1.8));
    needsParams.fatiguePerDay =
        static_cast<float>(config.Get<double>("needs.fatiguePerDay", 1.2));
    needsParams.gameSecondsPerDay = static_cast<float>(
        std::max(1.0, config.Get<double>("game.dayLengthMinutes", 60.0) * 60.0));
    needsParams.hungerDamageThreshold = static_cast<float>(
        config.Get<double>("needs.hungerDamageThreshold", 0.70));
    needsParams.hungerDamagePenalty = static_cast<float>(
        config.Get<double>("needs.hungerDamagePenalty", 0.35));
    needsParams.fatigueDamageThreshold = static_cast<float>(
        config.Get<double>("needs.fatigueDamageThreshold", 0.80));
    needsParams.fatigueDamagePenalty = static_cast<float>(
        config.Get<double>("needs.fatigueDamagePenalty", 0.20));
    needsParams.thirstMoveThreshold = static_cast<float>(
        config.Get<double>("needs.thirstMoveThreshold", 0.65));
    needsParams.thirstMovePenalty = static_cast<float>(
        config.Get<double>("needs.thirstMovePenalty", 0.30));
    needsParams.fatigueMoveThreshold = static_cast<float>(
        config.Get<double>("needs.fatigueMoveThreshold", 0.70));
    needsParams.fatigueMovePenalty = static_cast<float>(
        config.Get<double>("needs.fatigueMovePenalty", 0.30));
    needsParams.minMoveScale =
        static_cast<float>(config.Get<double>("needs.minMoveScale", 0.35));
    needsParams.hungerCritical =
        static_cast<float>(config.Get<double>("needs.hungerCritical", 0.90));
    needsParams.thirstCritical =
        static_cast<float>(config.Get<double>("needs.thirstCritical", 0.90));
    needsParams.starvationHpPerSecond = static_cast<float>(
        config.Get<double>("needs.starvationHpPerSecond", 0.35));
    needsParams.dehydrationHpPerSecond = static_cast<float>(
        config.Get<double>("needs.dehydrationHpPerSecond", 0.60));
    needsParams.nutritionHungerRelief = static_cast<float>(
        config.Get<double>("food.nutritionHungerRelief", 0.25));
    needsParams.nutritionDamageRelief = static_cast<float>(
        config.Get<double>("food.nutritionDamageRelief", 0.35));

    s.foodSafety.spoiledPoisonChance = static_cast<float>(
        config.Get<double>("food.spoiledPoisonChance", 0.85));
    s.foodSafety.poisonDurationSeconds = static_cast<float>(
        config.Get<double>("food.poisonDurationSeconds", 45.0));
    s.foodSafety.poisonDamagePerSecond = static_cast<float>(
        config.Get<double>("food.poisonDamagePerSecond", 0.12));
    s.foodSafety.nutritionDecayPerDay = static_cast<float>(
        config.Get<double>("food.nutritionDecayPerDay", 0.5));
    s.foodSafety.nutritionPoisonReduction = static_cast<float>(
        config.Get<double>("food.nutritionPoisonReduction", 0.5));

    s.ecs = std::make_unique<ecs::EcsWorld>();
    if (s.options.spawnAnimals && s.options.animals != nullptr) {
        game::AnimalSystem::Params animalParams;
        animalParams.enabled = config.Get<bool>("animals.spawn.enabled", true);
        animalParams.logAnimals =
            config.Get<bool>("animals.spawn.logAnimals", false);
        animalParams.maxLive = config.Get<int>("animals.spawn.maxLive", 36);
        animalParams.minPlayerDistance = static_cast<float>(
            config.Get<double>("animals.spawn.minPlayerDistance", 18.0));
        animalParams.poiRadius = static_cast<float>(
            config.Get<double>("animals.spawn.poiRadius", 16.0));
        animalParams.seed = static_cast<std::uint32_t>(std::max<std::int64_t>(
            0, config.Get<int>("animals.spawn.seed", 20261030)));
        animalParams.soundWallAttenuation = perceptionParams.wallAttenuation;
        s.animalSystem = std::make_unique<game::AnimalSystem>(
            *s.options.animals, *s.world, *s.ecs, s.options.pois,
            animalParams);
        s.animalSystem->SetCombat(s.combatSystem.get());
        ZL_INFO("animals", "动物生成：上限 {}，最小玩家距离 {:.1f}，种子 {}",
                animalParams.maxLive,
                static_cast<double>(animalParams.minPlayerDistance),
                animalParams.seed);
    }
    s.combatSystem->SetArmorWearCallback(
        [&s](ecs::Entity target, float amount) {
            entt::registry& registry = s.ecs->Registry();
            auto* equipment = registry.try_get<ecs::Equipment>(target);
            if (equipment == nullptr) {
                return;
            }
            const auto& slot =
                equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::Body)];
            const std::string defId = slot.has_value() ? slot->defId : std::string();
            if (items::WearEquippedArmor(*equipment, amount)) {
                ZL_INFO("characters", "护甲损坏：{}（目标稳定 ID {}）", defId,
                        s.ecs->IdOf(target).id);
            }
        });
    s.combatSystem->SetInfectionExposureCallback(
        [&s](ecs::Entity target, ecs::DamageType type, float) {
            if (!s.infectionParams.enabled || s.ecs == nullptr ||
                !s.ecs->Alive(target)) {
                return;
            }
            entt::registry& registry = s.ecs->Registry();
            if (!registry.all_of<ecs::PlayerTag>(target)) {
                return;
            }
            auto* infection = registry.try_get<ecs::Infection>(target);
            if (infection == nullptr) {
                infection = &registry.emplace<ecs::Infection>(target);
            }
            if (infection->stage != ecs::InfectionStage::None) {
                return;
            }
            const auto* wounds = registry.try_get<ecs::Wounds>(target);
            const ecs::Wounds emptyWounds;
            const ecs::Wounds& woundRef = wounds != nullptr ? *wounds : emptyWounds;
            const float chance =
                characters::ExposureChance(type, woundRef, s.infectionParams);
            std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
            const float roll = distribution(s.infectionRng);
            if (characters::TryInfect(*infection, chance, roll, &woundRef,
                                      s.infectionParams)) {
                ZL_INFO("infection",
                        "玩家感染暴露成功：类型 {}，概率 {:.0f}%，稳定 ID {}",
                        static_cast<int>(type),
                        static_cast<double>(chance * 100.0f),
                        s.ecs->IdOf(target).id);
            }
        });
    s.inputSystem = std::make_unique<characters::InputSystem>();
    s.needsSystem = std::make_unique<characters::NeedsSystem>(needsParams);
    s.movementSystem = std::make_unique<characters::MovementSystem>();
    s.physicsSyncSystem = std::make_unique<physics::PhysicsSyncSystem>(s.physics.get());
    s.actionPresentationSystem =
        std::make_unique<characters::ActionPresentationSystem>();
    s.animationSystem = std::make_unique<characters::AnimationSystem>();
    s.pathFollowSystem = std::make_unique<ai::PathFollowSystem>(s.pathfinding.get());
    s.settlementNpcSystem = std::make_unique<game::SettlementNpcSystem>(
        s.options.settlements,
        [&s](int wx, int wy) { return !s.IsBlocked(wx, wy); });
    s.settlementNpcSystem->SetDamageCallback(
        [&s](ecs::Entity source, ecs::Entity target, float damage,
             ecs::DamageType type) {
            if (s.combatSystem == nullptr || s.ecs == nullptr ||
                !s.ecs->Alive(source) || !s.ecs->Alive(target)) {
                return false;
            }
            entt::registry& registry = s.ecs->Registry();
            const auto* sourcePosition =
                registry.try_get<ecs::Position>(source);
            const auto* targetPosition =
                registry.try_get<ecs::Position>(target);
            if (sourcePosition == nullptr || targetPosition == nullptr) {
                return false;
            }
            ecs::DamageEvent event;
            event.sourceId = s.ecs->IdOf(source).id;
            event.type = type;
            event.amount = std::max(0.0f, damage);
            const float dx = targetPosition->x - sourcePosition->x;
            const float dy = targetPosition->y - sourcePosition->y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.0001f) {
                event.dirX = dx / length;
                event.dirY = dy / length;
            }
            event.knockback = 0.8f;
            event.staggerSeconds = 0.2f;
            return s.combatSystem->ApplyDamage(*s.ecs, target, event);
        });
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
    systems.AddSystem("WeatherSystem", [&s](ecs::EcsWorld&, float) {
        s.UpdateWeather();
    });
    systems.AddSystem("FireSystem", [&s](ecs::EcsWorld& value, float dt) {
        if (s.fireSystem == nullptr) {
            return;
        }
        std::vector<game::FireBurnOut> burnOuts;
        s.fireSystem->Update(value, dt, s.world.get(),
                             s.currentWeather.wetnessIntensity, &burnOuts);
        if (s.world != nullptr) {
            for (const game::FireBurnOut& burned : burnOuts) {
                const game::FireBurnResolution resolution =
                    s.fireSystem->ResolveBurnedCell(*s.world, burned);
                if (resolution.structureRemoved) {
                    const world::ChunkCoord coord =
                        world::ChunkOf(burned.x, burned.y);
                    world::Chunk* chunk =
                        FindChunkInWorld(*s.world, coord.cx, coord.cy);
                    if (chunk != nullptr &&
                        chunk->State() == world::ChunkState::Loaded) {
                        s.physics->BuildChunkStatics(coord.cx, coord.cy,
                                                     *chunk);
                        s.pathfinding->OnChunkLoaded(*chunk);
                    }
                }
                if (!resolution.structureRemoved || s.options.items == nullptr) {
                    continue;
                }
                for (const game::FireBurnedDrop& drop : resolution.drops) {
                    (void)game::SpawnGroundItem(
                        value, *s.options.items,
                        ecs::ItemInstance{drop.defId, drop.count, 1.0f},
                        static_cast<float>(burned.x) + 0.5f,
                        static_cast<float>(burned.y) + 0.5f, burned.z);
                }
            }
        }
        if (s.world != nullptr) {
            (void)s.fireSystem->SpreadFromBurningZombies(
                value, *s.world, dt, s.currentWeather.wetnessIntensity);
        }
        const game::FireParams& params = s.fireSystem->Params();
        if (s.combatSystem != nullptr) {
            (void)game::ApplyBurningZombieDamage(value, *s.combatSystem,
                                                 params, dt);
        }
        s.fireDamageAccumulator += dt;
        if (s.combatSystem != nullptr &&
            s.fireDamageAccumulator >= params.fireDamageIntervalSeconds) {
            (void)game::ApplyFireDamage(value, *s.combatSystem, params,
                                        s.fireDamageAccumulator);
            s.fireDamageAccumulator = 0.0f;
        }
    });
    systems.AddSystem("NeedsSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.needsSystem->Update(value, dt);
    });
    systems.AddSystem("FoodStateSystem", [&s](ecs::EcsWorld& value, float dt) {
        if (s.options.gameTime == nullptr) {
            return;
        }
        const double now = s.options.gameTime->TotalGameMinutes();
        float gameMinutes = 0.0f;
        if (s.lastFoodGameMinute >= 0.0 && now > s.lastFoodGameMinute) {
            gameMinutes = static_cast<float>(now - s.lastFoodGameMinute);
        }
        s.lastFoodGameMinute = now;
        game::AdvanceFoodStates(value, dt, gameMinutes, s.foodSafety);
    });
    systems.AddSystem("InputSystem",
                      [&s](ecs::EcsWorld& value, float) { s.inputSystem->Update(value); });
    systems.AddSystem("PerceptionSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.perceptionSystem->Update(value, dt);
    });
    systems.AddSystem("AiSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.aiSystem->Update(value, dt);
    });
    systems.AddSystem("ZombieSpecialSystem",
                      [&s](ecs::EcsWorld& value, float dt) {
                          s.zombieSpecialSystem->Update(value, dt);
                      });
    systems.AddSystem("SettlementNpcSystem",
                      [&s](ecs::EcsWorld& value, float dt) {
                          if (s.settlementNpcSystem == nullptr) {
                              return;
                          }
                          s.settlementNpcSystem->SetFocus(s.focus.x, s.focus.y);
                          int hour = 12;
                          int day = 1;
                          if (s.options.gameTime != nullptr) {
                              const auto now = s.options.gameTime->Now();
                              hour = now.hour;
                              day = now.day;
                          }
                          s.settlementNpcSystem->Update(value, dt, hour, day);
                      });
    systems.AddSystem("PathFollowSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.pathFollowSystem->Update(value, dt);
    });
    systems.AddSystem("BoidsSystem",
                      [&s](ecs::EcsWorld& value, float) { s.boidsSystem->Update(value); });
    systems.AddSystem("AnimalSystem", [&s](ecs::EcsWorld&, float dt) {
        if (s.animalSystem == nullptr) {
            return;
        }
        s.animalSystem->SetFocus(s.focus.x, s.focus.y);
        s.animalSystem->Update(dt);
    });
    systems.AddSystem("ArmorSystem", [&s](ecs::EcsWorld& value, float) {
        if (s.options.items == nullptr) {
            return;
        }
        entt::registry& registry = value.Registry();
        registry.view<ecs::PlayerTag, ecs::Equipment>().each(
            [&](const ecs::Entity entity, const ecs::Equipment& equipment) {
                const game::PlayerArmor armor =
                    game::ResolvePlayerArmor(equipment, *s.options.items);
                registry.emplace_or_replace<ecs::DamageResist>(entity,
                                                              armor.resist);
            });
    });
    systems.AddSystem("CombatSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.combatSystem->Update(value, dt);
    });
    systems.AddSystem("WoundSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.woundSystem->Update(value, dt);
    });
    systems.AddSystem("PainSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.painSystem->Update(value, dt);
    });
    systems.AddSystem("InfectionSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.infectionSystem->Update(value, dt);
    });
    systems.AddSystem("ClimateSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.UpdateClimate(value, dt);
    });
    // M15-T1：CombatSystem 只产生磨损请求，由 game 层落到主手 Equipment；
    // 保持 characters 不依赖 items 模块。
    systems.AddSystem("WeaponWearSystem", [&s](ecs::EcsWorld& value, float) {
        entt::registry& registry = value.Registry();
        for (const characters::CombatSystem::WeaponWearRequest& request :
             s.combatSystem->GetWeaponWears()) {
            if (!value.Alive(request.attacker)) {
                continue;
            }
            auto* equipment = registry.try_get<ecs::Equipment>(request.attacker);
            if (equipment == nullptr) {
                continue;
            }
            const auto& slot =
                equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
            const std::string defId = slot.has_value() ? slot->defId : std::string();
            if (items::WearEquippedWeapon(*equipment, request.amount)) {
                ZL_INFO("characters", "武器损坏：{}（攻击者稳定 ID {}）", defId,
                        value.IdOf(request.attacker).id);
            }
        }
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
    if (s.animalSystem != nullptr) {
        s.animalSystem->SetFocus(s.focus.x, s.focus.y);
    }
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
    s.blockFeedbacks.clear();
    s.hitFeedbacks.clear();
    s.spawnLodSystem.reset();
    s.actionPresentationSystem.reset();
    s.animationSystem.reset();
    s.deathSystem.reset();
    s.animalSystem.reset();
    s.combatSystem.reset();
    s.woundSystem.reset();
    s.painSystem.reset();
    s.infectionSystem.reset();
    s.fireSystem.reset();
    s.weatherSystem.reset();
    s.physicsSyncSystem.reset();
    s.movementSystem.reset();
    s.needsSystem.reset();
    s.inputSystem.reset();
    s.pathFollowSystem.reset();
    s.boidsSystem.reset();
    s.lodSystem.reset();
    s.zombieSpecialSystem.reset();
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
        if (m_impl->animalSystem != nullptr) {
            m_impl->animalSystem->SetFocus(gridX, gridY);
        }
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
    if (s.options.items != nullptr && s.options.gameTime != nullptr) {
        const double now = s.options.gameTime->TotalGameMinutes();
        if (now < s.lastSpoilGameMinute) {
            s.spoilAccumulatorMinutes = 0.0f;
        } else if (s.lastSpoilGameMinute >= 0.0 &&
                   now > s.lastSpoilGameMinute) {
            s.spoilAccumulatorMinutes +=
                static_cast<float>(now - s.lastSpoilGameMinute);
            if (s.spoilAccumulatorMinutes >= 1.0f) {
                const float step = std::floor(s.spoilAccumulatorMinutes);
                s.AdvanceFoodSpoilage(step);
                s.spoilAccumulatorMinutes -= step;
            }
        }
        s.lastSpoilGameMinute = now;
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
        if (auto* wounds = registry.try_get<ecs::Wounds>(entity); wounds != nullptr) {
            *wounds = ecs::Wounds{};
        }
        if (auto* pain = registry.try_get<ecs::Pain>(entity); pain != nullptr) {
            *pain = ecs::Pain{};
        }
        if (auto* infection = registry.try_get<ecs::Infection>(entity);
            infection != nullptr) {
            *infection = ecs::Infection{};
        }
        if (auto* climate = registry.try_get<ecs::Climate>(entity);
            climate != nullptr) {
            *climate = ecs::Climate{};
        }
        if (auto* food = registry.try_get<ecs::FoodState>(entity);
            food != nullptr) {
            *food = ecs::FoodState{};
        }
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

ecs::Entity WorldSimulation::SpawnZombieAt(float x, float y,
                                           std::uint64_t stableId) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return entt::null;
    }
    const int hour = CurrentZombieSpawnHour(s.options.gameTime);
    const ecs::ZombieVariantKind variant =
        PickZombieVariant(x, y, hour, ++s.zombieSpawnSerial);
    return SpawnZombie(*s.ecs, s.physics.get(), x, y, s.options.events, variant,
                       stableId, &s.combatRules);
}

ecs::Entity WorldSimulation::SpawnZombieAt(float x, float y,
                                           ecs::ZombieVariantKind variant,
                                           std::uint64_t stableId) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return entt::null;
    }
    return SpawnZombie(*s.ecs, s.physics.get(), x, y, s.options.events, variant,
                       stableId, &s.combatRules);
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
    constexpr float kSafeX = 70.0f;
    constexpr float kSafeY = 24.0f;
    const core::Config* config = s.options.config;
    const int demoZombies =
        config != nullptr ? std::clamp(config->Get<int>("spawner.demoZombies", 40), 0, 400)
                          : 40;
    const int demoHordes =
        config != nullptr ? std::clamp(config->Get<int>("spawner.demoHordes", 5), 0, 20)
                          : 5;
    const int demoZombiesPerHorde =
        config != nullptr
            ? std::clamp(config->Get<int>("spawner.demoZombiesPerHorde", 40), 0, 400)
            : 40;
    const float demoSafeRadius =
        config != nullptr
            ? std::max(0.0f,
                       static_cast<float>(config->Get<double>("spawner.demoSafeRadius", 0.0)))
            : 0.0f;
    const float demoSafeRadiusSq = demoSafeRadius * demoSafeRadius;
    const int hour = CurrentZombieSpawnHour(s.options.gameTime);
    int spawned = 0;
    for (int attempt = 0; attempt < demoZombies * 8 && spawned < demoZombies; ++attempt) {
        const float px = kBoxX0 + u01(rng) * (kBoxX1 - kBoxX0);
        const float py = kBoxY0 + u01(rng) * (kBoxY1 - kBoxY0);
        if (demoSafeRadiusSq > 0.0f) {
            const float dx = px - kSafeX;
            const float dy = py - kSafeY;
            if (dx * dx + dy * dy < demoSafeRadiusSq) {
                continue;  // 出生点安全区：避免开局被围
            }
        }
        const ecs::ZombieVariantKind variant =
            PickZombieVariant(px, py, hour, rng());
        SpawnZombie(*s.ecs, s.physics.get(), px, py, s.options.events, variant,
                    0, &s.combatRules);
        ++spawned;
    }
    ZL_INFO("sim", "生成 {} 个 AI 游荡者（区域 x[{:.0f},{:.0f}] y[{:.0f},{:.0f}]，"
                   "出生安全半径 {:.0f}）",
            spawned, kBoxX0, kBoxX1, kBoxY0, kBoxY1,
            static_cast<double>(demoSafeRadius));

    s.virtualHordes.Clear();
    for (int i = 0; i < demoHordes; ++i) {
        const float angle = static_cast<float>(i) *
                            (6.2831853f / static_cast<float>(std::max(1, demoHordes)));
        const float radius = 55.0f + static_cast<float>(i) * 5.0f;
        s.virtualHordes.AddHorde(70.0f + std::cos(angle) * radius,
                                 18.0f + std::sin(angle) * radius,
                                 demoZombiesPerHorde,
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
    if (s.animalSystem != nullptr) {
        s.animalSystem->Reset();
    }
    if (s.settlementNpcSystem != nullptr) {
        s.settlementNpcSystem->Reset();
    }
    std::vector<ecs::Entity> loaded;
    std::string entityError;
    if (!save::LoadEntities(snapshot, *s.ecs, *s.options.saveComponents, &loaded,
                            &entityError)) {
        return fail("读取实体失败：" + entityError);
    }
    if (!s.RestoreRuntimeComponents(loaded)) {
        return fail("恢复运行时组件失败");
    }
    if (s.settlementNpcSystem != nullptr) {
        s.ecs->Registry().view<SettlementState>().each(
            [&](const SettlementState& state) {
                s.settlementNpcSystem->ApplyState(state);
            });
    }
    if (s.animalSystem != nullptr) {
        s.animalSystem->LoadState(*s.ecs);
    }
    if (s.options.gameTime != nullptr) {
        s.options.gameTime->SetTotalGameMinutes(metadata.gameMinutes);
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

ai::ZombieSpecialSystem* WorldSimulation::ZombieSpecial() {
    return m_impl->zombieSpecialSystem.get();
}

characters::CombatSystem* WorldSimulation::Combat() {
    return m_impl->combatSystem.get();
}

std::vector<characters::CombatSystem::BlockFeedback>
WorldSimulation::TakeBlockFeedbacks() {
    std::vector<characters::CombatSystem::BlockFeedback> feedbacks;
    feedbacks.swap(m_impl->blockFeedbacks);
    return feedbacks;
}

std::vector<characters::CombatSystem::HitFeedback>
WorldSimulation::TakeHitFeedbacks() {
    std::vector<characters::CombatSystem::HitFeedback> feedbacks;
    feedbacks.swap(m_impl->hitFeedbacks);
    return feedbacks;
}

SettlementNpcSystem* WorldSimulation::SettlementNpcs() {
    return m_impl->settlementNpcSystem.get();
}

FireSystem* WorldSimulation::Fire() {
    return m_impl->fireSystem.get();
}

AnimalSystem* WorldSimulation::Animals() {
    return m_impl->animalSystem.get();
}

WeatherState WorldSimulation::CurrentWeather() const {
    return m_impl->SampleWeather();
}

void WorldSimulation::SetWeatherOverride(WeatherKind kind,
                                         float durationMinutes) {
    Impl& s = *m_impl;
    if (!s.weatherSystem || !(durationMinutes > 0.0f) ||
        !std::isfinite(durationMinutes)) {
        return;
    }
    const double now =
        s.options.gameTime != nullptr ? s.options.gameTime->TotalGameMinutes()
                                      : 0.0;
    s.weatherSystem->SetOverride(
        kind, now + static_cast<double>(durationMinutes));
    s.UpdateWeather();
}

void WorldSimulation::ClearWeatherOverride() {
    Impl& s = *m_impl;
    if (!s.weatherSystem) {
        return;
    }
    s.weatherSystem->ClearOverride();
    s.UpdateWeather();
}

}  // namespace zl::game
