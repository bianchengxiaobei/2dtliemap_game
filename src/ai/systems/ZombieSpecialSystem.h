#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "core/SoundEvent.h"
#include "ecs/EcsWorld.h"

namespace zl::ai {

// M20-F3：特殊僵尸行为（嚎叫者）。
// 系统只读写 ECS 与可选的 SoundEvent sink；声音仍经 OnSound 进入感知/音频，
// 同时直接给半径内存活僵尸一个“听到玩家”的可疑点，保证引怪可感知、可测试。
struct ZombieSpecialParams {
    float howlRange = 12.0f;
    float howlAbortRange = 18.0f;
    float howlWindupSeconds = 1.2f;
    float howlCooldownSeconds = 9.0f;
    float howlRadius = 24.0f;
};

class ZombieSpecialSystem {
public:
    using SoundSink = std::function<void(const core::SoundEvent&)>;

    struct Stats {
        int howlers = 0;
        int windups = 0;
        int howls = 0;
        int attracted = 0;
        int interrupted = 0;
    };

    explicit ZombieSpecialSystem(ZombieSpecialParams params = {},
                                 SoundSink soundSink = {})
        : m_params(params), m_soundSink(std::move(soundSink)) {}

    void Update(ecs::EcsWorld& world, float dt);
    const Stats& GetStats() const { return m_stats; }
    const ZombieSpecialParams& Params() const { return m_params; }

private:
    void CancelWindup(ecs::EcsWorld& world, ecs::Entity entity);
    void StartHowl(ecs::EcsWorld& world, ecs::Entity entity,
                   float x, float y, float playerX, float playerY);
    void FinishHowl(ecs::EcsWorld& world, ecs::Entity entity, float x, float y,
                    std::uint64_t playerId, float playerX, float playerY);
    void AttractNearby(ecs::EcsWorld& world, ecs::Entity source,
                       std::uint64_t playerId, float playerX, float playerY,
                       float x, float y);

    ZombieSpecialParams m_params;
    SoundSink m_soundSink;
    Stats m_stats;
};

}  // namespace zl::ai
