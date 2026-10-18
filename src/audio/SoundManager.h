#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "audio/AudioEngine.h"
#include "core/EventBus.h"
#include "core/SoundEvent.h"

namespace zl::ecs {
class EcsWorld;
}

namespace zl::audio {

struct SoundManagerOptions {
    AudioEngineOptions engine;
    float growlRadius = 24.0f;
    float growlMinInterval = 1.1f;
    float growlMaxInterval = 4.2f;
    float growlVolume = 0.9f;
    float ambienceVolume = 0.55f;
};

// M11-T1~T4：游戏音频门面。
//  - 世界声音只发 OnSound（GameApp 的感知订阅与这里的声音订阅同时消费）；
//  - UI/拾取为 2D 直接播放；
//  - 环境循环按 GameTime 昼夜切换；
//  - 僵尸低吼按听者附近存活僵尸数量调节间隔，音量为 3D 距离衰减；
//  - 设备不可用时全部 no-op，不影响游戏。
class SoundManager {
public:
    struct Stats {
        std::uint64_t eventsPlayed = 0;
        std::uint64_t footsteps = 0;
        std::uint64_t gunshots = 0;
        std::uint64_t uiClicks = 0;
        std::uint64_t pickups = 0;
        std::uint64_t growls = 0;
        std::uint64_t ambienceStarts = 0;
        std::uint64_t musicStarts = 0;
        int activeVoices = 0;
        bool audioReady = false;
        bool deviceAvailable = false;
    };

    SoundManager();
    ~SoundManager();

    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    bool Init(core::EventBus& events, const SoundManagerOptions& options);
    void Shutdown();

    // 主线程每帧调用：listener=玩家相机中心；night 来自 GameTime；gameplay=false 时
    // 不产生僵尸低吼（菜单/暂停）。
    void Update(float dt, const ecs::EcsWorld* world, const glm::vec2& listener, bool night,
                bool gameplay);

    // 世界声音入口：写入 clip 后经事件总线 OnSound 投递（音频与 AI 同时消费）。
    void EmitWorldSound(core::SoundEvent sound, std::string clip = {});

    void PlayUiClick();
    void PlayPickup();
    AudioEngine& Engine() { return m_engine; }
    const AudioEngine& Engine() const { return m_engine; }

    bool IsReady() const;
    bool IsDeviceAvailable() const;
    const Stats& GetStats() const;

private:
    void OnSound(const core::SoundEvent& sound);
    void OnItemPickup();
    void UpdateMusic();
    void UpdateAmbience(bool night);
    void UpdateGrowls(float dt, const ecs::EcsWorld* world, bool gameplay);

    core::EventBus* m_events = nullptr;
    AudioEngine m_engine;
    SoundManagerOptions m_options;
    core::EventBus::SubscriptionId m_soundSubscription = 0;
    core::EventBus::SubscriptionId m_pickupSubscription = 0;
    bool m_musicStarted = false;
    AudioHandle m_musicHandle;
    AudioHandle m_ambientHandle;
    int m_ambientNight = -1;  // -1 未启动；0 白天；1 夜晚
    float m_growlTimer = 0.0f;
    std::uint64_t m_growlSerial = 0;
    Stats m_stats;
};

}  // namespace zl::audio
