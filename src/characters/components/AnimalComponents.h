#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zl::ecs {

enum class AnimalKind : std::uint8_t {
    Passive = 0,
    Dangerous = 1,
};

struct Animal {
    std::string speciesId;
    AnimalKind kind = AnimalKind::Passive;
};

struct Flee {
    bool active = false;
    float remaining = 0.0f;
    float directionX = 0.0f;
    float directionY = 0.0f;
    float threatX = 0.0f;
    float threatY = 0.0f;
    float alertness = 0.0f;
};

struct AnimalCorpse {
    std::string speciesId;
    bool harvested = false;
};

struct AnimalWander {
    bool hasGoal = false;
    float goalX = 0.0f;
    float goalY = 0.0f;
    float idleRemaining = 0.0f;
    bool retaliating = false;
    float retaliateRemaining = 0.0f;
    float attackCooldown = 0.0f;
    float threatX = 0.0f;
    float threatY = 0.0f;
    std::uint32_t lastHitSerial = 0;
};

inline constexpr std::uint64_t kAnimalWorldStateStableId = 0x4D323342414E4D53ull;
inline constexpr std::size_t kMaxAnimalWorldStateChunks = 4096;
inline constexpr std::size_t kMaxAnimalWorldStateAnimalsPerChunk = 16;
inline constexpr std::size_t kMaxAnimalWorldStateAnimals = 2048;

struct AnimalSavedRecord {
    std::string speciesId;
    float x = 0.0f;
    float y = 0.0f;
    float health = 1.0f;
    bool fleeing = false;
    float fleeRemaining = 0.0f;
    float fleeDirX = 0.0f;
    float fleeDirY = 0.0f;
    float threatX = 0.0f;
    float threatY = 0.0f;
    float alertness = 0.0f;
};

struct AnimalSavedChunk {
    int chunkX = 0;
    int chunkY = 0;
    std::vector<AnimalSavedRecord> animals;
};

struct AnimalWorldState {
    std::vector<AnimalSavedChunk> chunks;
};

}  // namespace zl::ecs
