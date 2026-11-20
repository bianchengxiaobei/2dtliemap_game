#include "game/WorldSimulation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <random>
#include <string>
#include <unordered_set>
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
#include "game/Companion.h"
#include "game/CompanionInventory.h"
#include "game/CompanionSystem.h"
#include "game/FoodSafety.h"
#include "game/FireSystem.h"
#include "game/GroundItems.h"
#include "game/HomesteadComponents.h"
#include "game/LootContainers.h"
#include "game/Objectives.h"
#include "game/RandomEvents.h"
#include "game/RegionEventDirector.h"
#include "game/RegionalSiege.h"
#include "game/StrategicDirector.h"
#include "game/StrategicPersistence.h"
#include "game/StrategicState.h"
#include "game/MissionComponents.h"
#include "game/PoiComponents.h"
#include "game/WeatherStatus.h"
#include "game/WorldMap.h"
#include "game/AnimalSystem.h"
#include "game/Animals.h"
#include "game/Poi.h"
#include "game/ProgressionRules.h"
#include "game/SettlementNpc.h"
#include "game/SettlementReputation.h"
#include "game/WorldEventDirector.h"
#include "game/WorldEventRuntime.h"
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
#include "save/SaveService.h"
#include "save/WorldManifest.h"
#include "world/Chunk.h"
#include "world/ChunkManager.h"
#include "world/RegionStreaming.h"
#include "world/World.h"
#include "world/WorldDefinition.h"
#include "world/WorldGraph.h"

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

ecs::ZombieVariantKind ParseWorldEventVariant(std::string_view target) {
    std::string value(target);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value.find("armored") != std::string::npos) {
        return ecs::ZombieVariantKind::Armored;
    }
    if (value.find("strong") != std::string::npos ||
        value.find("brute") != std::string::npos) {
        return ecs::ZombieVariantKind::Strong;
    }
    if (value.find("fast") != std::string::npos ||
        value.find("runner") != std::string::npos) {
        return ecs::ZombieVariantKind::Fast;
    }
    return ecs::ZombieVariantKind::Normal;
}

ecs::SkillKind ParseWorldEventSkill(std::string_view skill) {
    if (skill == "melee") {
        return ecs::SkillKind::Melee;
    }
    if (skill == "building") {
        return ecs::SkillKind::Building;
    }
    if (skill == "crafting") {
        return ecs::SkillKind::Crafting;
    }
    return ecs::SkillKind::Survival;
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
    std::vector<glm::vec2> focuses;

    std::unique_ptr<world::World> world;
    std::unique_ptr<world::ChunkManager> chunks;
    std::unique_ptr<world::WorldGraph> graph;
    std::unique_ptr<world::RegionStreaming> regions;
    std::unique_ptr<RegionEventDirector> regionEvents;
    std::unique_ptr<WorldEventDirector> worldEvents;
    WorldEventOutcomeLedger worldEventOutcomes;
    std::unordered_set<std::string> worldEventContentReady;
    std::unordered_set<std::string> worldEventContentWarned;
    std::unordered_set<std::uint64_t> worldEventKilled;
    ProgressionConfig progressionConfig = DefaultProgressionConfig();
    std::unique_ptr<StrategicState> strategic;
    std::unique_ptr<RegionalSiege> regionalSiege;
    RegionalSiegeConfig regionalSiegeConfig;
    RegionalSiegeConfig regionalReclaimConfig;
    std::int64_t strategicGameMinute = 0;
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
    std::unique_ptr<game::CompanionSystem> companionSystem;
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
        registry.emplace<ecs::StealthState>(entity, ecs::StealthState{});
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
            if (!registry.all_of<ecs::StealthState>(entity)) {
                registry.emplace<ecs::StealthState>(entity);
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
            if (registry.all_of<Companion>(entity)) {
                (void)PrepareLoadedCompanion(*ecs, entity, options.items);
                if (options.companions != nullptr) {
                    const auto* identity =
                        registry.try_get<Companion>(entity);
                    auto* state = registry.try_get<CompanionState>(entity);
                    const CompanionDef* def =
                        identity != nullptr
                            ? options.companions->Find(identity->defId)
                            : nullptr;
                    if (def != nullptr && state != nullptr) {
                        RefreshCompanionQuestState(*def, state);
                    }
                }
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
                    last.sourceId = 0;  // 失温：无击杀者
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
    s.progressionConfig = LoadProgressionConfig(config);
    const std::string mapDir =
        !s.options.mapDir.empty()
            ? s.options.mapDir
            : config.Get<std::string>("world.mapDir", "assets/maps/village_demo");
    world::MapLoader::Meta mapMeta;
    std::string mapError;
    world::WorldDefinition worldDefinition;
    bool hasWorldDefinition = false;
    if (!world::MapLoader::LoadMeta(mapDir, &mapMeta, &mapError)) {
        ZL_ERROR("world", "地图元数据加载失败（降级为空世界）: {}", mapError);
        mapMeta = world::MapLoader::Meta{};
        mapMeta.rootDir = mapDir;
        mapMeta.chunksX = 0;
        mapMeta.chunksY = 0;
    } else {
        ZL_INFO("world", "地图已加载: {}（{}×{} chunks，默认地面 {}）", mapMeta.name,
                mapMeta.chunksX, mapMeta.chunksY, mapMeta.defaultFloor);
        std::string worldError;
        const world::WorldDefinitionStatus worldStatus =
            world::LoadWorldDefinition(mapDir, &worldDefinition, &worldError);
        if (worldStatus == world::WorldDefinitionStatus::Invalid) {
            return fail("世界描述加载失败: " + worldError);
        }
        if (worldStatus == world::WorldDefinitionStatus::Loaded) {
            hasWorldDefinition = true;
            ZL_INFO("world", "世界描述已加载: {}（seed={}，generatorVersion={}，mapHash={}）",
                    worldDefinition.mapId, worldDefinition.seed,
                    worldDefinition.generatorVersion,
                    world::FormatMapHash(worldDefinition.mapHash));
        }
    }

    const int loadRadius = config.Get<int>("world.loadRadius", core::kChunkLoadRadius);
    world::StreamingConfig streamingConfig;
    streamingConfig.loadRadius = loadRadius;
    streamingConfig.residentRadius =
        std::max(loadRadius, config.Get<int>("world.residentRadius", loadRadius + 2));
    streamingConfig.maxLookaheadChunks =
        std::max(0, config.Get<int>("world.predictionMaxChunks", 2));
    streamingConfig.lookaheadSeconds = static_cast<float>(
        std::max(0.0, config.Get<double>("world.predictionSeconds", 0.6)));
    // Keep legacy maps permissive; finite-region maps use the explicit bounds.
    const bool enforceMapBounds =
        hasWorldDefinition &&
        config.Get<bool>("world.enforceMapBounds", true);

    s.world = std::make_unique<world::World>();
    s.chunks = std::make_unique<world::ChunkManager>(
        *s.world, std::move(mapMeta), *s.options.tasks, loadRadius,
        std::max(1, config.Get<int>("world.chunkRequestBudget", 4)),
        std::max(1, config.Get<int>("world.chunkAttachBudget", 8)),
        std::max(0.0, config.Get<double>("world.chunkAttachBudgetMs", 2.0)),
        streamingConfig.residentRadius, enforceMapBounds);
    s.chunks->SetPredictionConfig(streamingConfig.maxLookaheadChunks,
                                  streamingConfig.lookaheadSeconds);
    if (hasWorldDefinition) {
        s.graph = std::make_unique<world::WorldGraph>(worldDefinition);
        if (!s.graph->Valid()) {
            return fail("world.json 道路图无效");
        }
        const glm::vec2 routeStart = s.options.focus;
        const world::RoadRoute route =
            s.graph->PlanRoute(routeStart, glm::vec2(300.0f, 280.0f), true);
        ZL_INFO("world", "道路图: {} 节点/{} 边，出生点→checkpoint 车辆路线 {}，距离 {:.1f}",
                s.graph->Nodes().size(), s.graph->Edges().size(),
                route.outcome == world::RoadRoute::Outcome::Found ? "可达" : "不可达",
                route.totalDistance);
        s.regions = std::make_unique<world::RegionStreaming>(
            std::move(worldDefinition), streamingConfig);
        RegionEventConfig eventConfig;
        eventConfig.enabled =
            config.Get<bool>("world.regionEvents.enabled", true);
        eventConfig.durationSeconds = static_cast<float>(std::max(
            1.0, config.Get<double>("world.regionEvents.durationSeconds", 240.0)));
        eventConfig.contentRadius = static_cast<float>(std::max(
            0.0, config.Get<double>("world.regionEvents.contentRadius", 24.0)));
        s.regionEvents = std::make_unique<RegionEventDirector>(
            s.regions->Definition(), eventConfig);
        ZL_INFO("events", "区域事件导演已启用：{} 个区域，持续 {:.0f}s，近距 {:.0f} 格",
                s.regions->Definition().regions.size(),
                static_cast<double>(eventConfig.durationSeconds),
                static_cast<double>(eventConfig.contentRadius));

        if (s.options.worldEvents != nullptr &&
            s.options.worldEvents->enabled &&
            !s.regions->Definition().regions.empty()) {
            WorldEventDirectorConfig worldEventConfig;
            worldEventConfig.detailRadius = static_cast<float>(std::max(
                0.0, config.Get<double>("worldEvents.detailRadius", 32.0)));
            s.worldEvents = std::make_unique<WorldEventDirector>(
                *s.options.worldEvents, s.regions->Definition(),
                worldEventConfig);
            ZL_INFO("events",
                    "世界事件导演已启用：{} 个定义，{} 个区域",
                    s.options.worldEvents->defs.size(),
                    s.regions->Definition().regions.size());
        }

        if (s.options.strategic != nullptr && s.options.strategic->enabled) {
            std::string strategicError;
            if (!ValidateStrategicAgainstWorld(*s.options.strategic,
                                               s.regions->Definition(),
                                               &strategicError)) {
                return fail("战略配置与 world.json 不匹配：" + strategicError);
            }
            s.strategic = std::make_unique<StrategicState>();
            if (!s.strategic->Initialize(*s.options.strategic,
                                         &strategicError)) {
                s.strategic.reset();
                return fail("战略状态初始化失败：" + strategicError);
            }
            ZL_INFO("strategic",
                    "战略状态已初始化：{} 个区域，初始资源 人{} 补给{} 弹药{} 燃料{}",
                    s.strategic->regions.size(), s.strategic->resources.people,
                    s.strategic->resources.supplies,
                    s.strategic->resources.ammo, s.strategic->resources.fuel);
        }
    }

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
                s.options.saveManager != nullptr && chunk.IsDirty()) {
                std::string saveError;
                save::ChunkDiff diff;
                if (!save::CaptureChunkDiff(chunk, &diff, &saveError)) {
                    ZL_WARN("save", "chunk ({},{}) 卸载快照失败：{}", cx, cy, saveError);
                } else if (s.options.saveService != nullptr) {
                    if (!s.options.saveService->RequestChunk(
                            *s.options.activeSaveId, std::move(diff), &saveError)) {
                        ZL_WARN("save", "chunk ({},{}) 提交流式写入失败：{}", cx, cy,
                                saveError);
                    }
                } else if (!s.options.saveManager->SaveChunkDiff(
                               *s.options.activeSaveId, diff, &saveError)) {
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
    s.combatSystem->SetDamageAppliedCallback(
        [&s](const characters::CombatSystem::DamageApplied& applied) {
            if (s.options.events == nullptr) {
                return;
            }
            core::DamageEvent event;
            event.targetId = applied.targetId;
            event.sourceId = applied.sourceId;
            event.amount = applied.amount;
            event.damageType = ecs::DamageTypeName(applied.type);
            event.cause =
                ecs::DamageCauseName(ecs::DamageCauseFromType(applied.type));
            event.remainingHp = applied.remainingHp;
            event.lethal = applied.lethal;
            event.blocked = applied.blocked;
            s.options.events->EmitQueued(core::kOnDamage, std::move(event));
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
    if (s.graph != nullptr) {
        s.spawnLodSystem->SetDensityScale(
            s.graph->DensityScaleAt(s.focus.x, s.focus.y));
    }

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
    if (s.options.pois != nullptr && s.options.pois->enabled) {
        const ecs::Entity poiWorld =
            s.ecs->CreateWithStableId(ecs::kPoiWorldProgressStableId);
        if (poiWorld != ecs::EcsWorld::kNull) {
            s.ecs->Registry().emplace<ecs::PoiWorldProgress>(poiWorld);
        }
    }
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
                if (auto* companion =
                        registry.try_get<CompanionState>(target);
                    companion != nullptr) {
                    equipment = &companion->equipment;
                }
            }
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
    const auto applyNpcDamage = [&s](ecs::Entity source, ecs::Entity target,
                                     float damage, ecs::DamageType type) {
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
    };
    s.settlementNpcSystem->SetDamageCallback(applyNpcDamage);
    game::CompanionSystem::Options companionOptions;
    if (s.options.config != nullptr) {
        companionOptions =
            game::LoadCompanionSystemOptions(*s.options.config);
    }
    s.companionSystem = std::make_unique<game::CompanionSystem>(
        [&s](int wx, int wy) { return !s.IsBlocked(wx, wy); },
        applyNpcDamage, companionOptions);
    s.companionSystem->SetItemRegistry(s.options.items);
    s.companionSystem->SetCompanionConfig(s.options.companions);
    s.companionSystem->SetTrapDefs(s.options.traps);
    if (s.options.config != nullptr && s.options.strategic != nullptr &&
        s.options.strategic->enabled && s.options.settlements != nullptr) {
        RegionalSiegeConfig siege;
        siege.enabled =
            s.options.config->Get<bool>("strategic.siege.enabled", true);
        siege.warmupSeconds = static_cast<float>(
            s.options.config->Get<double>("strategic.siege.warmupSeconds",
                                          10.0));
        siege.waveIntervalSeconds = static_cast<float>(
            s.options.config->Get<double>("strategic.siege.waveIntervalSeconds",
                                          25.0));
        siege.waves =
            s.options.config->Get<std::vector<int>>("strategic.siege.waves",
                                                    {4, 6, 8});
        siege.maxAlive =
            s.options.config->Get<int>("strategic.siege.maxAlive", 12);
        siege.maxWaveSize =
            s.options.config->Get<int>("strategic.siege.maxWaveSize", 12);
        siege.breachRadius = static_cast<float>(
            s.options.config->Get<double>("strategic.siege.breachRadius",
                                          6.0));
        siege.lossDelaySeconds = static_cast<float>(
            s.options.config->Get<double>("strategic.siege.lossDelaySeconds",
                                          8.0));
        s.regionalSiegeConfig = siege;

        RegionalSiegeConfig reclaim;
        reclaim.enabled =
            s.options.config->Get<bool>("strategic.reclaim.enabled", true);
        reclaim.warmupSeconds = static_cast<float>(
            s.options.config->Get<double>("strategic.reclaim.warmupSeconds",
                                          5.0));
        reclaim.waveIntervalSeconds = static_cast<float>(
            s.options.config->Get<double>(
                "strategic.reclaim.waveIntervalSeconds", 12.0));
        reclaim.waves =
            s.options.config->Get<std::vector<int>>("strategic.reclaim.waves",
                                                    {4, 6});
        reclaim.maxAlive =
            s.options.config->Get<int>("strategic.reclaim.maxAlive", 8);
        reclaim.maxWaveSize =
            s.options.config->Get<int>("strategic.reclaim.maxWaveSize", 8);
        reclaim.breachRadius = static_cast<float>(
            s.options.config->Get<double>("strategic.reclaim.breachRadius",
                                          4.0));
        reclaim.lossDelaySeconds = static_cast<float>(
            s.options.config->Get<double>(
                "strategic.reclaim.lossDelaySeconds", 6.0));
        s.regionalReclaimConfig = reclaim;

        RegionalSiegeCallbacks callbacks;
        callbacks.beginDefense = [&s](std::string_view regionId) {
            return s.settlementNpcSystem != nullptr &&
                   s.settlementNpcSystem->BeginExternalDefense(*s.ecs,
                                                               regionId);
        };
        callbacks.beginReclaim = [&s](std::string_view regionId) {
            return s.settlementNpcSystem != nullptr &&
                   s.settlementNpcSystem->BeginExternalReclaim(*s.ecs,
                                                               regionId);
        };
        callbacks.spawnWave =
            [&s](std::string_view regionId, int count, int waveIndex,
                 ContestedFaction faction) {
                if (s.settlementNpcSystem == nullptr) {
                    return 0;
                }
                return s.settlementNpcSystem->SpawnExternalWave(
                    *s.ecs, regionId, count, waveIndex, faction);
            };
        callbacks.aliveEnemies = [&s](std::string_view regionId) {
            return s.settlementNpcSystem != nullptr
                       ? s.settlementNpcSystem->LiveRaiders(*s.ecs, regionId)
                       : 0;
        };
        callbacks.aliveGuards = [&s](std::string_view regionId) {
            return s.settlementNpcSystem != nullptr
                       ? s.settlementNpcSystem->LiveGuards(*s.ecs, regionId)
                       : 0;
        };
        callbacks.raidersWithin =
            [&s](std::string_view regionId, float x, float y, float radius) {
                return s.settlementNpcSystem != nullptr
                           ? s.settlementNpcSystem->RaidersWithin(
                                 *s.ecs, regionId, x, y, radius)
                           : 0;
            };
        callbacks.endDefense =
            [&s](std::string_view regionId, bool fallen,
                 std::string_view reason) {
                if (s.settlementNpcSystem != nullptr) {
                    s.settlementNpcSystem->EndExternalDefense(
                        *s.ecs, regionId, fallen, reason);
                }
            };
        callbacks.endReclaim =
            [&s](std::string_view regionId, bool held,
                 std::string_view reason) {
                if (s.settlementNpcSystem != nullptr) {
                    s.settlementNpcSystem->EndExternalReclaim(
                        *s.ecs, regionId, held, reason);
                }
            };
        callbacks.onResult = [&s](const RegionalSiegeResult& result) {
            if (s.strategic == nullptr || s.options.strategic == nullptr) {
                return;
            }
            StrategicDetailedResolution resolution;
            resolution.regionId = result.regionId;
            resolution.attackMinute = result.attackMinute;
            resolution.resolvedMinute = result.resolvedMinute;
            resolution.held = result.held;
            resolution.breached = result.breached;
            resolution.rating = result.rating;
            std::string error;
            const bool resolved =
                result.reclaim
                    ? ResolveStrategicReclaim(
                          *s.strategic, *s.options.strategic,
                          StrategicReclaimResolution{
                              result.regionId, result.attackMinute,
                              result.resolvedMinute, result.held,
                              result.breached, result.rating},
                          &error)
                    : ResolveStrategicSiege(*s.strategic,
                                            *s.options.strategic, resolution,
                                            &error);
            if (!resolved) {
                ZL_ERROR("strategic", "详细围攻结果回写失败：{}", error);
                return;
            }
            if (!result.reclaim && !result.held &&
                s.settlementNpcSystem != nullptr &&
                !result.settlementId.empty()) {
                s.settlementNpcSystem->MarkFallen(*s.ecs,
                                                  result.settlementId);
            }
            ZL_INFO(
                "strategic",
                "详细{}结束：{} {}，评级 {}，波次 {}/{}，击杀 {}，守卫 {}/{}，破口 {}，残敌 {}",
                result.reclaim ? "反攻" : "围攻", result.regionId,
                result.held ? "守住" : "失守", result.rating,
                result.wavesSpawned, result.waveCount, result.kills,
                result.guardsEnd, result.guardsStart,
                result.breached
                    ? BaseSiegeDirectionName(result.breachDirection)
                    : "无",
                result.raidersLeft);
        };
        s.regionalSiege = std::make_unique<RegionalSiege>();
        s.regionalSiege->Configure(siege, std::move(callbacks));
        s.regionalSiege->ConfigureReclaim(reclaim);
    }

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
        std::vector<game::FireDamageCredit>* credits =
            s.fireSystem->MutableDamageCredits();
        if (s.combatSystem != nullptr) {
            (void)game::ApplyBurningZombieDamage(
                value, *s.combatSystem, params, dt, credits);
        }
        s.fireDamageAccumulator += dt;
        if (s.combatSystem != nullptr &&
            s.fireDamageAccumulator >= params.fireDamageIntervalSeconds) {
            (void)game::ApplyFireDamage(
                value, *s.combatSystem, params, s.fireDamageAccumulator,
                credits);
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
    systems.AddSystem("CompanionSystem", [&s](ecs::EcsWorld& value, float dt) {
        if (s.companionSystem != nullptr) {
            s.companionSystem->Update(value, dt);
        }
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
        s.spawnLodSystem->SetDensityScale(
            s.graph != nullptr ? s.graph->DensityScaleAt(s.focus.x, s.focus.y)
                               : 1.0f);
        s.spawnLodSystem->Update(value, dt);
    });
    systems.AddSystem("RegionEventSystem", [this, &s](ecs::EcsWorld&, float dt) {
        if (s.regionEvents == nullptr || s.regions == nullptr) {
            return;
        }
        int day = 1;
        if (s.options.gameTime != nullptr) {
            day = 1 + static_cast<int>(
                          s.options.gameTime->TotalGameMinutes() / 1440.0);
        }
        s.regionEvents->AdvanceDay(day);
        s.regionEvents->UpdateFocuses(
            dt, s.focuses,
            [&s](const std::string& regionId) {
                return s.regions->PhaseOf(regionId);
            },
            [this](const RegionEvent& event) {
                return SpawnRegionEventContent(event);
            });
    });
    systems.AddSystem("WorldEventSystem", [this, &s](ecs::EcsWorld& value, float) {
        if (s.worldEvents == nullptr || s.options.gameTime == nullptr) {
            return;
        }
        s.worldEvents->Update(s.options.gameTime->TotalGameMinutes(),
                              s.focuses);
        const WorldEventConfig* config = s.options.worldEvents;
        if (config != nullptr) {
            for (const WorldEventInstance& event : s.worldEvents->Events()) {
                if (event.status != WorldEventStatus::Active ||
                    !event.detail ||
                    s.worldEventContentReady.contains(event.id)) {
                    continue;
                }
                const WorldEventDef* def = config->Find(event.defId);
                if (def == nullptr) {
                    continue;
                }
                const WorldEventSpawnPlan plan =
                    BuildWorldEventSpawnPlan(*def, event);
                WorldEventSpawnCallbacks callbacks;
                callbacks.spawn =
                    [this, &s, &event](const WorldEventSpawnRequest& request) {
                        const int gridX =
                            static_cast<int>(std::floor(request.x));
                        const int gridY =
                            static_cast<int>(std::floor(request.y));
                        int wx = 0;
                        int wy = 0;
                        if (!ai::FindNearestWalkable(
                                gridX, gridY, 4,
                                [&s](int x, int y) { return !s.IsBlocked(x, y); },
                                &wx, &wy)) {
                            return false;
                        }
                        const float px = static_cast<float>(wx) + 0.5f;
                        const float py = static_cast<float>(wy) + 0.5f;
                        if (request.kind == WorldEventSpawnKind::Zombie) {
                            const ecs::Entity entity = SpawnZombieAt(
                                px, py,
                                ParseWorldEventVariant(request.target),
                                request.stableId);
                            if (entity == entt::null) {
                                return false;
                            }
                            s.ecs->Registry()
                                .emplace_or_replace<ecs::WorldEventActor>(
                                    entity,
                                    ecs::WorldEventActor{
                                        request.eventId, request.target,
                                        request.index});
                            return true;
                        }
                        if (s.options.lootTables == nullptr) {
                            return false;
                        }
                        const std::string tableId =
                            request.target.empty() ? "event_supply_crate"
                                                   : request.target;
                        ecs::Entity entity = game::SpawnLootContainer(
                            *s.ecs, *s.options.lootTables, tableId, px, py, 0,
                            "object.crate", request.stableId);
                        if (entity == entt::null &&
                            tableId != "event_supply_crate") {
                            entity = game::SpawnLootContainer(
                                *s.ecs, *s.options.lootTables,
                                "event_supply_crate", px, py, 0,
                                "object.crate", request.stableId);
                        }
                        if (entity == entt::null) {
                            return false;
                        }
                        s.ecs->Registry()
                            .emplace_or_replace<ecs::WorldEventContainer>(
                                entity,
                                ecs::WorldEventContainer{request.eventId,
                                                         request.index});
                        return true;
                    };
                WorldEventSpawnResult result;
                if (!EnsureWorldEventContent(plan, callbacks, &result)) {
                    if (s.worldEventContentWarned.insert(event.id).second) {
                        ZL_WARN("events",
                                "世界事件内容生成未完成：{}（{}/{}），将重试",
                                event.name, result.ready, result.total);
                    }
                    continue;
                }
                s.worldEventContentReady.insert(event.id);
                s.worldEventContentWarned.erase(event.id);
                s.worldEvents->MarkContentSpawned(event.id);
                if (result.total > 0) {
                    ZL_INFO("events", "世界事件内容：{}（{}）生成 {} 个，位置 ({:.1f},{:.1f})",
                            event.name, event.defId, result.ready,
                            static_cast<double>(event.x),
                            static_cast<double>(event.y));
                }
            }
        }
        value.Registry()
            .view<ecs::WorldEventActor, ecs::Health>()
            .each([&s, &value](const ecs::Entity entity,
                               const ecs::WorldEventActor& actor,
                               const ecs::Health& health) {
                if (health.current > 0.0f || !value.Alive(entity)) {
                    return;
                }
                const std::uint64_t stableId = value.IdOf(entity).id;
                if (stableId == 0 ||
                    !s.worldEventKilled.insert(stableId).second) {
                    return;
                }
                const WorldEventInstance* event =
                    s.worldEvents->Find(actor.eventId);
                if (event == nullptr ||
                    event->status != WorldEventStatus::Active) {
                    return;
                }
                if (event->stageKind != WorldEventStageKind::Kill &&
                    event->stageKind != WorldEventStageKind::Defend) {
                    return;
                }
                std::string error;
                if (!s.worldEvents->ReportStageProgress(
                        actor.eventId, event->stageKind, actor.target, 1,
                        &error)) {
                    ZL_DEBUG("events", "世界事件击杀推进失败：{}", error);
                }
            });
    });
    systems.AddSystem("AnimationSystem", [&s](ecs::EcsWorld& value, float dt) {
        s.animationSystem->Update(value, dt);
    });
    systems.AddSystem("ActionPresentationSystem",
                      [&s](ecs::EcsWorld& value, float dt) {
                          s.actionPresentationSystem->Update(value, dt);
                      });

    s.focus = s.options.focus;
    s.focuses = {s.focus};
    if (s.animalSystem != nullptr) {
        s.animalSystem->SetFocus(s.focus.x, s.focus.y);
    }
    if (s.regions != nullptr) {
        s.regions->SetFocuses(s.focuses);
    }
    s.initialized = true;
    ZL_INFO("sim", "模拟已就绪（无窗口可运行）：{} 个系统，世界 {}×{} chunks",
            systems.Count(), loadedMeta.chunksX, loadedMeta.chunksY);
    return true;
}

void WorldSimulation::SetTrapDefs(const TrapDefsConfig* traps) {
    if (m_impl != nullptr && m_impl->companionSystem != nullptr) {
        m_impl->companionSystem->SetTrapDefs(traps);
    }
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
    s.graph.reset();
    s.regionEvents.reset();
    s.worldEvents.reset();
    s.worldEventOutcomes.Reset();
    s.worldEventContentReady.clear();
    s.worldEventContentWarned.clear();
    s.worldEventKilled.clear();
    s.regions.reset();
    s.chunks.reset();
    s.world.reset();
    s.ecs.reset();
    s.initialized = false;
    ZL_INFO("sim", "模拟已关闭");
}

void WorldSimulation::SetFocus(float gridX, float gridY) {
    Impl& s = *m_impl;
    if (!std::isfinite(gridX) || !std::isfinite(gridY)) {
        return;
    }
    SetFocuses({glm::vec2(gridX, gridY)});
    if (s.regions != nullptr) {
        glm::vec2 velocity{0.0f, 0.0f};
        const ecs::Entity player = Player();
        if (s.ecs != nullptr && player != entt::null && s.ecs->Alive(player)) {
            if (const auto* value =
                    s.ecs->Registry().try_get<ecs::Velocity>(player);
                value != nullptr) {
                velocity = glm::vec2(value->x, value->y);
            }
        }
        s.regions->SetFocus(gridX, gridY, velocity.x, velocity.y);
    }
}

void WorldSimulation::SetFocuses(const std::vector<glm::vec2>& focuses) {
    Impl& s = *m_impl;
    std::vector<glm::vec2> valid;
    valid.reserve(focuses.size());
    for (const glm::vec2& focus : focuses) {
        if (std::isfinite(focus.x) && std::isfinite(focus.y)) {
            valid.push_back(focus);
        }
    }
    if (valid.empty()) {
        return;
    }
    s.focuses = std::move(valid);
    s.focus = s.focuses.front();
    if (s.animalSystem != nullptr) {
        s.animalSystem->SetFocus(s.focus.x, s.focus.y);
    }
    if (s.regions != nullptr) {
        s.regions->SetFocuses(s.focuses);
    }
    if (s.spawnLodSystem != nullptr) {
        s.spawnLodSystem->SetFocus(s.focus.x, s.focus.y);
    }
    if (s.graph != nullptr && s.spawnLodSystem != nullptr) {
        s.spawnLodSystem->SetDensityScale(
            s.graph->DensityScaleAt(s.focus.x, s.focus.y));
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
    if (s.focuses.size() > 1) {
        StreamAtFocuses(s.focuses);
        return;
    }
    glm::vec2 velocity{0.0f, 0.0f};
    const ecs::Entity player = Player();
    if (s.ecs != nullptr && player != entt::null && s.ecs->Alive(player)) {
        if (const auto* value = s.ecs->Registry().try_get<ecs::Velocity>(player);
            value != nullptr) {
            velocity = glm::vec2(value->x, value->y);
        }
    }
    StreamAt(s.focus.x, s.focus.y, velocity.x, velocity.y);
}

void WorldSimulation::StreamAt(float focusX, float focusY, float velocityX,
                               float velocityY) {
    Impl& s = *m_impl;
    if (!s.initialized || s.chunks == nullptr || !std::isfinite(focusX) ||
        !std::isfinite(focusY)) {
        return;
    }
    s.focus = glm::vec2(focusX, focusY);
    s.focuses = {s.focus};
    if (s.regions != nullptr) {
        s.regions->SetFocus(focusX, focusY, velocityX, velocityY);
        s.chunks->SetPrediction(velocityX, velocityY);
    } else {
        // Legacy maps keep the pre-M31 camera-centered window.
        s.chunks->SetPrediction(0.0f, 0.0f);
    }
    s.chunks->Update(focusX, focusY);
    s.chunks->ProcessResults();
}

void WorldSimulation::StreamAtFocuses(
    const std::vector<glm::vec2>& focuses) {
    Impl& s = *m_impl;
    if (!s.initialized || s.chunks == nullptr) {
        return;
    }
    SetFocuses(focuses);
    if (s.focuses.empty()) {
        return;
    }
    s.chunks->SetPrediction(0.0f, 0.0f);
    std::vector<world::ChunkManager::FocusPoint> points;
    points.reserve(s.focuses.size());
    for (const glm::vec2& focus : s.focuses) {
        points.push_back(world::ChunkManager::FocusPoint{focus.x, focus.y});
    }
    s.chunks->UpdateFocuses(points);
    s.chunks->ProcessResults();
}

world::RegionStreaming* WorldSimulation::Regions() {
    return m_impl->regions.get();
}

world::WorldGraph* WorldSimulation::Graph() {
    return m_impl->graph.get();
}

RegionEventDirector* WorldSimulation::RegionEvents() {
    return m_impl->regionEvents.get();
}

WorldEventDirector* WorldSimulation::WorldEvents() {
    return m_impl->worldEvents.get();
}

const WorldEventDirector* WorldSimulation::WorldEvents() const {
    return m_impl->worldEvents.get();
}

bool WorldSimulation::ChooseWorldEvent(std::string_view eventId,
                                       std::string_view choiceId,
                                       ecs::Entity player,
                                       WorldEventSettlement* out,
                                       std::string* error) {
    if (out != nullptr) {
        *out = {};
    }
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    Impl& s = *m_impl;
    if (s.worldEvents == nullptr || s.options.worldEvents == nullptr) {
        return fail("世界事件不可用");
    }
    if (s.ecs == nullptr || player == entt::null || !s.ecs->Alive(player)) {
        return fail("世界事件玩家无效");
    }
    const WorldEventInstance* event = s.worldEvents->Find(eventId);
    if (event == nullptr) {
        return fail("世界事件不存在");
    }
    if (event->status != WorldEventStatus::Active) {
        return fail("世界事件未进行中");
    }
    const WorldEventDef* def = s.options.worldEvents->Find(event->defId);
    if (def == nullptr) {
        return fail("世界事件定义缺失");
    }
    const auto choice = std::find_if(
        def->choices.begin(), def->choices.end(),
        [choiceId](const WorldEventChoiceDef& value) {
            return value.id == choiceId;
        });
    if (choice == def->choices.end()) {
        return fail("世界事件选项无效");
    }
    if (!s.worldEvents->Choose(eventId, choiceId, error)) {
        return false;
    }
    return SettleWorldEvent(eventId, player, out, error);
}

bool WorldSimulation::SettleWorldEvent(std::string_view eventId,
                                       ecs::Entity player,
                                       WorldEventSettlement* out,
                                       std::string* error) {
    if (out != nullptr) {
        *out = {};
    }
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    Impl& s = *m_impl;
    if (s.worldEvents == nullptr || s.options.worldEvents == nullptr) {
        return fail("世界事件不可用");
    }
    if (s.ecs == nullptr || player == entt::null || !s.ecs->Alive(player)) {
        return fail("世界事件玩家无效");
    }
    const WorldEventInstance* event = s.worldEvents->Find(eventId);
    if (event == nullptr) {
        return fail("世界事件不存在");
    }
    const WorldEventDef* def = s.options.worldEvents->Find(event->defId);
    if (def == nullptr) {
        return fail("世界事件定义缺失");
    }
    WorldEventOutcome outcome;
    std::string outcomeError;
    if (!BuildWorldEventOutcome(*def, *event, &outcome, &outcomeError)) {
        return fail(outcomeError.empty() ? "世界事件结果无效"
                                         : outcomeError);
    }
    if (out != nullptr) {
        out->choiceId = outcome.choiceId;
    }
    const std::uint64_t playerId = s.ecs->IdOf(player).id;
    if (playerId == 0) {
        return fail("世界事件玩家 StableId 无效");
    }
    if (s.worldEventOutcomes.Contains(event->id, playerId)) {
        if (out != nullptr) {
            out->alreadyApplied = true;
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }
    if (!s.worldEventOutcomes.Claim(event->id, playerId)) {
        return fail("世界事件结果重复或超出上限");
    }

    const std::string key = WorldEventOutcomeKey(event->id, playerId);
    if (outcome.reputation != 0 && s.options.settlements != nullptr &&
        s.options.settlements->reputation.enabled) {
        const SettlementDef* settlement = FindSettlementContaining(
            *s.options.settlements, event->x, event->y);
        if (settlement != nullptr) {
            const SettlementReputationChange change =
                AwardSettlementReputationDelta(
                    *s.ecs, player, s.options.settlements->reputation,
                    settlement->id, outcome.reputation, key,
                    settlement->startingReputation);
            if (out != nullptr) {
                out->reputationDelta = change.delta;
                out->reputationScore = change.current;
            }
        }
    }

    if (outcome.pressure != 0 && s.strategic != nullptr &&
        s.options.strategic != nullptr) {
        std::int64_t gameMinute = s.strategicGameMinute;
        if (s.options.gameTime != nullptr) {
            gameMinute = std::max<std::int64_t>(
                0, static_cast<std::int64_t>(
                       std::floor(s.options.gameTime->TotalGameMinutes())));
        }
        StrategicWorldEventResult pressure;
        if (!ApplyStrategicWorldEventEffect(
                *s.strategic, *s.options.strategic, event->regionId,
                outcome.pressure, event->id, gameMinute, &pressure, error)) {
            return false;
        }
        if (out != nullptr) {
            out->pressureDelta = pressure.applied ? outcome.pressure : 0;
            out->pressureBefore = pressure.pressureBefore;
            out->pressureAfter = pressure.pressureAfter;
        }
    }

    if (outcome.xp > 0.0f && s.progressionConfig.enabled && s.ecs != nullptr) {
        auto* progression =
            s.ecs->Registry().try_get<ecs::Progression>(player);
        if (progression != nullptr) {
            const ecs::SkillKind skill = ParseWorldEventSkill(outcome.skill);
            (void)AwardXp(*progression, skill, outcome.xp,
                           s.progressionConfig);
            RefreshDerived(*progression, s.progressionConfig);
            if (out != nullptr) {
                out->xp = outcome.xp;
            }
        }
    }

    if (out != nullptr) {
        out->item = outcome.item;
        out->itemCount = outcome.itemCount;
    }
    if (!outcome.item.empty() && s.options.items != nullptr) {
        const items::ItemDef* itemDef =
            s.options.items->Find(outcome.item);
        if (itemDef != nullptr) {
            ecs::ItemInstance item{outcome.item,
                                   std::max(1, outcome.itemCount), 1.0f};
            auto* container =
                s.ecs->Registry().try_get<ecs::Container>(player);
            const auto* position =
                s.ecs->Registry().try_get<ecs::Position>(player);
            const int added =
                container != nullptr
                    ? items::AddItem(*container, *s.options.items, item)
                    : 0;
            if (added < item.count && position != nullptr) {
                ecs::ItemInstance remainder = item;
                remainder.count -= added;
                if (SpawnGroundItem(*s.ecs, *s.options.items, remainder,
                                    position->x, position->y,
                                    position->z) != entt::null &&
                    out != nullptr) {
                    out->itemDropped = true;
                }
            }
        }
    }

    if (out != nullptr) {
        out->applied = true;
    }
    if (error != nullptr) {
        error->clear();
    }
    ZL_INFO("events",
            "世界事件结果：{}（{}）选择={}，声望 {:+d}，压力 {:+d}，经验 {:.0f}，物品 {}{}",
            def->name, def->id, outcome.choiceId, outcome.reputation,
            outcome.pressure, static_cast<double>(outcome.xp),
            outcome.item.empty() ? "无" : outcome.item,
            out != nullptr && out->itemDropped ? "（背包已满，部分掉落）" : "");
    return true;
}

StrategicState* WorldSimulation::Strategic() {
    return m_impl->strategic.get();
}

const StrategicState* WorldSimulation::Strategic() const {
    return m_impl->strategic.get();
}

const StrategicConfig* WorldSimulation::StrategicSettings() const {
    return m_impl->options.strategic;
}

bool WorldSimulation::TickStrategic(const StrategicTickInput& input,
                                    StrategicTickReport* report,
                                    std::string* error) {
    m_impl->strategicGameMinute = input.gameMinute;
    if (m_impl->strategic == nullptr || m_impl->options.strategic == nullptr) {
        return true;
    }
    StrategicTickReport local;
    StrategicTickReport* out = report != nullptr ? report : &local;
    if (!game::AdvanceStrategic(*m_impl->strategic,
                                *m_impl->options.strategic, input, out,
                                error)) {
        return false;
    }
    const auto settlementForRegion =
        [this](std::string_view regionId) -> std::string {
        Impl& s = *m_impl;
        if (s.options.settlements == nullptr || s.options.strategic == nullptr) {
            return {};
        }
        const StrategicRegionConfig* def =
            s.options.strategic->Find(regionId);
        if (def == nullptr || !def->hasAnchor) {
            return {};
        }
        const SettlementDef* settlement =
            FindSettlementContaining(*s.options.settlements, def->anchorX,
                                     def->anchorY);
        return settlement != nullptr ? settlement->id : std::string{};
    };
    for (const std::string& regionId : out->detailedRegions) {
        if (m_impl->regionalSiege != nullptr &&
            m_impl->regionalSiege->Active() &&
            std::string_view(m_impl->regionalSiege->RegionId()) == regionId) {
            continue;
        }
        if (!StartRegionalSiege(regionId)) {
            ZL_WARN("strategic", "战略区域未能启动详细围攻：{}", regionId);
        }
    }
    for (const std::string& regionId : out->recoveryRegions) {
        if (m_impl->regionalSiege != nullptr &&
            m_impl->regionalSiege->Active() &&
            std::string_view(m_impl->regionalSiege->RegionId()) == regionId) {
            continue;
        }
        if (!StartRegionalReclaim(regionId)) {
            ZL_WARN("strategic", "战略区域未能启动反攻波次：{}", regionId);
        }
    }
    for (const std::string& regionId : out->completedRecoveries) {
        const std::string settlementId = settlementForRegion(regionId);
        if (!settlementId.empty() && m_impl->settlementNpcSystem != nullptr) {
            m_impl->settlementNpcSystem->RestoreSettlement(
                *m_impl->ecs, settlementId);
        }
        ZL_INFO("strategic", "区域恢复 Human：{}", regionId);
    }
    for (const StrategicEconomyInfo& economy : out->economy) {
        ZL_INFO("strategic",
                "战略经济：第 {} 日（{} 天）产出补给 {}，消耗 {}，短缺 {}，"
                "区域补给恢复 {}，工事修复 {}，交易弹药 {} 燃料 {}",
                economy.day, economy.days, economy.producedSupplies,
                economy.consumedSupplies, economy.shortageSupplies,
                economy.restoredRegionSupply, economy.repairedFortification,
                economy.tradedAmmo, economy.tradedFuel);
    }
    for (const StrategicWarningInfo& warning : out->warnings) {
        ZL_INFO("strategic",
                "战略预警：{} 于 {} 分钟后受袭，防御 {}（守军 {} + 工事 {} + "
                "补给 {}）vs 敌约 {}/{}（情报 {}，预计 {}）",
                warning.regionId, warning.warningMinutes, warning.defenseScore,
                warning.defenseFromGarrison, warning.defenseFromFortification,
                warning.defenseFromSupply, warning.reportedEnemyStrength,
                warning.enemyStrength, warning.intelLevel,
                warning.reportedAttackMinute);
    }
    for (const StrategicResolutionInfo& resolution : out->resolutions) {
        if (!resolution.held && !resolution.coreProtected &&
            m_impl->settlementNpcSystem != nullptr) {
            const std::string settlementId =
                settlementForRegion(resolution.regionId);
            if (!settlementId.empty()) {
                m_impl->settlementNpcSystem->MarkFallen(
                    *m_impl->ecs, settlementId);
            }
        }
        ZL_INFO("strategic",
                "战略自动结算：{} {}，防御 {}（守军 {} + 工事 {} + 补给 {}）"
                "vs 敌军 {}；守军 {}/{}，人口 {}/{}，工事 {}/{}，设施损伤 "
                "{}/{}，区域补给 {}/{}{}",
                resolution.regionId,
                resolution.held ? "守住" : "失守",
                resolution.defenseScore, resolution.defenseFromGarrison,
                resolution.defenseFromFortification,
                resolution.defenseFromSupply, resolution.enemyStrength,
                resolution.garrisonBefore, resolution.garrisonAfter,
                resolution.populationBefore, resolution.populationAfter,
                resolution.fortificationBefore, resolution.fortificationAfter,
                resolution.facilityDamageBefore,
                resolution.facilityDamageAfter, resolution.regionSupplyBefore,
                resolution.regionSupplyAfter,
                resolution.coreProtected ? "（核心区转争夺）" : "");
    }
    return true;
}

bool WorldSimulation::ClearRegionalRecoveryCore(
    std::string_view regionId, std::string* error) {
    Impl& s = *m_impl;
    if (s.strategic == nullptr || s.options.strategic == nullptr) {
        if (error != nullptr) {
            *error = "战略状态不可用";
        }
        return false;
    }
    if (!game::ClearStrategicRecoveryCore(
            *s.strategic, *s.options.strategic, regionId, s.strategicGameMinute,
            error)) {
        return false;
    }
    ZL_INFO("strategic", "感染核心已清理：{}，下一步修复设施", regionId);
    return true;
}

bool WorldSimulation::RepairRegionalRecoveryFacility(
    std::string_view regionId, std::string* error) {
    Impl& s = *m_impl;
    if (s.strategic == nullptr || s.options.strategic == nullptr) {
        if (error != nullptr) {
            *error = "战略状态不可用";
        }
        return false;
    }
    if (!game::RepairStrategicRecoveryFacility(
            *s.strategic, *s.options.strategic, regionId, s.strategicGameMinute,
            error)) {
        return false;
    }
    ZL_INFO("strategic", "区域设施已修复：{}，准备反攻波次", regionId);
    return true;
}

bool WorldSimulation::GrantStrategicMissionReward(std::string_view missionId,
                                                  std::string* error) {
    Impl& s = *m_impl;
    if (s.strategic == nullptr || s.options.strategic == nullptr) {
        if (error != nullptr) {
            *error = "战略状态不可用";
        }
        return false;
    }
    if (!game::ApplyStrategicMissionReward(
            *s.strategic, *s.options.strategic, missionId, s.strategicGameMinute,
            error)) {
        return false;
    }
    if (s.options.strategic->economy.FindMission(missionId) != nullptr) {
        ZL_INFO("strategic", "任务战略奖励已结算：{}（{}）", missionId,
                s.strategicGameMinute);
    }
    return true;
}

bool WorldSimulation::DonateStrategicSupply(std::string_view regionId,
                                            int amount,
                                            std::uint64_t donorStableId,
                                            std::string* error,
                                            int* deliveredOut) {
    if (deliveredOut != nullptr) {
        *deliveredOut = 0;
    }
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    Impl& s = *m_impl;
    if (s.strategic == nullptr || s.options.strategic == nullptr) {
        return fail("战略状态不可用");
    }
    if (amount <= 0 || amount > 50) {
        return fail("支援补给数量非法");
    }
    StrategicRegionState* region = s.strategic->FindRegion(regionId);
    if (region == nullptr) {
        return fail("战略区域不存在");
    }
    if (region->owner != StrategicOwner::Human &&
        region->owner != StrategicOwner::Contested) {
        return fail("只能支援人类或争夺中的区域");
    }
    if (region->supply >= 100.0f) {
        return fail("目标区域补给已满");
    }
    if (s.strategic->resources.supplies < amount) {
        return fail("战略补给不足");
    }
    const int capacity = std::max(
        0, 100 - static_cast<int>(std::ceil(region->supply)));
    const int delivered = std::min(amount, capacity);
    if (delivered <= 0) {
        return fail("目标区域补给已满");
    }
    const std::string eventId =
        "support." + std::string(regionId) + "." +
        std::to_string(s.strategicGameMinute) + "." +
        std::to_string(donorStableId);
    if (s.strategic->FindEvent(eventId) != nullptr) {
        return fail("重复支援请求");
    }
    const float before = region->supply;
    region->supply += static_cast<float>(delivered);
    StrategicResources delta;
    delta.supplies = -delivered;
    if (!s.strategic->ApplyResourceDelta(regionId, delta, eventId,
                                        s.strategicGameMinute, error)) {
        region->supply = before;
        return false;
    }
    if (deliveredOut != nullptr) {
        *deliveredOut = delivered;
    }
    ZL_INFO("strategic",
            "战略支援：{} +{} 区域补给（全球补给 -{}，玩家 {}）",
            regionId, delivered, delivered,
            static_cast<unsigned long long>(donorStableId));
    return true;
}

bool WorldSimulation::StartRegionalSiege(std::string_view regionId) {
    Impl& s = *m_impl;
    if (s.regionalSiege == nullptr || s.strategic == nullptr ||
        s.options.strategic == nullptr || s.regions == nullptr) {
        return false;
    }
    StrategicRegionState* region = s.strategic->FindRegion(regionId);
    const StrategicRegionConfig* def =
        s.options.strategic->Find(regionId);
    if (region == nullptr || def == nullptr ||
        region->siegePhase != StrategicSiegePhase::Active) {
        return false;
    }
    if (s.regionalSiege->Active()) {
        return std::string_view(s.regionalSiege->RegionId()) == regionId;
    }

    float anchorX = 0.0f;
    float anchorY = 0.0f;
    if (def->hasAnchor) {
        anchorX = def->anchorX;
        anchorY = def->anchorY;
    } else {
        const world::WorldDefinition& world = s.regions->Definition();
        const auto found = std::find_if(
            world.regions.begin(), world.regions.end(),
            [&](const world::WorldRegion& item) {
                return item.id == regionId;
            });
        if (found == world.regions.end()) {
            return false;
        }
        anchorX = static_cast<float>(found->rect.chunkX * core::kChunkSize) +
                  static_cast<float>(found->rect.chunksW *
                                     core::kChunkSize) /
                      2.0f;
        anchorY = static_cast<float>(found->rect.chunkY * core::kChunkSize) +
                  static_cast<float>(found->rect.chunksH *
                                     core::kChunkSize) /
                      2.0f;
    }

    std::uint64_t seed = s.options.strategic->seed;
    for (const unsigned char c : region->id) {
        seed ^= static_cast<std::uint64_t>(c);
        seed *= 1099511628211ull;
    }
    seed ^= static_cast<std::uint64_t>(
                std::max<std::int64_t>(0, region->nextAttackMinute)) *
            0x9e3779b97f4a7c15ull;
    seed ^= seed >> 30u;
    seed *= 0xbf58476d1ce4e5b9ull;
    seed ^= seed >> 27u;
    BaseSiegePlanInput planInput;
    planInput.centerX = anchorX;
    planInput.centerY = anchorY;
    planInput.fallbackSeed = seed;
    const BaseSiegeAttackPlan plan = MakeBaseSiegeAttackPlan({}, planInput);

    const SettlementDef* settlement =
        s.options.settlements != nullptr
            ? FindSettlementContaining(*s.options.settlements, anchorX, anchorY)
            : nullptr;

    RegionalSiegeStart start;
    start.regionId = region->id;
    start.settlementId =
        settlement != nullptr ? settlement->id : std::string{};
    const StrategicRegionConfig* opposing =
        s.options.strategic->Find(region->id);
    start.contestedFaction = opposing != nullptr
                                 ? opposing->contestedFaction
                                 : ContestedFaction::Infected;
    start.regionName = WorldMapRegionName(region->id);
    start.centerX = anchorX;
    start.centerY = anchorY;
    start.attackMinute =
        region->nextAttackMinute >= 0 ? region->nextAttackMinute
                                      : s.strategicGameMinute;
    const double defense =
        static_cast<double>(region->defense) +
        static_cast<double>(region->garrison) *
            static_cast<double>(s.options.strategic->autoResolve
                                    .defensePerGarrison) +
        static_cast<double>(region->fortification) *
            static_cast<double>(s.options.strategic->autoResolve
                                    .defensePerFortification) +
        static_cast<double>(region->supply) *
            static_cast<double>(s.options.strategic->autoResolve
                                    .defensePerSupply);
    start.defenseScore = std::clamp(
        static_cast<int>(std::llround(defense)), 0, 1000000);
    start.enemyStrength = std::clamp(
        static_cast<int>(std::llround(
            static_cast<double>(std::clamp(region->pressure, 0.0f, 100.0f)) *
            static_cast<double>(s.options.strategic->autoResolve
                                    .enemyStrengthPerPressure))),
        1, 1000000);
    start.primaryDirection = plan.primaryDirection;
    start.secondaryDirection = plan.secondaryDirection;
    if (!s.regionalSiege->Start(start, s.strategicGameMinute)) {
        return false;
    }
    ZL_INFO("strategic",
            "详细围攻开始：{}（{}），锚点 {:.1f},{:.1f}，{} 波，敌约 {}，防御评分 {}",
            region->id, start.regionName, static_cast<double>(anchorX),
            static_cast<double>(anchorY),
            static_cast<int>(s.regionalSiege->Config().waves.size()),
            start.enemyStrength, start.defenseScore);
    return true;
}

bool WorldSimulation::StartRegionalReclaim(std::string_view regionId) {
    Impl& s = *m_impl;
    if (s.regionalSiege == nullptr || s.strategic == nullptr ||
        s.options.strategic == nullptr || s.options.settlements == nullptr ||
        s.regions == nullptr) {
        return false;
    }
    StrategicRegionState* region = s.strategic->FindRegion(regionId);
    const StrategicRegionConfig* def =
        s.options.strategic->Find(regionId);
    if (region == nullptr || def == nullptr ||
        region->owner != StrategicOwner::Infested ||
        region->recoveryStage != StrategicRecoveryStage::Counterattack ||
        region->recoveryReadyMinute < 0 ||
        s.strategicGameMinute < region->recoveryReadyMinute) {
        return false;
    }
    if (s.regionalSiege->Active()) {
        return std::string_view(s.regionalSiege->RegionId()) == regionId;
    }

    float anchorX = def->anchorX;
    float anchorY = def->anchorY;
    if (!def->hasAnchor) {
        const world::WorldDefinition& world = s.regions->Definition();
        const auto found = std::find_if(
            world.regions.begin(), world.regions.end(),
            [&](const world::WorldRegion& item) {
                return item.id == regionId;
            });
        if (found == world.regions.end()) {
            return false;
        }
        anchorX = static_cast<float>(found->rect.chunkX * core::kChunkSize) +
                  static_cast<float>(found->rect.chunksW *
                                     core::kChunkSize) /
                      2.0f;
        anchorY = static_cast<float>(found->rect.chunkY * core::kChunkSize) +
                  static_cast<float>(found->rect.chunksH *
                                     core::kChunkSize) /
                      2.0f;
    }
    const SettlementDef* settlement =
        FindSettlementContaining(*s.options.settlements, anchorX, anchorY);
    if (settlement == nullptr) {
        ZL_WARN("strategic", "反攻区域没有可复用的聚落节点：{}", regionId);
        return false;
    }

    std::uint64_t seed = s.options.strategic->seed;
    for (const unsigned char c : region->id) {
        seed ^= static_cast<std::uint64_t>(c);
        seed *= 1099511628211ull;
    }
    seed ^= static_cast<std::uint64_t>(
                std::max<std::int64_t>(0, s.strategicGameMinute)) *
            0x9e3779b97f4a7c15ull;
    seed ^= seed >> 30u;
    seed *= 0xbf58476d1ce4e5b9ull;
    seed ^= seed >> 27u;
    BaseSiegePlanInput planInput;
    planInput.centerX = anchorX;
    planInput.centerY = anchorY;
    planInput.fallbackSeed = seed;
    const BaseSiegeAttackPlan plan = MakeBaseSiegeAttackPlan({}, planInput);

    RegionalSiegeStart start;
    start.regionId = region->id;
    start.settlementId = settlement->id;
    start.regionName = WorldMapRegionName(region->id);
    start.centerX = anchorX;
    start.centerY = anchorY;
    start.attackMinute = s.strategicGameMinute;
    start.defenseScore = 0;
    start.enemyStrength = std::max(1, static_cast<int>(
                                           s.regionalReclaimConfig.waves.size() *
                                           5));
    start.primaryDirection = plan.primaryDirection;
    start.secondaryDirection = plan.secondaryDirection;
    start.reclaim = true;
    if (!s.regionalSiege->Start(start, s.strategicGameMinute)) {
        return false;
    }
    ZL_INFO("strategic",
            "区域反攻开始：{}（{}），锚点 {:.1f},{:.1f}，{} 波，聚落 {}", region->id,
            start.regionName, static_cast<double>(anchorX),
            static_cast<double>(anchorY),
            static_cast<int>(s.regionalReclaimConfig.waves.size()),
            settlement->id);
    return true;
}

void WorldSimulation::UpdateRegionalSiege(float dt, bool paused,
                                          std::string_view playerRegion) {
    Impl& s = *m_impl;
    if (s.regionalSiege == nullptr || !s.regionalSiege->Active()) {
        return;
    }
    if (s.strategic == nullptr) {
        s.regionalSiege->Abort(s.strategicGameMinute, "战略状态不可用");
        return;
    }
    const StrategicRegionState* region =
        s.strategic->FindRegion(s.regionalSiege->RegionId());
    const bool reclaim =
        region != nullptr &&
        region->recoveryStage == StrategicRecoveryStage::Counterattack;
    if (region == nullptr ||
        (reclaim ? region->owner != StrategicOwner::Infested
                 : region->siegePhase != StrategicSiegePhase::Active)) {
        s.regionalSiege->Abort(s.strategicGameMinute, "围攻已由战略层结算");
        return;
    }
    if (playerRegion != std::string_view(s.regionalSiege->RegionId())) {
        s.regionalSiege->Abort(s.strategicGameMinute,
                               "你离开区域，围攻转为战略自动结算");
        return;
    }
    if (paused) {
        return;
    }
    s.regionalSiege->Update(dt, s.strategicGameMinute, true);
}

const RegionalSiegeSnapshot& WorldSimulation::RegionalSiegeStatus() const {
    static const RegionalSiegeSnapshot kEmpty;
    return m_impl->regionalSiege != nullptr ? m_impl->regionalSiege->Snapshot()
                                            : kEmpty;
}

const RegionalSiegeResult& WorldSimulation::RegionalSiegeLastResult() const {
    static const RegionalSiegeResult kEmpty;
    return m_impl->regionalSiege != nullptr ? m_impl->regionalSiege->Result()
                                            : kEmpty;
}

bool WorldSimulation::CaptureStrategicToManifest(
    save::WorldManifest* manifest, std::string* error) const {
    if (m_impl->strategic == nullptr) {
        return true;
    }
    return CaptureStrategicState(*m_impl->strategic, manifest, error);
}

bool WorldSimulation::RestoreStrategicFromManifest(
    const save::WorldManifest& manifest, std::string* error) {
    if (m_impl->strategic == nullptr || m_impl->options.strategic == nullptr) {
        return true;
    }
    if (!RestoreStrategicState(manifest, *m_impl->options.strategic,
                               m_impl->strategic.get(), error)) {
        return false;
    }
    ZL_INFO("strategic", "战略状态已恢复：{} 个区域，事件 {}，资源 人{} 补给{} 弹药{} 燃料{}",
            m_impl->strategic->regions.size(),
            m_impl->strategic->events.size(),
            m_impl->strategic->resources.people,
            m_impl->strategic->resources.supplies,
            m_impl->strategic->resources.ammo,
            m_impl->strategic->resources.fuel);
    return true;
}

bool WorldSimulation::CaptureWorldEventsToManifest(
    save::WorldManifest* manifest, std::string* error) const {
    if (manifest == nullptr) {
        if (error != nullptr) {
            *error = "world event manifest output is null";
        }
        return false;
    }
    manifest->worldEvents.clear();
    manifest->worldEventOutcomes.clear();
    const Impl& s = *m_impl;
    if (s.worldEvents == nullptr) {
        return true;
    }
    for (const WorldEventInstance& event : s.worldEvents->Events()) {
        save::ManifestWorldEvent saved;
        saved.id = event.id;
        saved.defId = event.defId;
        saved.name = event.name;
        saved.kind = event.kind;
        saved.regionId = event.regionId;
        saved.x = event.x;
        saved.y = event.y;
        saved.status = static_cast<std::uint8_t>(event.status);
        saved.stageKind = static_cast<std::uint8_t>(event.stageKind);
        saved.day = event.day;
        saved.stageIndex = event.stageIndex;
        saved.stageProgress = event.stageProgress;
        saved.stageTarget = event.stageTarget;
        saved.stageRadius = event.stageRadius;
        saved.stageTargetId = event.stageTargetId;
        saved.scheduledMinute = event.scheduledMinute;
        saved.warningMinute = event.warningMinute;
        saved.activeMinute = event.activeMinute;
        saved.deadlineMinute = event.deadlineMinute;
        saved.resolvedMinute = event.resolvedMinute;
        saved.chainDepth = event.chainDepth;
        saved.detail = event.detail;
        saved.downgraded = event.downgraded;
        saved.stagesComplete = event.stagesComplete;
        saved.contentSpawned = event.contentSpawned;
        saved.choiceId = event.choiceId;
        saved.nextEventId = event.nextEventId;
        manifest->worldEvents.push_back(std::move(saved));
    }
    manifest->worldEventOutcomes = s.worldEventOutcomes.Keys();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool WorldSimulation::RestoreWorldEventsFromManifest(
    const save::WorldManifest& manifest, std::string* error) {
    Impl& s = *m_impl;
    s.worldEventContentReady.clear();
    s.worldEventContentWarned.clear();
    s.worldEventKilled.clear();
    if (s.worldEvents == nullptr) {
        s.worldEventOutcomes.Reset();
        return true;
    }
    if (manifest.worldEvents.empty()) {
        s.worldEvents->Reset();
    } else {
        std::vector<WorldEventInstance> events;
        events.reserve(manifest.worldEvents.size());
        for (const save::ManifestWorldEvent& saved : manifest.worldEvents) {
            WorldEventInstance event;
            event.id = saved.id;
            event.defId = saved.defId;
            event.name = saved.name;
            event.kind = saved.kind;
            event.regionId = saved.regionId;
            event.x = saved.x;
            event.y = saved.y;
            event.status = static_cast<WorldEventStatus>(saved.status);
            event.stageKind =
                static_cast<WorldEventStageKind>(saved.stageKind);
            event.day = saved.day;
            event.stageIndex = saved.stageIndex;
            event.stageProgress = saved.stageProgress;
            event.stageTarget = saved.stageTarget;
            event.stageRadius = saved.stageRadius;
            event.stageTargetId = saved.stageTargetId;
            event.scheduledMinute = saved.scheduledMinute;
            event.warningMinute = saved.warningMinute;
            event.activeMinute = saved.activeMinute;
            event.deadlineMinute = saved.deadlineMinute;
            event.resolvedMinute = saved.resolvedMinute;
            event.chainDepth = saved.chainDepth;
            event.detail = saved.detail;
            event.downgraded = saved.downgraded;
            event.stagesComplete = saved.stagesComplete;
            event.contentSpawned = saved.contentSpawned;
            event.choiceId = saved.choiceId;
            event.nextEventId = saved.nextEventId;
            events.push_back(std::move(event));
        }
        const double now = s.options.gameTime != nullptr
                               ? s.options.gameTime->TotalGameMinutes()
                               : 0.0;
        const int day =
            s.options.gameTime != nullptr
                ? 1 + static_cast<int>(
                          s.options.gameTime->TotalGameMinutes() / 1440.0)
                : 1;
        if (!s.worldEvents->Restore(events, day, now, error)) {
            return false;
        }
        for (const save::ManifestWorldEvent& saved : manifest.worldEvents) {
            if (saved.contentSpawned) {
                s.worldEventContentReady.insert(saved.id);
            }
        }
    }
    if (!s.worldEventOutcomes.Restore(manifest.worldEventOutcomes)) {
        if (error != nullptr) {
            *error = "世界事件结果账本恢复失败";
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
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

ecs::Entity WorldSimulation::SpawnCompanionAt(
    const CompanionDef& def, std::uint64_t ownerStableId,
    std::string_view homeSettlementId, float targetX, float targetY,
    int searchRadius) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr || ownerStableId == 0 ||
        !std::isfinite(targetX) || !std::isfinite(targetY)) {
        return entt::null;
    }
    const int targetGridX = static_cast<int>(std::floor(targetX));
    const int targetGridY = static_cast<int>(std::floor(targetY));
    const world::ChunkCoord targetChunk =
        world::ChunkOf(targetGridX, targetGridY);
    const world::Chunk* chunk =
        FindChunkInWorld(*s.world, targetChunk.cx, targetChunk.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return entt::null;
    }
    int spawnX = 0;
    int spawnY = 0;
    if (!ai::FindNearestWalkable(
            targetGridX, targetGridY, std::max(1, searchRadius),
            [&s](int wx, int wy) { return !s.IsBlocked(wx, wy); }, &spawnX,
            &spawnY)) {
        return entt::null;
    }
    const float x = static_cast<float>(spawnX) + 0.5f;
    const float y = static_cast<float>(spawnY) + 0.5f;
    const ecs::Entity entity = s.ecs->Create();
    if (entity == entt::null) {
        return entt::null;
    }
    entt::registry& registry = s.ecs->Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
    const ecs::Collider collider;
    registry.emplace<ecs::Collider>(entity, collider);
    if (s.physics != nullptr) {
        ecs::RigidBody rigid;
        rigid.body = s.physics->CreateCharacterBody(
            collider.radius, collider.height, x, y, 0);
        registry.emplace<ecs::RigidBody>(entity, rigid);
    }
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                               0xFF9AD8B0u});
    registry.emplace<ecs::Facing>(entity, ecs::Facing{});
    registry.emplace<ecs::AnimState>(entity, ecs::AnimState{});
    registry.emplace<ecs::ActionState>(entity, ecs::ActionState{});
    registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
    registry.emplace<ecs::PathRequest>(entity, ecs::PathRequest{});
    ecs::PathFollow follow;
    follow.speed = 3.6f;
    registry.emplace<ecs::PathFollow>(entity, follow);
    ecs::Health health;
    health.max = std::max(1.0f, def.baseHealth);
    health.current = health.max;
    registry.emplace<ecs::Health>(entity, health);
    registry.emplace<ecs::FriendlyNpc>(entity);
    registry.emplace<ecs::NoCombatTarget>(entity);
    registry.emplace<Companion>(
        entity, Companion{def.id, ownerStableId,
                          std::string(homeSettlementId)});
    CompanionState state;
    state.loyalty = def.startingLoyalty;
    state.command = CompanionCommand::Follow;
    state.inventory.capacityKg = def.inventoryCapacityKg;
    if (s.options.items != nullptr) {
        const auto equipInitial = [&](ecs::EquipSlot slot,
                                      const std::string& itemId) {
            if (itemId.empty()) {
                return;
            }
            const items::ItemDef* itemDef = s.options.items->Find(itemId);
            if (itemDef == nullptr) {
                ZL_WARN("companions", "同伴 {} 的初始装备 '{}' 不存在",
                        def.id, itemId);
                return;
            }
            ecs::ItemInstance item{itemId, 1, 1.0f, 0};
            if (!items::Equip(state.equipment, *s.options.items, slot, item,
                              nullptr)) {
                ZL_WARN("companions", "同伴 {} 的初始装备 '{}' 槽位不匹配",
                        def.id, itemId);
            }
        };
        equipInitial(ecs::EquipSlot::PrimaryHand, def.equipment.primary);
        equipInitial(ecs::EquipSlot::Body, def.equipment.body);
        for (const CompanionItemStack& stack : def.inventory) {
            const int added = items::AddItem(
                state.inventory, *s.options.items,
                ecs::ItemInstance{stack.item, stack.count, 1.0f, 0});
            if (added != stack.count) {
                ZL_WARN("companions",
                        "同伴 {} 的初始物品 '{}' 只放入 {}/{}",
                        def.id, stack.item, added, stack.count);
            }
        }
    }
    registry.emplace<CompanionState>(entity, std::move(state));
    if (auto* loadedState = registry.try_get<CompanionState>(entity);
        loadedState != nullptr) {
        RefreshCompanionQuestState(def, loadedState);
    }
    (void)PrepareLoadedCompanion(*s.ecs, entity, s.options.items);
    CompanionAiState ai;
    ai.anchorX = x;
    ai.anchorY = y;
    ai.homeX = x;
    ai.homeY = y;
    registry.emplace<CompanionAiState>(entity, ai);
    ZL_INFO("companions", "同伴生成：{}，owner={}，格 ({}, {})", def.id,
            ownerStableId, spawnX, spawnY);
    return entity;
}

bool WorldSimulation::TeleportCompanion(ecs::Entity companion, float targetX,
                                        float targetY, int searchRadius) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr || companion == entt::null ||
        !s.ecs->Alive(companion) ||
        s.ecs->Registry().try_get<Companion>(companion) == nullptr ||
        !std::isfinite(targetX) || !std::isfinite(targetY)) {
        return false;
    }
    int spawnX = 0;
    int spawnY = 0;
    if (!ai::FindNearestWalkable(
            static_cast<int>(std::floor(targetX)),
            static_cast<int>(std::floor(targetY)), std::max(1, searchRadius),
            [&s](int wx, int wy) { return !s.IsBlocked(wx, wy); }, &spawnX,
            &spawnY)) {
        return false;
    }
    const float x = static_cast<float>(spawnX) + 0.5f;
    const float y = static_cast<float>(spawnY) + 0.5f;
    entt::registry& registry = s.ecs->Registry();
    if (auto* position = registry.try_get<ecs::Position>(companion);
        position != nullptr) {
        position->x = x;
        position->y = y;
        position->z = 0;
    }
    if (auto* velocity = registry.try_get<ecs::Velocity>(companion);
        velocity != nullptr) {
        *velocity = ecs::Velocity{};
    }
    if (auto* rigid = registry.try_get<ecs::RigidBody>(companion);
        rigid != nullptr && rigid->body != nullptr && s.physics != nullptr) {
        s.physics->TeleportBody(rigid->body, x, y, 0);
    }
    if (auto* intent = registry.try_get<ecs::MoveIntent>(companion);
        intent != nullptr) {
        intent->x = 0.0f;
        intent->y = 0.0f;
    }
    if (auto* request = registry.try_get<ecs::PathRequest>(companion);
        request != nullptr) {
        request->newRequest = false;
        request->queryId = 0;
        request->state = ecs::PathStatus::None;
        request->goalX = spawnX;
        request->goalY = spawnY;
    }
    if (auto* follow = registry.try_get<ecs::PathFollow>(companion);
        follow != nullptr) {
        follow->waypoints.clear();
        follow->index = 0;
        follow->status = ecs::PathStatus::None;
        follow->anchorX = x;
        follow->anchorY = y;
        follow->stuckTimer = 0.0f;
    }
    if (auto* ai = registry.try_get<CompanionAiState>(companion);
        ai != nullptr) {
        ai->targetId = 0;
        ai->engaged = false;
        ai->autoRetreat = false;
        ai->frozen = false;
        ai->hasAnchor = false;
        ai->anchorX = x;
        ai->anchorY = y;
        ai->repathTimer = 0.0f;
    }
    return true;
}

bool WorldSimulation::RescueCompanion(ecs::Entity companion,
                                      ecs::Entity rescuer,
                                      std::string* error) {
    Impl& s = *m_impl;
    if (!s.initialized || s.companionSystem == nullptr) {
        if (error != nullptr) {
            *error = "同伴系统不可用";
        }
        return false;
    }
    return s.companionSystem->TryRescue(*s.ecs, companion, rescuer, error);
}

int WorldSimulation::SpawnRegionEventContent(const RegionEvent& event) {
    Impl& s = *m_impl;
    if (!s.initialized || s.ecs == nullptr ||
        !std::isfinite(event.x) || !std::isfinite(event.y)) {
        return 0;
    }

    int wx = 0;
    int wy = 0;
    if (!ai::FindNearestWalkable(
            static_cast<int>(std::floor(event.x)),
            static_cast<int>(std::floor(event.y)), 8,
            [&s](int x, int y) { return !s.IsBlocked(x, y); }, &wx, &wy)) {
        return 0;
    }
    const float baseX = static_cast<float>(wx) + 0.5f;
    const float baseY = static_cast<float>(wy) + 0.5f;
    const auto stableId = [&event](int index) {
        return game::RandomEventStableId(event.id, event.day, index);
    };
    const auto spawnZombie = [&](int index, float dx, float dy,
                                 ecs::ZombieVariantKind variant) {
        if (s.IsBlocked(static_cast<int>(std::floor(baseX + dx)),
                        static_cast<int>(std::floor(baseY + dy)))) {
            return false;
        }
        return SpawnZombieAt(baseX + dx, baseY + dy, variant,
                             stableId(index)) != entt::null;
    };
    const auto spawnContainer = [&](int index, float dx, float dy,
                                    const char* tableId) {
        if (s.options.lootTables == nullptr ||
            s.IsBlocked(static_cast<int>(std::floor(baseX + dx)),
                        static_cast<int>(std::floor(baseY + dy)))) {
            return false;
        }
        return game::SpawnLootContainer(
                   *s.ecs, *s.options.lootTables, tableId, baseX + dx,
                   baseY + dy, 0, "object.crate", stableId(index)) != entt::null;
    };

    int spawned = 0;
    switch (event.kind) {
        case RegionEventKind::Horde: {
            static constexpr ecs::ZombieVariantKind kVariants[8] = {
                ecs::ZombieVariantKind::Normal, ecs::ZombieVariantKind::Normal,
                ecs::ZombieVariantKind::Fast, ecs::ZombieVariantKind::Normal,
                ecs::ZombieVariantKind::Normal, ecs::ZombieVariantKind::Fast,
                ecs::ZombieVariantKind::Normal, ecs::ZombieVariantKind::Normal};
            for (int i = 0; i < 8; ++i) {
                const float angle = static_cast<float>(i) * 0.7853982f;
                const float radius = 2.0f + static_cast<float>(i % 3) * 0.65f;
                if (spawnZombie(i + 1, std::cos(angle) * radius,
                                std::sin(angle) * radius, kVariants[i])) {
                    ++spawned;
                }
            }
            break;
        }
        case RegionEventKind::Caravan: {
            for (int i = 0; i < 2; ++i) {
                const ecs::Entity npc =
                    s.ecs->CreateWithStableId(stableId(20 + i));
                if (npc == entt::null) {
                    continue;
                }
                const SettlementNpcKind kind =
                    i == 0 ? SettlementNpcKind::Merchant
                           : SettlementNpcKind::Guard;
                game::ConfigureSettlementNpc(
                    *s.ecs, npc, "region_event", kind,
                    baseX + (i == 0 ? -1.0f : 1.0f),
                    baseY + (i == 0 ? -0.5f : 0.5f), 2.0f);
                ++spawned;
            }
            break;
        }
        case RegionEventKind::SupplyDrop:
            if (spawnContainer(31, 0.0f, 0.0f, "event_supply_crate")) {
                ++spawned;
            }
            if (spawnContainer(32, 1.5f, 0.0f, "event_supply_crate")) {
                ++spawned;
            }
            break;
        case RegionEventKind::Outbreak:
            for (int i = 0; i < 5; ++i) {
                const float offset = static_cast<float>(i) - 2.0f;
                if (spawnZombie(
                        i + 1, offset * 0.8f,
                        static_cast<float>((i % 2 == 0) ? -1 : 1) *
                            (1.0f + static_cast<float>(i % 3) * 0.5f),
                        ecs::ZombieVariantKind::Normal)) {
                    ++spawned;
                }
            }
            if (spawnContainer(40, 0.0f, 0.0f, "event_infected_cache")) {
                ++spawned;
            }
            break;
    }
    if (spawned > 0) {
        ZL_INFO("events", "区域事件内容：{}（{}）生成 {} 个，位置 ({:.1f},{:.1f})",
                RegionEventKindName(event.kind), event.regionId, spawned,
                static_cast<double>(event.x),
                static_cast<double>(event.y));
    }
    return spawned;
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

    save::WorldManifest manifest;
    bool hasManifest = false;
    if (s.regions != nullptr) {
        std::string manifestError;
        const save::SaveManager::LoadResult manifestResult =
            s.options.saveManager->ReadWorldManifest(saveId, &manifest,
                                                     &manifestError);
        if (manifestResult == save::SaveManager::LoadResult::Ok) {
            hasManifest = true;
            std::string mismatch;
            if (world::CheckWorldSaveCompatibility(
                    s.regions->Definition(), &manifest.identity,
                    &mismatch) == world::WorldSaveCompatibility::Mismatch) {
                return fail(mismatch);
            }
        } else if (manifestResult != save::SaveManager::LoadResult::NotFound) {
            return fail("读档失败：world manifest 不可用：" + manifestError);
        }
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
    if (s.options.pois != nullptr && s.options.pois->enabled) {
        ecs::Entity poiWorld =
            s.ecs->FindById(ecs::kPoiWorldProgressStableId);
        if (poiWorld == ecs::EcsWorld::kNull) {
            poiWorld =
                s.ecs->CreateWithStableId(ecs::kPoiWorldProgressStableId);
        }
        if (poiWorld != ecs::EcsWorld::kNull &&
            !s.ecs->Registry().all_of<ecs::PoiWorldProgress>(poiWorld)) {
            s.ecs->Registry().emplace<ecs::PoiWorldProgress>(poiWorld);
        }
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
    if (s.regionEvents != nullptr) {
        if (!hasManifest) {
            s.regionEvents->Reset();
        } else {
            std::vector<RegionEvent> restoredEvents;
            restoredEvents.reserve(manifest.events.size());
            for (const save::ManifestRegionEvent& saved : manifest.events) {
                RegionEvent event;
                event.id = saved.id;
                event.regionId = saved.regionId;
                event.kind = static_cast<RegionEventKind>(saved.kind);
                event.status = static_cast<RegionEventStatus>(saved.status);
                event.day = saved.day;
                event.x = saved.x;
                event.y = saved.y;
                event.remainingSeconds = saved.remainingSeconds;
                event.targetCount = saved.targetCount;
                event.spawnedCount = saved.spawnedCount;
                event.contentSpawned = saved.contentSpawned;
                restoredEvents.push_back(std::move(event));
            }
            const int day =
                s.options.gameTime != nullptr
                    ? 1 + static_cast<int>(
                              s.options.gameTime->TotalGameMinutes() / 1440.0)
                    : 1;
            std::string eventError;
            if (!s.regionEvents->Restore(restoredEvents, day, &eventError)) {
                return fail("区域事件状态恢复失败：" + eventError);
            }
        }
    }
    std::string worldEventError;
    if (!RestoreWorldEventsFromManifest(manifest, &worldEventError)) {
        return fail("世界事件状态恢复失败：" + worldEventError);
    }
    std::string strategicError;
    if (!RestoreStrategicFromManifest(manifest, &strategicError)) {
        return fail("战略状态恢复失败：" + strategicError);
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
