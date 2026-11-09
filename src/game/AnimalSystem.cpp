#include "game/AnimalSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/components/AiComponents.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "core/Constants.h"
#include "core/Log.h"
#include "core/SoundPropagation.h"
#include "ecs/components/CommonComponents.h"
#include "game/Animals.h"
#include "game/Poi.h"
#include "game/WorldQueries.h"
#include "world/GridSquare.h"
#include "world/World.h"
#include "world/WorldCoords.h"

namespace zl::game {
namespace {

constexpr int kAnimalZ = 0;
constexpr float kAnimalRadius = 0.24f;
constexpr float kSpawnSpacing = 1.5f;
constexpr int kTreeRadius = 4;
constexpr int kSpawnAttempts = 8;
constexpr float kMinFleeSeconds = 0.5f;
constexpr float kRetaliateLeash = 12.0f;

std::uint64_t PackCell(int wx, int wy) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(wx)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(wy));
}

std::uint64_t Mix64(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31u);
}

float UnitFromHash(std::uint64_t value) {
    return static_cast<float>(value >> 11u) * (1.0f / 9007199254740992.0f);
}

std::uint32_t TintForSpecies(std::string_view) {
    return 0xFFFFFFFFu;
}

bool HasTree(const world::GridSquare& square) {
    for (int layer = 0; layer < world::kSquareLayerCount; ++layer) {
        for (const world::SquareObject& object :
             square.Layer(static_cast<world::SquareLayer>(layer))) {
            if (object.sprite == "object.tree") {
                return true;
            }
        }
    }
    return false;
}

bool IsAnimalFloor(const world::GridSquare& square) {
    const world::SquareObject* floor = square.Floor();
    return floor != nullptr &&
           (floor->sprite == "tile.floor.grass" ||
            floor->sprite == "tile.floor.dirt");
}

}  // namespace

AnimalSystem::AnimalSystem(const AnimalRegistry& animals, world::World& world,
                           ecs::EcsWorld& ecs, const PoiConfig* pois,
                           Params params)
    : m_animals(animals),
      m_world(world),
      m_ecs(ecs),
      m_pois(pois),
      m_params(params),
      m_rng(params.seed) {
    m_params.maxLive = std::max(0, m_params.maxLive);
    m_params.minPlayerDistance = std::max(0.0f, m_params.minPlayerDistance);
    m_params.poiRadius = std::max(0.0f, m_params.poiRadius);
    m_params.wanderRadius = std::max(2.0f, m_params.wanderRadius);
    m_params.soundWallAttenuation =
        std::clamp(m_params.soundWallAttenuation, 0.0f, 1.0f);
}

AnimalSystem::~AnimalSystem() = default;

void AnimalSystem::SetFocus(float x, float y) {
    if (std::isfinite(x) && std::isfinite(y)) {
        m_focusX = x;
        m_focusY = y;
    }
}

void AnimalSystem::SetCombat(characters::CombatSystem* combat) {
    m_combat = combat;
}

void AnimalSystem::PushSound(const core::SoundEvent& sound) {
    if (std::isfinite(sound.x) && std::isfinite(sound.y) &&
        std::isfinite(sound.radius) && sound.radius > 0.0f) {
        m_pendingSounds.push_back(sound);
    }
}

std::uint64_t AnimalSystem::ChunkKey(int chunkX, int chunkY) const {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkY));
}

bool AnimalSystem::IsStandable(float x, float y) const {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    const int centerX = static_cast<int>(std::floor(x));
    const int centerY = static_cast<int>(std::floor(y));
    constexpr float kProbe = kAnimalRadius;
    constexpr int kProbeCount = 9;
    const float offsets[kProbeCount][2] = {
        {0.0f, 0.0f}, {kProbe, 0.0f}, {-kProbe, 0.0f},
        {0.0f, kProbe}, {0.0f, -kProbe},
        {kProbe, kProbe}, {kProbe, -kProbe},
        {-kProbe, kProbe}, {-kProbe, -kProbe}};
    const world::GridSquare* center = nullptr;
    for (int i = 0; i < kProbeCount; ++i) {
        const int wx = static_cast<int>(std::floor(x + offsets[i][0]));
        const int wy = static_cast<int>(std::floor(y + offsets[i][1]));
        if (game::IsSquareBlocked(m_world, wx, wy, kAnimalZ)) {
            return false;
        }
        const world::GridSquare* square =
            m_world.GetSquare(wx, wy, kAnimalZ);
        if (square == nullptr || !IsAnimalFloor(*square) ||
            square->RoomId() >= 0) {
            return false;
        }
        if (wx == centerX && wy == centerY) {
            center = square;
        }
    }
    return center != nullptr;
}

bool AnimalSystem::HasTreeNear(
    int wx, int wy, const std::unordered_set<std::uint64_t>& trees) const {
    for (int dy = -kTreeRadius; dy <= kTreeRadius; ++dy) {
        for (int dx = -kTreeRadius; dx <= kTreeRadius; ++dx) {
            if (dx * dx + dy * dy > kTreeRadius * kTreeRadius) {
                continue;
            }
            if (trees.contains(PackCell(wx + dx, wy + dy))) {
                return true;
            }
        }
    }
    return false;
}

bool AnimalSystem::NearPoi(const AnimalSpawnRule& rule, int wx, int wy) const {
    if (m_pois == nullptr || !m_pois->enabled ||
        !(m_params.poiRadius > 0.0f)) {
        return false;
    }
    const float px = static_cast<float>(wx) + 0.5f;
    const float py = static_cast<float>(wy) + 0.5f;
    const float radiusSq = m_params.poiRadius * m_params.poiRadius;
    for (const PoiDef& poi : m_pois->defs) {
        if (!rule.poiIds.empty() &&
            std::find(rule.poiIds.begin(), rule.poiIds.end(), poi.id) ==
                rule.poiIds.end()) {
            continue;
        }
        const float dx = px - poi.markerX;
        const float dy = py - poi.markerY;
        if (dx * dx + dy * dy <= radiusSq) {
            return true;
        }
    }
    return false;
}

float AnimalSystem::NextFloat() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng);
}

std::uint64_t AnimalSystem::SpawnRoll(std::uint64_t key,
                                      const std::string& speciesId,
                                      int category, int slot) const {
    const std::uint64_t salt =
        static_cast<std::uint64_t>(category) * 0x9E3779B97F4A7C15ull +
        static_cast<std::uint64_t>(slot) * 0xD1B54A32D192ED03ull;
    return Mix64(key ^ (static_cast<std::uint64_t>(m_params.seed) << 1u) ^
                 static_cast<std::uint64_t>(ecs::HashName(speciesId)) ^ salt);
}

int AnimalSystem::CountLive() const {
    int live = 0;
    m_ecs.Registry().view<ecs::Animal, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::Animal&,
            const ecs::Health& health) {
            if (m_ecs.Alive(entity) && health.current > 0.0f) {
                ++live;
            }
        });
    return live;
}

int AnimalSystem::CountAnimalsInChunk(int chunkX, int chunkY) const {
    int count = 0;
    m_ecs.Registry().view<ecs::Animal, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::Animal&,
            const ecs::Position& position) {
            if (!m_ecs.Alive(entity)) {
                return;
            }
            const world::ChunkCoord coord = world::ChunkOf(
                static_cast<int>(std::floor(position.x)),
                static_cast<int>(std::floor(position.y)));
            if (coord.cx == chunkX && coord.cy == chunkY) {
                ++count;
            }
        });
    return count;
}

bool AnimalSystem::AnimalNear(float x, float y, float radius) const {
    const float radiusSq = radius * radius;
    bool near = false;
    m_ecs.Registry().view<ecs::Animal, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::Animal&,
            const ecs::Position& position) {
            if (near || !m_ecs.Alive(entity)) {
                return;
            }
            const float dx = position.x - x;
            const float dy = position.y - y;
            near = dx * dx + dy * dy <= radiusSq;
        });
    return near;
}

void AnimalSystem::RefreshLive() {
    m_stats.live = CountLive();
    m_stats.seededChunks = static_cast<int>(m_seeded.size());
    m_stats.pendingChunks = static_cast<int>(m_saved.size());
    int saved = 0;
    for (const auto& entry : m_saved) {
        saved += static_cast<int>(entry.second.size());
    }
    m_stats.savedAnimals = saved;
}

ecs::Entity AnimalSystem::SpawnAnimal(const AnimalDef& def, float x, float y,
                                      float health, const SavedAnimal* saved) {
    entt::registry& registry = m_ecs.Registry();
    const ecs::Entity entity = m_ecs.Create();
    if (entity == ecs::EcsWorld::kNull) {
        return ecs::EcsWorld::kNull;
    }
    registry.emplace<ecs::Position>(entity,
                                    ecs::Position{x, y, kAnimalZ});
    ecs::Health value;
    value.max = std::max(1.0f, def.maxHealth);
    value.current = std::clamp(health, 1.0f, value.max);
    registry.emplace<ecs::Health>(entity, value);
    registry.emplace<ecs::Animal>(
        entity, ecs::Animal{def.id, def.kind});
    registry.emplace<ecs::Flee>(entity, ecs::Flee{});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName(def.sprite), 0,
                               TintForSpecies(def.id)});
    registry.emplace<ecs::LodState>(entity, ecs::LodState{});
    registry.emplace<ecs::Collider>(
        entity, ecs::Collider{kAnimalRadius, 0.75f,
                              ecs::kColliderCharacter, ecs::kColliderAll});
    ecs::AnimalWander wander;
    wander.idleRemaining = NextFloat() * 2.0f;
    registry.emplace<ecs::AnimalWander>(entity, wander);
    if (saved != nullptr && saved->fleeing) {
        ecs::Flee flee;
        flee.active = true;
        flee.remaining = std::clamp(saved->fleeRemaining, 0.0f, 120.0f);
        flee.directionX = saved->fleeDirX;
        flee.directionY = saved->fleeDirY;
        flee.threatX = saved->threatX;
        flee.threatY = saved->threatY;
        flee.alertness = std::clamp(saved->alertness, 0.0f, 1.0f);
        registry.replace<ecs::Flee>(entity, flee);
    }
    m_stats.live = CountLive();
    return entity;
}

void AnimalSystem::SpawnChunk(int chunkX, int chunkY) {
    if (!m_params.enabled || m_params.maxLive <= 0 || m_animals.Size() == 0) {
        return;
    }
    RefreshLive();
    if (m_stats.live >= m_params.maxLive) {
        return;
    }

    const int originX = chunkX * core::kChunkSize;
    const int originY = chunkY * core::kChunkSize;
    std::unordered_set<std::uint64_t> trees;
    for (int wy = originY - kTreeRadius;
         wy < originY + core::kChunkSize + kTreeRadius; ++wy) {
        for (int wx = originX - kTreeRadius;
             wx < originX + core::kChunkSize + kTreeRadius; ++wx) {
            const world::GridSquare* square =
                m_world.GetSquare(wx, wy, kAnimalZ);
            if (square != nullptr && HasTree(*square)) {
                trees.insert(PackCell(wx, wy));
            }
        }
    }

    std::vector<SpawnCell> cells;
    cells.reserve(static_cast<std::size_t>(core::kChunkSize *
                                           core::kChunkSize));
    for (int ly = 0; ly < core::kChunkSize; ++ly) {
        for (int lx = 0; lx < core::kChunkSize; ++lx) {
            const int wx = originX + lx;
            const int wy = originY + ly;
            const float centerX = static_cast<float>(wx) + 0.5f;
            const float centerY = static_cast<float>(wy) + 0.5f;
            if (!IsStandable(centerX, centerY)) {
                continue;
            }
            const world::GridSquare* square =
                m_world.GetSquare(wx, wy, kAnimalZ);
            if (square == nullptr) {
                continue;
            }
            cells.push_back(
                SpawnCell{wx, wy, HasTreeNear(wx, wy, trees)});
        }
    }
    if (cells.empty()) {
        return;
    }

    const std::uint64_t key = ChunkKey(chunkX, chunkY);
    for (const AnimalDef& def : m_animals.All()) {
        if (def.spawn.maxPerChunk <= 0 ||
            (def.spawn.forest <= 0.0f && def.spawn.meadow <= 0.0f &&
             def.spawn.poi <= 0.0f)) {
            continue;
        }
        int spawned = 0;
        const int categories[3] = {2, 0, 1};
        const float weights[3] = {def.spawn.poi, def.spawn.forest,
                                  def.spawn.meadow};
        for (int categoryIndex = 0;
             categoryIndex < 3 && spawned < def.spawn.maxPerChunk;
             ++categoryIndex) {
            const int category = categories[categoryIndex];
            const float weight = weights[categoryIndex];
            if (!(weight > 0.0f) || m_stats.live >= m_params.maxLive) {
                continue;
            }
            std::vector<const SpawnCell*> candidates;
            for (const SpawnCell& cell : cells) {
                if (category == 2) {
                    if (NearPoi(def.spawn, cell.x, cell.y)) {
                        candidates.push_back(&cell);
                    }
                } else if (category == 0) {
                    if (cell.forest) {
                        candidates.push_back(&cell);
                    }
                } else if (!cell.forest) {
                    candidates.push_back(&cell);
                }
            }
            if (candidates.empty()) {
                continue;
            }
            for (int slot = 0; slot < def.spawn.maxPerChunk; ++slot) {
                if (spawned >= def.spawn.maxPerChunk ||
                    m_stats.live >= m_params.maxLive) {
                    break;
                }
                const float roll =
                    UnitFromHash(SpawnRoll(key, def.id, category, slot));
                if (roll >= weight) {
                    continue;
                }
                const std::uint64_t selection =
                    SpawnRoll(key, def.id, category + 16, slot);
                const int attempts =
                    std::min(kSpawnAttempts,
                             static_cast<int>(candidates.size()));
                for (int attempt = 0; attempt < attempts; ++attempt) {
                    const std::size_t index =
                        (selection +
                         static_cast<std::uint64_t>(attempt) *
                             0x9E3779B97F4A7C15ull) %
                        candidates.size();
                    const SpawnCell& cell = *candidates[index];
                    const float px = static_cast<float>(cell.x) + 0.5f;
                    const float py = static_cast<float>(cell.y) + 0.5f;
                    const float dx = px - m_focusX;
                    const float dy = py - m_focusY;
                    if (dx * dx + dy * dy <
                        m_params.minPlayerDistance *
                            m_params.minPlayerDistance) {
                        continue;
                    }
                    if (AnimalNear(px, py, kSpawnSpacing)) {
                        continue;
                    }
                    if (SpawnAnimal(def, px, py, def.maxHealth, nullptr) ==
                        ecs::EcsWorld::kNull) {
                        continue;
                    }
                    ++spawned;
                    ++m_stats.spawned;
                    break;
                }
            }
        }
    }
    RefreshLive();
}

void AnimalSystem::RestoreChunk(const std::vector<SavedAnimal>& saved) {
    for (const SavedAnimal& record : saved) {
        if (CountLive() >= m_params.maxLive) {
            break;
        }
        const AnimalDef* def = m_animals.Find(record.speciesId);
        if (def == nullptr || !std::isfinite(record.x) ||
            !std::isfinite(record.y)) {
            continue;
        }
        if (!IsStandable(record.x, record.y)) {
            continue;
        }
        if (SpawnAnimal(*def, record.x, record.y, record.health, &record) !=
            ecs::EcsWorld::kNull) {
            ++m_stats.restored;
        }
    }
}

void AnimalSystem::RefreshCorpseVisuals() {
    entt::registry& registry = m_ecs.Registry();
    registry.view<ecs::AnimalCorpse, ecs::SpriteRef>().each(
        [&](const ecs::Entity entity, const ecs::AnimalCorpse& corpse,
            ecs::SpriteRef& sprite) {
            if (!m_ecs.Alive(entity)) {
                return;
            }
            const AnimalDef* def = m_animals.Find(corpse.speciesId);
            if (def == nullptr) {
                return;
            }
            const std::uint32_t logical = ecs::HashName(def->corpseSprite);
            if (sprite.sprite != logical) {
                sprite.sprite = logical;
            }
        });
}

void AnimalSystem::OnChunkLoaded(int chunkX, int chunkY) {
    const std::uint64_t key = ChunkKey(chunkX, chunkY);
    const auto saved = m_saved.find(key);
    if (saved != m_saved.end()) {
        RestoreChunk(saved->second);
        m_saved.erase(saved);
        m_seeded.insert(key);
        RefreshLive();
        return;
    }
    if (m_seeded.contains(key)) {
        return;
    }
    m_seeded.insert(key);
    if (CountAnimalsInChunk(chunkX, chunkY) > 0) {
        RefreshLive();
        return;
    }
    SpawnChunk(chunkX, chunkY);
    RefreshLive();
}

void AnimalSystem::OnChunkUnloaded(int chunkX, int chunkY) {
    const std::uint64_t key = ChunkKey(chunkX, chunkY);
    std::vector<SavedAnimal> records;
    std::vector<ecs::Entity> remove;
    m_ecs.Registry().view<ecs::Animal, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::Animal& animal,
            const ecs::Position& position) {
            if (!m_ecs.Alive(entity)) {
                return;
            }
            const world::ChunkCoord coord = world::ChunkOf(
                static_cast<int>(std::floor(position.x)),
                static_cast<int>(std::floor(position.y)));
            if (coord.cx != chunkX || coord.cy != chunkY) {
                return;
            }
            SavedAnimal record;
            record.speciesId = animal.speciesId;
            record.x = position.x;
            record.y = position.y;
            const auto* health =
                m_ecs.Registry().try_get<ecs::Health>(entity);
            record.health = health != nullptr ? health->current : 1.0f;
            const auto* flee = m_ecs.Registry().try_get<ecs::Flee>(entity);
            if (flee != nullptr && flee->active) {
                record.fleeing = true;
                record.fleeRemaining = flee->remaining;
                record.fleeDirX = flee->directionX;
                record.fleeDirY = flee->directionY;
                record.threatX = flee->threatX;
                record.threatY = flee->threatY;
                record.alertness = flee->alertness;
            }
            records.push_back(std::move(record));
            remove.push_back(entity);
        });
    if (records.empty()) {
        return;
    }
    m_saved[key] = std::move(records);
    for (const ecs::Entity entity : remove) {
        m_ecs.Destroy(entity);
    }
    RefreshLive();
}

void AnimalSystem::Reset() {
    m_seeded.clear();
    m_saved.clear();
    m_pendingSounds.clear();
    m_players.clear();
    m_stats = Stats{};
    m_rng.seed(m_params.seed);
}

void AnimalSystem::StoreState(ecs::EcsWorld& ecs) const {
    std::vector<std::pair<int, int>> chunks;
    chunks.reserve(m_seeded.size() + m_saved.size());
    for (const std::uint64_t key : m_seeded) {
        chunks.emplace_back(static_cast<int>(key >> 32u),
                            static_cast<int>(key & 0xFFFFFFFFull));
    }
    for (const auto& [key, records] : m_saved) {
        static_cast<void>(records);
        chunks.emplace_back(static_cast<int>(key >> 32u),
                            static_cast<int>(key & 0xFFFFFFFFull));
    }
    std::sort(chunks.begin(), chunks.end());
    chunks.erase(std::unique(chunks.begin(), chunks.end()), chunks.end());

    ecs::AnimalWorldState state;
    std::size_t totalAnimals = 0;
    for (const auto& [chunkX, chunkY] : chunks) {
        if (state.chunks.size() >= ecs::kMaxAnimalWorldStateChunks) {
            break;
        }
        ecs::AnimalSavedChunk chunk;
        chunk.chunkX = chunkX;
        chunk.chunkY = chunkY;
        const auto saved = m_saved.find(ChunkKey(chunkX, chunkY));
        if (saved != m_saved.end()) {
            for (const SavedAnimal& record : saved->second) {
                if (chunk.animals.size() >=
                        ecs::kMaxAnimalWorldStateAnimalsPerChunk ||
                    totalAnimals >= ecs::kMaxAnimalWorldStateAnimals) {
                    break;
                }
                ecs::AnimalSavedRecord copy;
                copy.speciesId = record.speciesId;
                copy.x = record.x;
                copy.y = record.y;
                copy.health = record.health;
                copy.fleeing = record.fleeing;
                copy.fleeRemaining = record.fleeRemaining;
                copy.fleeDirX = record.fleeDirX;
                copy.fleeDirY = record.fleeDirY;
                copy.threatX = record.threatX;
                copy.threatY = record.threatY;
                copy.alertness = record.alertness;
                chunk.animals.push_back(std::move(copy));
                ++totalAnimals;
            }
        }
        state.chunks.push_back(std::move(chunk));
    }

    ecs::Entity entity = ecs.FindById(ecs::kAnimalWorldStateStableId);
    if (entity == ecs::EcsWorld::kNull) {
        entity = ecs.CreateWithStableId(ecs::kAnimalWorldStateStableId);
    }
    if (entity == ecs::EcsWorld::kNull) {
        return;
    }
    ecs.Registry().emplace_or_replace<ecs::AnimalWorldState>(entity,
                                                             std::move(state));
}

void AnimalSystem::LoadState(ecs::EcsWorld& ecs) {
    m_saved.clear();
    m_seeded.clear();
    const ecs::Entity entity = ecs.FindById(ecs::kAnimalWorldStateStableId);
    if (entity == ecs::EcsWorld::kNull) {
        return;
    }
    const auto* state = ecs.Registry().try_get<ecs::AnimalWorldState>(entity);
    if (state == nullptr) {
        return;
    }
    for (const ecs::AnimalSavedChunk& chunk : state->chunks) {
        if (chunk.chunkX < -1000000 || chunk.chunkX > 1000000 ||
            chunk.chunkY < -1000000 || chunk.chunkY > 1000000) {
            continue;
        }
        const std::uint64_t key = ChunkKey(chunk.chunkX, chunk.chunkY);
        m_seeded.insert(key);
        if (chunk.animals.empty()) {
            continue;
        }
        std::vector<SavedAnimal> records;
        records.reserve(std::min<std::size_t>(
            chunk.animals.size(), ecs::kMaxAnimalWorldStateAnimalsPerChunk));
        for (const ecs::AnimalSavedRecord& record : chunk.animals) {
            if (records.size() >= ecs::kMaxAnimalWorldStateAnimalsPerChunk) {
                break;
            }
            if (record.speciesId.empty() ||
                record.speciesId.size() > 64 ||
                !std::isfinite(record.x) || !std::isfinite(record.y) ||
                !std::isfinite(record.health) || record.health < 0.0f ||
                !std::isfinite(record.fleeRemaining) ||
                !std::isfinite(record.fleeDirX) ||
                !std::isfinite(record.fleeDirY) ||
                !std::isfinite(record.threatX) ||
                !std::isfinite(record.threatY) ||
                !std::isfinite(record.alertness)) {
                continue;
            }
            SavedAnimal saved;
            saved.speciesId = record.speciesId;
            saved.x = record.x;
            saved.y = record.y;
            saved.health = record.health;
            saved.fleeing = record.fleeing;
            saved.fleeRemaining = record.fleeRemaining;
            saved.fleeDirX = record.fleeDirX;
            saved.fleeDirY = record.fleeDirY;
            saved.threatX = record.threatX;
            saved.threatY = record.threatY;
            saved.alertness = record.alertness;
            records.push_back(std::move(saved));
        }
        if (!records.empty()) {
            m_saved[key] = std::move(records);
        }
    }
    RefreshLive();
}

void AnimalSystem::CollectPlayers() {
    m_players.clear();
    entt::registry& registry = m_ecs.Registry();
    registry.view<ecs::PlayerTag, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::Position& position) {
            if (!m_ecs.Alive(entity)) {
                return;
            }
            const auto* health = registry.try_get<ecs::Health>(entity);
            if (health != nullptr && health->current <= 0.0f) {
                return;
            }
            m_players.push_back(
                PlayerTarget{entity, position.x, position.y, position.z});
        });
}

const AnimalSystem::PlayerTarget* AnimalSystem::NearestPlayer(
    float x, float y, float maxRadius) const {
    const PlayerTarget* best = nullptr;
    float bestDistanceSq = maxRadius > 0.0f ? maxRadius * maxRadius : 1.0e30f;
    for (const PlayerTarget& player : m_players) {
        const float dx = player.x - x;
        const float dy = player.y - y;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq <= bestDistanceSq) {
            bestDistanceSq = distanceSq;
            best = &player;
        }
    }
    return best;
}

bool AnimalSystem::HasLineOfSight(float x0, float y0, float x1,
                                  float y1) const {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.001f) {
        return true;
    }
    const int steps = std::max(2, static_cast<int>(std::ceil(distance / 0.5f)));
    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int wx = static_cast<int>(std::floor(x0 + dx * t));
        const int wy = static_cast<int>(std::floor(y0 + dy * t));
        if (game::IsSquareBlocked(m_world, wx, wy, kAnimalZ)) {
            return false;
        }
    }
    return true;
}

bool AnimalSystem::HearsSound(const AnimalDef& def,
                              const core::SoundEvent& sound, float x,
                              float y) const {
    const float radius = std::min(def.hearingRadius, sound.radius);
    if (!(radius > 0.0f)) {
        return false;
    }
    const float dx = sound.x - x;
    const float dy = sound.y - y;
    const float distanceSq = dx * dx + dy * dy;
    if (distanceSq > radius * radius) {
        return false;
    }
    const float distance = std::sqrt(distanceSq);
    const int walls = core::CountBlockingCells(
        x, y, sound.x, sound.y, [this](int wx, int wy) {
            return !game::IsSquareBlocked(m_world, wx, wy, kAnimalZ);
        });
    return core::IsSoundAudible(radius, distance, walls,
                                m_params.soundWallAttenuation);
}

void AnimalSystem::StartFlee(ecs::Entity entity, const AnimalDef& def,
                             ecs::Flee& flee, ecs::AnimalWander& wander,
                             float threatX, float threatY) {
    const auto* position = m_ecs.Registry().try_get<ecs::Position>(entity);
    if (position == nullptr) {
        return;
    }
    float dx = position->x - threatX;
    float dy = position->y - threatY;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0001f) {
        const float angle = NextFloat() * 6.2831853f;
        dx = std::cos(angle);
        dy = std::sin(angle);
        length = 1.0f;
    }
    flee.active = true;
    flee.remaining = std::max(kMinFleeSeconds, def.fleeSeconds);
    flee.directionX = dx / length;
    flee.directionY = dy / length;
    flee.threatX = threatX;
    flee.threatY = threatY;
    flee.alertness = 1.0f;
    wander.hasGoal = false;
    wander.retaliating = false;
    wander.retaliateRemaining = 0.0f;
    wander.idleRemaining = 0.0f;
}

void AnimalSystem::StartRetaliate(ecs::Entity entity, const AnimalDef& def,
                                  ecs::AnimalWander& wander, float threatX,
                                  float threatY) {
    if (!m_ecs.Alive(entity)) {
        return;
    }
    wander.retaliating = true;
    wander.retaliateRemaining = std::max(kMinFleeSeconds, def.fleeSeconds);
    wander.threatX = threatX;
    wander.threatY = threatY;
    wander.hasGoal = false;
    wander.idleRemaining = 0.0f;
    auto* flee = m_ecs.Registry().try_get<ecs::Flee>(entity);
    if (flee != nullptr) {
        flee->active = false;
    }
}

bool AnimalSystem::StepToward(ecs::Entity entity, float targetX, float targetY,
                              float speed, float dt, float stopDistance) {
    auto* position = m_ecs.Registry().try_get<ecs::Position>(entity);
    if (position == nullptr || !(dt > 0.0f)) {
        return false;
    }
    const float dx = targetX - position->x;
    const float dy = targetY - position->y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= stopDistance) {
        return true;
    }
    const float step =
        std::min(distance, std::max(0.1f, speed) * dt);
    const float nx = position->x + dx / distance * step;
    const float ny = position->y + dy / distance * step;
    if (IsStandable(nx, ny)) {
        position->x = nx;
        position->y = ny;
        return true;
    }
    if (IsStandable(nx, position->y)) {
        position->x = nx;
        return true;
    }
    if (IsStandable(position->x, ny)) {
        position->y = ny;
        return true;
    }
    return false;
}

void AnimalSystem::UpdateFlee(ecs::Entity entity, ecs::Flee& flee,
                              const AnimalDef& def, float dt) {
    if (!flee.active) {
        return;
    }
    flee.remaining = std::max(0.0f, flee.remaining - dt);
    flee.alertness = std::max(0.0f, flee.remaining /
                                      std::max(kMinFleeSeconds, def.fleeSeconds));
    if (flee.remaining <= 0.0f) {
        flee.active = false;
        flee.alertness = 0.0f;
        return;
    }
    auto* position = m_ecs.Registry().try_get<ecs::Position>(entity);
    if (position == nullptr) {
        return;
    }
    float dx = position->x - flee.threatX;
    float dy = position->y - flee.threatY;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0001f) {
        dx = flee.directionX;
        dy = flee.directionY;
        length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0001f) {
            return;
        }
    }
    flee.directionX = dx / length;
    flee.directionY = dy / length;
    const float targetX = position->x + flee.directionX * def.fleeSpeed;
    const float targetY = position->y + flee.directionY * def.fleeSpeed;
    if (StepToward(entity, targetX, targetY, def.fleeSpeed, dt, 0.01f)) {
        return;
    }
    const float sideX = -flee.directionY;
    const float sideY = flee.directionX;
    if (!StepToward(entity, position->x + sideX * def.fleeSpeed,
                    position->y + sideY * def.fleeSpeed, def.fleeSpeed, dt,
                    0.01f)) {
        (void)StepToward(entity, position->x - sideX * def.fleeSpeed,
                         position->y - sideY * def.fleeSpeed, def.fleeSpeed, dt,
                         0.01f);
    }
}

void AnimalSystem::UpdateRetaliate(ecs::Entity entity,
                                   ecs::AnimalWander& wander,
                                   const AnimalDef& def, float dt) {
    if (!wander.retaliating) {
        return;
    }
    wander.retaliateRemaining =
        std::max(0.0f, wander.retaliateRemaining - dt);
    wander.attackCooldown = std::max(0.0f, wander.attackCooldown - dt);
    if (wander.retaliateRemaining <= 0.0f) {
        wander.retaliating = false;
        wander.idleRemaining = 0.5f;
        return;
    }
    const auto* position = m_ecs.Registry().try_get<ecs::Position>(entity);
    if (position == nullptr) {
        return;
    }
    const float leash = std::max(kRetaliateLeash, def.fleeRadius * 2.0f);
    const PlayerTarget* target = NearestPlayer(position->x, position->y, leash);
    if (target != nullptr && target->z != kAnimalZ) {
        target = nullptr;
    }
    float targetX = wander.threatX;
    float targetY = wander.threatY;
    float distance = std::sqrt((targetX - position->x) * (targetX - position->x) +
                               (targetY - position->y) * (targetY - position->y));
    if (target != nullptr) {
        targetX = target->x;
        targetY = target->y;
        distance = std::sqrt(
            (targetX - position->x) * (targetX - position->x) +
            (targetY - position->y) * (targetY - position->y));
        if (distance <= def.attackRange && wander.attackCooldown <= 0.0f &&
            m_combat != nullptr &&
            HasLineOfSight(position->x, position->y, targetX, targetY)) {
            ecs::DamageEvent event;
            event.sourceId = m_ecs.IdOf(entity).id;
            event.type = ecs::DamageType::Blunt;
            event.amount = def.damage;
            if (distance > 0.0001f) {
                event.dirX = (targetX - position->x) / distance;
                event.dirY = (targetY - position->y) / distance;
            }
            event.knockback = 2.0f;
            event.staggerSeconds = 0.35f;
            if (m_combat->ApplyDamage(m_ecs, target->entity, event)) {
                ++m_stats.attacks;
                m_attacks.push_back(
                    AttackInfo{entity, target->entity, def.id, event.amount});
                wander.attackCooldown = std::max(0.1f, def.attackCooldown);
            }
            return;
        }
    }
    if (distance <= std::max(0.5f, def.attackRange * 0.8f)) {
        return;
    }
    (void)StepToward(entity, targetX, targetY, def.fleeSpeed, dt,
                     std::max(0.5f, def.attackRange * 0.8f));
}

void AnimalSystem::ChooseGoal(ecs::Entity entity, ecs::AnimalWander& wander,
                              const AnimalDef& def) {
    for (int attempt = 0; attempt < kSpawnAttempts; ++attempt) {
        const float angle = NextFloat() * 6.2831853f;
        const float radius =
            1.0f + NextFloat() * std::max(1.0f, m_params.wanderRadius - 1.0f);
        const auto* position =
            m_ecs.Registry().try_get<ecs::Position>(entity);
        if (position == nullptr) {
            return;
        }
        const float x = position->x + std::cos(angle) * radius;
        const float y = position->y + std::sin(angle) * radius;
        if (!IsStandable(x, y)) {
            continue;
        }
        wander.hasGoal = true;
        wander.goalX = x;
        wander.goalY = y;
        wander.idleRemaining = 0.0f;
        return;
    }
    wander.hasGoal = false;
    wander.idleRemaining = std::max(0.5f, def.fleeSeconds);
}

void AnimalSystem::UpdateWander(ecs::Entity entity, ecs::AnimalWander& wander,
                                const AnimalDef& def, float dt) {
    entt::registry& registry = m_ecs.Registry();
    auto* position = registry.try_get<ecs::Position>(entity);
    if (position == nullptr) {
        return;
    }
    if (!wander.hasGoal) {
        if (wander.idleRemaining > 0.0f) {
            wander.idleRemaining = std::max(0.0f, wander.idleRemaining - dt);
            return;
        }
        ChooseGoal(entity, wander, def);
        if (!wander.hasGoal) {
            return;
        }
    }
    const float dx = wander.goalX - position->x;
    const float dy = wander.goalY - position->y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.20f) {
        wander.hasGoal = false;
        wander.idleRemaining = 1.0f + NextFloat() * 2.0f;
        return;
    }
    const float step =
        std::min(distance, std::max(0.1f, def.wanderSpeed) * dt);
    const float nx = position->x + dx / distance * step;
    const float ny = position->y + dy / distance * step;
    if (IsStandable(nx, ny)) {
        position->x = nx;
        position->y = ny;
    } else if (IsStandable(nx, position->y)) {
        position->x = nx;
    } else if (IsStandable(position->x, ny)) {
        position->y = ny;
    } else {
        wander.hasGoal = false;
        wander.idleRemaining = 0.75f;
    }
}

void AnimalSystem::Update(float dt) {
    m_attacks.clear();
    RefreshCorpseVisuals();
    m_stats.live = CountLive();
    m_stats.seededChunks = static_cast<int>(m_seeded.size());
    m_stats.pendingChunks = static_cast<int>(m_saved.size());
    int saved = 0;
    for (const auto& entry : m_saved) {
        saved += static_cast<int>(entry.second.size());
    }
    m_stats.savedAnimals = saved;
    m_stats.updated = 0;
    m_stats.skippedFar = 0;
    m_stats.fleeing = 0;
    m_stats.retaliating = 0;
    if (!m_params.enabled || !(dt > 0.0f)) {
        m_pendingSounds.clear();
        return;
    }
    const float stepDt = std::min(dt, 0.25f);
    CollectPlayers();
    entt::registry& registry = m_ecs.Registry();
    if (m_params.logAnimals) {
        m_nearestLogTimer += dt;
        if (m_nearestLogTimer >= 0.5f) {
            m_nearestLogTimer = 0.0f;
            registry.view<ecs::Animal, ecs::Position, ecs::Health>().each(
                [&](const ecs::Entity entity, const ecs::Animal& animal,
                    const ecs::Position& position, const ecs::Health& health) {
                    if (!m_ecs.Alive(entity) || health.current <= 0.0f ||
                        registry.any_of<ecs::Dying>(entity)) {
                        return;
                    }
                    const auto* flee = registry.try_get<ecs::Flee>(entity);
                    const auto* wander =
                        registry.try_get<ecs::AnimalWander>(entity);
                    ZL_INFO("animals",
                            "H1 animal id={} species={} x={:.2f} y={:.2f} "
                            "health={:.1f} fleeing={} retaliating={} "
                            "player={:.2f},{:.2f}",
                            m_ecs.IdOf(entity).id, animal.speciesId,
                            static_cast<double>(position.x),
                            static_cast<double>(position.y),
                            static_cast<double>(health.current),
                            flee != nullptr && flee->active ? "true"
                                                               : "false",
                            wander != nullptr && wander->retaliating ? "true"
                                                                     : "false",
                            static_cast<double>(m_focusX),
                            static_cast<double>(m_focusY));
                });
        }
    }
    registry.view<ecs::Animal, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::Animal& animal,
            const ecs::Position& position, const ecs::Health& health) {
            if (!m_ecs.Alive(entity) || health.current <= 0.0f ||
                registry.any_of<ecs::Dying>(entity)) {
                return;
            }
            const AnimalDef* def = m_animals.Find(animal.speciesId);
            if (def == nullptr) {
                return;
            }
            if (!registry.all_of<ecs::LodState>(entity)) {
                registry.emplace<ecs::LodState>(entity);
            }
            ecs::AnimalWander& wander =
                registry.get_or_emplace<ecs::AnimalWander>(entity);
            ecs::Flee& flee = registry.get_or_emplace<ecs::Flee>(entity);

            if (const auto* hit = registry.try_get<ecs::HitReaction>(entity);
                hit != nullptr && hit->hitSerial != 0 &&
                hit->hitSerial != wander.lastHitSerial) {
                wander.lastHitSerial = hit->hitSerial;
                float threatX = position.x;
                float threatY = position.y;
                const float dirLength =
                    std::sqrt(hit->sourceDirX * hit->sourceDirX +
                              hit->sourceDirY * hit->sourceDirY);
                if (dirLength > 0.0001f) {
                    threatX += hit->sourceDirX / dirLength;
                    threatY += hit->sourceDirY / dirLength;
                }
                if (animal.kind == ecs::AnimalKind::Dangerous &&
                    hit->sourceType != ecs::DamageType::Fire) {
                    StartRetaliate(entity, *def, wander, threatX, threatY);
                } else {
                    StartFlee(entity, *def, flee, wander, threatX, threatY);
                }
            }

            const float senseRadius =
                std::max(def->alertRadius, def->fleeRadius);
            if (const PlayerTarget* player =
                    NearestPlayer(position.x, position.y, senseRadius);
                player != nullptr && player->z == kAnimalZ) {
                const float dx = player->x - position.x;
                const float dy = player->y - position.y;
                const float distanceSq = dx * dx + dy * dy;
                const bool close =
                    def->fleeRadius > 0.0f &&
                    distanceSq <= def->fleeRadius * def->fleeRadius;
                const bool seen =
                    def->alertRadius > 0.0f &&
                    distanceSq <= def->alertRadius * def->alertRadius &&
                    HasLineOfSight(position.x, position.y, player->x,
                                   player->y);
                if (close || seen) {
                    if (animal.kind == ecs::AnimalKind::Dangerous) {
                        StartRetaliate(entity, *def, wander, player->x,
                                       player->y);
                    } else {
                        StartFlee(entity, *def, flee, wander, player->x,
                                  player->y);
                    }
                }
            }

            for (const core::SoundEvent& sound : m_pendingSounds) {
                if (sound.type == core::SoundType::Ui ||
                    !HearsSound(*def, sound, position.x, position.y)) {
                    continue;
                }
                ++m_stats.heard;
                if (animal.kind == ecs::AnimalKind::Dangerous) {
                    StartRetaliate(entity, *def, wander, sound.x, sound.y);
                } else {
                    StartFlee(entity, *def, flee, wander, sound.x, sound.y);
                }
            }

            const auto* lod = registry.try_get<ecs::LodState>(entity);
            if (lod != nullptr && !lod->updateThisTick) {
                ++m_stats.skippedFar;
                return;
            }
            if (animal.kind == ecs::AnimalKind::Dangerous) {
                if (wander.retaliating) {
                    UpdateRetaliate(entity, wander, *def, stepDt);
                } else {
                    UpdateWander(entity, wander, *def, stepDt);
                }
            } else if (flee.active) {
                UpdateFlee(entity, flee, *def, stepDt);
            } else {
                UpdateWander(entity, wander, *def, stepDt);
            }
            ++m_stats.updated;
        });
    registry.view<ecs::Animal, ecs::Flee>().each(
        [&](const ecs::Entity, const ecs::Animal&, const ecs::Flee& flee) {
            if (flee.active) {
                ++m_stats.fleeing;
            }
        });
    registry.view<ecs::Animal, ecs::AnimalWander>().each(
        [&](const ecs::Entity, const ecs::Animal&,
            const ecs::AnimalWander& wander) {
            if (wander.retaliating) {
                ++m_stats.retaliating;
            }
        });
    m_pendingSounds.clear();
    m_logTimer += dt;
    if (m_logTimer >= 5.0f) {
        m_logTimer = 0.0f;
        ZL_INFO("animals",
                "动物统计：live={} 已生成={} 恢复={} 种子区块={} 卸载待恢复={} 远端降频={} 逃跑={} 反击={} 听声={} 攻击={}",
                m_stats.live,
                static_cast<unsigned long long>(m_stats.spawned),
                static_cast<unsigned long long>(m_stats.restored),
                m_stats.seededChunks, m_stats.pendingChunks,
                static_cast<unsigned long long>(m_stats.skippedFar),
                m_stats.fleeing, m_stats.retaliating,
                static_cast<unsigned long long>(m_stats.heard),
                static_cast<unsigned long long>(m_stats.attacks));
    }
}
}  // namespace zl::game
