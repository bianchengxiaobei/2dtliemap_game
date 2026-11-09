#include "audio/SoundManager.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "ai/components/AiComponents.h"
#include "audio/SoundIds.h"
#include "characters/components/CharacterComponents.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"

namespace zl::audio {
namespace {

constexpr const char* kModule = "audio";

const char* DefaultClip(core::SoundType type) {
    switch (type) {
        case core::SoundType::Gunshot:
            return kGunshot;
        case core::SoundType::Impact:
            return kImpact;
        case core::SoundType::Footstep:
            return kFootstepGrass;
        case core::SoundType::ZombieGrowl:
            return kZombieGrowl;
        case core::SoundType::Ui:
            return kUiClick;
        case core::SoundType::Vehicle:
        case core::SoundType::Generic:
        default:
            return kImpact;
    }
}

float DefaultMaxDistance(core::SoundType type) {
    switch (type) {
        case core::SoundType::Gunshot:
            return 52.0f;
        case core::SoundType::Impact:
            return 22.0f;
        case core::SoundType::Footstep:
            return 12.0f;
        case core::SoundType::ZombieGrowl:
            return 30.0f;
        case core::SoundType::Vehicle:
            return 26.0f;
        case core::SoundType::Generic:
        case core::SoundType::Ui:
        default:
            return 28.0f;
    }
}

float DefaultVolume(core::SoundType type) {
    switch (type) {
        case core::SoundType::Gunshot:
            return 1.0f;
        case core::SoundType::Impact:
            return 0.8f;
        case core::SoundType::Footstep:
            return 0.45f;
        case core::SoundType::ZombieGrowl:
            return 0.8f;
        case core::SoundType::Ui:
            return 0.55f;
        case core::SoundType::Vehicle:
            return 0.45f;
        case core::SoundType::Generic:
        default:
            return 0.7f;
    }
}

}  // namespace

SoundManager::SoundManager() = default;

SoundManager::~SoundManager() { Shutdown(); }

bool SoundManager::Init(core::EventBus& events, const SoundManagerOptions& options) {
    Shutdown();
    m_events = &events;
    m_options = options;
    m_stats = Stats{};
    m_engine.Init(options.engine);
    m_stats.audioReady = m_engine.IsReady();
    m_stats.deviceAvailable = m_engine.IsDeviceAvailable();

    if (m_events != nullptr) {
        m_soundSubscription =
            m_events->Subscribe(core::kOnSound, [this](const core::Event& event) {
                if (const auto* sound = std::get_if<core::SoundEvent>(&event.payload)) {
                    OnSound(*sound);
                }
            });
        m_pickupSubscription =
            m_events->Subscribe(core::kOnItemPickup, [this](const core::Event&) {
                OnItemPickup();
            });
    }
    m_ambientNight = -1;
    m_growlTimer = 1.0f;
    ZL_INFO(kModule, "声音管理就绪（设备={}，OnSound/OnItemPickup 已接线）",
            m_engine.IsDeviceAvailable() ? "可用" : "静音降级");
    return true;
}

void SoundManager::Shutdown() {
    if (m_events != nullptr) {
        if (m_soundSubscription != core::EventBus::kInvalidSubscription) {
            m_events->Unsubscribe(m_soundSubscription);
        }
        if (m_pickupSubscription != core::EventBus::kInvalidSubscription) {
            m_events->Unsubscribe(m_pickupSubscription);
        }
    }
    m_soundSubscription = core::EventBus::kInvalidSubscription;
    m_pickupSubscription = core::EventBus::kInvalidSubscription;
    m_ambientHandle = {};
    m_musicHandle = {};
    m_ambientNight = -1;
    m_musicStarted = false;
    m_engine.Shutdown();
    m_events = nullptr;
    m_stats = Stats{};
}

void SoundManager::Update(float dt, const ecs::EcsWorld* world, const glm::vec2& listener,
                          bool night, bool gameplay) {
    m_engine.SetListener(listener);
    m_engine.Update(dt);
    UpdateMusic();
    UpdateAmbience(night);
    UpdateGrowls(dt, world, gameplay);
    m_stats.activeVoices = m_engine.GetStats().activeVoices;
    m_stats.audioReady = m_engine.IsReady();
    m_stats.deviceAvailable = m_engine.IsDeviceAvailable();
}

void SoundManager::EmitWorldSound(core::SoundEvent sound, std::string clip) {
    if (m_events == nullptr) {
        return;
    }
    if (!clip.empty()) {
        sound.clip = std::move(clip);
    }
    m_events->EmitQueued(core::kOnSound, std::move(sound));
}

void SoundManager::PlayUiClick() {
    ++m_stats.uiClicks;
    if (!m_engine.IsReady()) {
        return;
    }
    m_engine.Play2D(kUiClick, AudioGroup::SFX, 0.55f);
}

void SoundManager::PlayPickup() {
    ++m_stats.pickups;
    if (!m_engine.IsReady()) {
        return;
    }
    m_engine.Play2D(kPickup, AudioGroup::SFX, 0.75f);
}

bool SoundManager::IsReady() const { return m_engine.IsReady(); }

bool SoundManager::IsDeviceAvailable() const { return m_engine.IsDeviceAvailable(); }

const SoundManager::Stats& SoundManager::GetStats() const { return m_stats; }

void SoundManager::OnSound(const core::SoundEvent& sound) {
    if (!m_engine.IsReady()) {
        return;
    }
    std::string clip = sound.clip;
    if (clip.empty()) {
        const char* fallback = DefaultClip(sound.type);
        if (fallback == nullptr) {
            return;
        }
        clip = fallback;
    }

    if (sound.type == core::SoundType::Ui) {
        if (m_engine.Play2D(clip, AudioGroup::SFX, DefaultVolume(sound.type)).Valid()) {
            ++m_stats.eventsPlayed;
        }
        return;
    }

    const AudioHandle handle =
        m_engine.Play3D(clip, glm::vec2(sound.x, sound.y), AudioGroup::SFX,
                        DefaultVolume(sound.type), false, DefaultMaxDistance(sound.type));
    if (handle.Valid()) {
        ++m_stats.eventsPlayed;
        if (sound.type == core::SoundType::Footstep) {
            ++m_stats.footsteps;
        } else if (sound.type == core::SoundType::Gunshot) {
            ++m_stats.gunshots;
        }
    }
}

void SoundManager::OnItemPickup() {
    PlayPickup();
}

void SoundManager::UpdateMusic() {
    if (!m_engine.IsReady() || m_musicStarted) {
        return;
    }
    m_musicStarted = true;
    m_musicHandle = m_engine.Play2D(kMusicCalm, AudioGroup::Music, 0.32f, true);
    if (m_musicHandle.Valid()) {
        ++m_stats.musicStarts;
        ZL_INFO(kModule, "音乐循环启动（Music 音量组）");
    }
}

void SoundManager::UpdateAmbience(bool night) {
    if (!m_engine.IsReady()) {
        return;
    }
    const int nightFlag = night ? 1 : 0;
    if (m_ambientNight == nightFlag && m_ambientHandle.Valid()) {
        return;
    }
    if (m_ambientHandle.Valid()) {
        m_engine.Stop(m_ambientHandle);
    }
    m_ambientNight = nightFlag;
    m_ambientHandle =
        m_engine.Play2D(night ? kAmbientNight : kAmbientDay, AudioGroup::Ambient,
                        m_options.ambienceVolume, true);
    if (m_ambientHandle.Valid()) {
        ++m_stats.ambienceStarts;
        ZL_INFO(kModule, "环境循环切换：{}", night ? "夜晚" : "白天");
    }
}

void SoundManager::UpdateGrowls(float dt, const ecs::EcsWorld* world, bool gameplay) {
    if (!gameplay || world == nullptr || !m_engine.IsReady()) {
        return;
    }
    m_growlTimer -= dt;
    if (m_growlTimer > 0.0f) {
        return;
    }

    float nearestDistSq = m_options.growlRadius * m_options.growlRadius;
    bool found = false;
    glm::vec2 nearest{0.0f, 0.0f};
    int nearby = 0;
    const glm::vec2& listener = m_engine.Listener();
    auto& registry = world->Registry();
    registry.view<ecs::AiState, ecs::Position>().each(
        [&](const entt::entity entity, const ecs::AiState& ai, const ecs::Position& position) {
            if (ai.status == ecs::AiStatus::Dead || registry.any_of<ecs::Dying>(entity)) {
                return;
            }
            if (registry.all_of<ecs::PlayerTag>(entity)) {
                return;
            }
            if (const auto* health = registry.try_get<ecs::Health>(entity);
                health != nullptr && health->current <= 0.0f) {
                return;
            }
            const float dx = position.x - listener.x;
            const float dy = position.y - listener.y;
            const float distSq = dx * dx + dy * dy;
            if (distSq > m_options.growlRadius * m_options.growlRadius) {
                return;
            }
            ++nearby;
            if (distSq <= nearestDistSq) {
                nearestDistSq = distSq;
                nearest = {position.x, position.y};
                found = true;
            }
        });

    if (found) {
        m_engine.Play3D(kZombieGrowl, nearest, AudioGroup::SFX, m_options.growlVolume, false,
                        m_options.growlRadius + 6.0f);
        ++m_stats.growls;
    }

    const float richness = std::min(1.0f, static_cast<float>(nearby) / 8.0f);
    const float base =
        m_options.growlMaxInterval -
        (m_options.growlMaxInterval - m_options.growlMinInterval) * richness;
    const float jitter = static_cast<float>((m_growlSerial++ % 5)) * 0.08f;
    m_growlTimer = std::max(0.35f, base + jitter);
}

}  // namespace zl::audio
