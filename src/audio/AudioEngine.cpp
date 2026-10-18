#include "audio/AudioEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4100 4324)
#endif
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "core/Constants.h"
#include "core/Log.h"

namespace zl::audio {
namespace {

constexpr const char* kModule = "audio";
constexpr ma_uint32 kNullSampleRate = 48000;
constexpr ma_uint32 kNullChannels = 2;
constexpr ma_uint32 kNullReadFrames = 512;

ma_sound_group* GroupPtr(ma_sound_group* master, ma_sound_group* music, ma_sound_group* sfx,
                         ma_sound_group* ambient, AudioGroup group) {
    switch (group) {
        case AudioGroup::Master:
            return master;
        case AudioGroup::Music:
            return music;
        case AudioGroup::SFX:
            return sfx;
        case AudioGroup::Ambient:
            return ambient;
    }
    return sfx;
}

}  // namespace

const char* AudioGroupName(AudioGroup group) {
    switch (group) {
        case AudioGroup::Master:
            return "Master";
        case AudioGroup::Music:
            return "Music";
        case AudioGroup::SFX:
            return "SFX";
        case AudioGroup::Ambient:
            return "Ambient";
    }
    return "?";
}

SpatialGain ComputeSpatial(const glm::vec2& listener, const glm::vec2& source,
                           float referenceDistance, float maxDistance,
                           float wallAttenuation,
                           const core::SoundWalkableQuery& walkable) {
    SpatialGain out;
    const float dx = source.x - listener.x;
    const float dy = source.y - listener.y;
    out.distance = std::sqrt(dx * dx + dy * dy);

    const float ref = std::max(0.0f, referenceDistance);
    const float maxDist = std::max(ref + 0.001f, maxDistance);
    if (out.distance <= ref) {
        out.gain = 1.0f;
    } else if (out.distance >= maxDist) {
        out.gain = 0.0f;
    } else {
        out.gain = 1.0f - (out.distance - ref) / (maxDist - ref);
    }

    out.walls = core::CountBlockingCells(listener.x, listener.y, source.x, source.y, walkable);
    out.gain *= core::WallAttenuationGain(out.walls, wallAttenuation);

    // 等距屏幕横向：x+ 向右下，y+ 向左下；耳机 pan 只看屏幕 x。
    const float screenX = (dx - dy) * (static_cast<float>(core::kTileW) * 0.5f);
    const float screenY = (dx + dy) * (static_cast<float>(core::kTileH) * 0.5f);
    const float screenLen = std::sqrt(screenX * screenX + screenY * screenY);
    out.pan = screenLen > 0.001f ? std::clamp(screenX / screenLen, -1.0f, 1.0f) : 0.0f;
    return out;
}

struct AudioEngine::Impl {
    struct Voice {
        AudioHandle handle;
        ma_sound sound{};
        bool initialized = false;
        bool looping = false;
        bool spatial = false;
        AudioGroup group = AudioGroup::SFX;
        float baseVolume = 1.0f;
        float maxDistance = 0.0f;
        glm::vec2 position{0.0f, 0.0f};
    };

    AudioEngineOptions options;
    ma_engine engine{};
    ma_sound_group master{};
    ma_sound_group music{};
    ma_sound_group sfx{};
    ma_sound_group ambient{};
    bool engineInitialized = false;
    bool groupsInitialized = false;
    bool deviceAvailable = false;
    bool nullDevice = false;

    std::vector<std::unique_ptr<Voice>> voices;
    std::unordered_map<std::string, bool> checkedClips;
    std::vector<float> nullBuffer;
    std::uint64_t nextHandle = 1;
    std::array<float, kAudioGroupCount> groupVolume{0.65f, 0.70f, 0.85f, 0.55f};
    glm::vec2 listener{0.0f, 0.0f};
    core::SoundWalkableQuery walkable;
    Stats stats;
    bool missingLogged = false;

    ma_sound_group* Group(AudioGroup group) {
        if (!groupsInitialized) {
            return nullptr;
        }
        return GroupPtr(&master, &music, &sfx, &ambient, group);
    }

    std::string ClipPath(std::string_view clip) const {
        return (std::filesystem::path(options.root) /
                (std::string(clip) + std::string(".wav")))
            .string();
    }

    void CleanupVoice(std::unique_ptr<Voice>& voice) {
        if (voice == nullptr) {
            return;
        }
        if (voice->initialized) {
            ma_sound_stop(&voice->sound);
            ma_sound_uninit(&voice->sound);
        }
        voice.reset();
    }

    void ApplySpatial(Voice& voice) {
        if (!voice.spatial) {
            return;
        }
        const SpatialGain spatial =
            ComputeSpatial(listener, voice.position, options.referenceDistance,
                           voice.maxDistance > 0.0f ? voice.maxDistance : options.maxDistance,
                           options.wallAttenuation, walkable);
        ma_sound_set_volume(&voice.sound, std::clamp(voice.baseVolume * spatial.gain, 0.0f, 1.0f));
        ma_sound_set_pan(&voice.sound, spatial.pan);
    }

    AudioHandle Start(std::string_view clip, AudioGroup group, float volume, bool loop,
                      bool spatial, const glm::vec2& position, float maxDistance) {
        if (!engineInitialized || !groupsInitialized || clip.empty()) {
            return {};
        }
        if (static_cast<int>(voices.size()) >= std::max(1, options.maxVoices)) {
            ++stats.dropped;
            return {};
        }
        const std::string clipKey(clip);
        const auto checked = checkedClips.find(clipKey);
        if (checked != checkedClips.end() && !checked->second) {
            return {};
        }

        const std::string path = ClipPath(clip);
        auto voice = std::make_unique<Voice>();
        const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION |
                                MA_SOUND_FLAG_NO_PITCH;
        const ma_result result =
            ma_sound_init_from_file(&engine, path.c_str(), flags, Group(group), nullptr,
                                    &voice->sound);
        if (result != MA_SUCCESS) {
            checkedClips[clipKey] = false;
            ++stats.loadFailures;
            if (!missingLogged) {
                ZL_WARN(kModule, "音频文件加载失败：{}（错误 {}；后续同文件不再重试）", path,
                        static_cast<int>(result));
                missingLogged = true;
            }
            return {};
        }
        checkedClips[clipKey] = true;
        voice->initialized = true;
        voice->handle.id = nextHandle++;
        voice->looping = loop;
        voice->spatial = spatial;
        voice->group = group;
        voice->baseVolume = std::clamp(volume, 0.0f, 1.0f);
        voice->maxDistance = maxDistance;
        voice->position = position;
        ma_sound_set_looping(&voice->sound, loop ? MA_TRUE : MA_FALSE);
        if (spatial) {
            ApplySpatial(*voice);
        } else {
            ma_sound_set_volume(&voice->sound, voice->baseVolume);
            ma_sound_set_pan(&voice->sound, 0.0f);
        }
        ma_sound_start(&voice->sound);
        ++stats.played;
        const AudioHandle handle = voice->handle;
        voices.push_back(std::move(voice));
        stats.activeVoices = static_cast<int>(voices.size());
        return handle;
    }

    void RemoveFinished() {
        if (!engineInitialized) {
            return;
        }
        for (auto it = voices.begin(); it != voices.end();) {
            const std::unique_ptr<Voice>& voice = *it;
            if (voice != nullptr && voice->initialized && !voice->looping &&
                ma_sound_at_end(&voice->sound)) {
                CleanupVoice(*it);
                it = voices.erase(it);
                continue;
            }
            ++it;
        }
        stats.activeVoices = static_cast<int>(voices.size());
    }
};

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() { Shutdown(); }

bool AudioEngine::Init(const AudioEngineOptions& options) {
    Shutdown();
    m_impl->options = options;
    m_impl->stats = Stats{};
    m_impl->checkedClips.clear();
    m_impl->missingLogged = false;
    m_impl->groupVolume = {std::clamp(options.masterVolume, 0.0f, 1.0f),
                           std::clamp(options.musicVolume, 0.0f, 1.0f),
                           std::clamp(options.sfxVolume, 0.0f, 1.0f),
                           std::clamp(options.ambientVolume, 0.0f, 1.0f)};
    m_impl->listener = glm::vec2(0.0f, 0.0f);

    if (options.device == "off") {
        ZL_INFO(kModule, "音频已关闭（audio.device=off），静音运行");
        return false;
    }

    ma_engine_config config = ma_engine_config_init();
    m_impl->nullDevice = options.device == "null";
    if (m_impl->nullDevice) {
        config.noDevice = MA_TRUE;
        config.channels = kNullChannels;
        config.sampleRate = kNullSampleRate;
        m_impl->nullBuffer.resize(static_cast<std::size_t>(kNullReadFrames) * kNullChannels);
    }

    const ma_result engineResult = ma_engine_init(&config, &m_impl->engine);
    if (engineResult != MA_SUCCESS) {
        ZL_WARN(kModule, "音频设备初始化失败（错误 {}），静音运行", static_cast<int>(engineResult));
        m_impl->engineInitialized = false;
        return false;
    }
    m_impl->engineInitialized = true;
    m_impl->deviceAvailable = !m_impl->nullDevice;
    m_impl->stats.deviceAvailable = m_impl->deviceAvailable;
    m_impl->stats.nullDevice = m_impl->nullDevice;

    const ma_result masterResult =
        ma_sound_group_init(&m_impl->engine, 0, nullptr, &m_impl->master);
    const ma_result musicResult =
        masterResult == MA_SUCCESS
            ? ma_sound_group_init(&m_impl->engine, 0, &m_impl->master, &m_impl->music)
            : MA_ERROR;
    const ma_result sfxResult =
        masterResult == MA_SUCCESS
            ? ma_sound_group_init(&m_impl->engine, 0, &m_impl->master, &m_impl->sfx)
            : MA_ERROR;
    const ma_result ambientResult =
        masterResult == MA_SUCCESS
            ? ma_sound_group_init(&m_impl->engine, 0, &m_impl->master, &m_impl->ambient)
            : MA_ERROR;
    if (masterResult != MA_SUCCESS || musicResult != MA_SUCCESS || sfxResult != MA_SUCCESS ||
        ambientResult != MA_SUCCESS) {
        ZL_WARN(kModule, "音频音量组初始化失败，静音运行");
        ma_engine_uninit(&m_impl->engine);
        m_impl->engineInitialized = false;
        return false;
    }
    m_impl->groupsInitialized = true;
    for (int i = 0; i < kAudioGroupCount; ++i) {
        SetGroupVolume(static_cast<AudioGroup>(i),
                       m_impl->groupVolume[static_cast<std::size_t>(i)]);
    }

    ZL_INFO(kModule, "音频引擎就绪（{}，{} 个音量组）",
            m_impl->nullDevice ? "null device（静音）" : "系统默认设备", kAudioGroupCount);
    return true;
}

void AudioEngine::Shutdown() {
    if (m_impl == nullptr) {
        return;
    }
    for (auto& voice : m_impl->voices) {
        m_impl->CleanupVoice(voice);
    }
    m_impl->voices.clear();
    m_impl->stats.activeVoices = 0;
    if (m_impl->groupsInitialized) {
        ma_sound_group_uninit(&m_impl->ambient);
        ma_sound_group_uninit(&m_impl->sfx);
        ma_sound_group_uninit(&m_impl->music);
        ma_sound_group_uninit(&m_impl->master);
        m_impl->groupsInitialized = false;
    }
    if (m_impl->engineInitialized) {
        ma_engine_uninit(&m_impl->engine);
        m_impl->engineInitialized = false;
    }
    m_impl->deviceAvailable = false;
    m_impl->nullDevice = false;
    m_impl->stats.deviceAvailable = false;
    m_impl->stats.nullDevice = false;
}

bool AudioEngine::IsReady() const { return m_impl != nullptr && m_impl->engineInitialized; }

bool AudioEngine::IsDeviceAvailable() const {
    return m_impl != nullptr && m_impl->deviceAvailable;
}

void AudioEngine::Update(float dt) {
    if (m_impl == nullptr || !m_impl->engineInitialized) {
        return;
    }
    (void)dt;
    if (m_impl->nullDevice) {
        ma_uint64 read = 0;
        ma_engine_read_pcm_frames(&m_impl->engine, m_impl->nullBuffer.data(),
                                  kNullReadFrames, &read);
    }
    for (auto& voice : m_impl->voices) {
        if (voice != nullptr && voice->initialized) {
            m_impl->ApplySpatial(*voice);
        }
    }
    m_impl->RemoveFinished();
}

AudioHandle AudioEngine::Play2D(std::string_view clip, AudioGroup group, float volume, bool loop) {
    if (m_impl == nullptr) {
        return {};
    }
    return m_impl->Start(clip, group, volume, loop, false, glm::vec2(0.0f, 0.0f), 0.0f);
}

AudioHandle AudioEngine::Play3D(std::string_view clip, const glm::vec2& worldPos,
                                AudioGroup group, float volume, bool loop, float maxDistance) {
    if (m_impl == nullptr) {
        return {};
    }
    return m_impl->Start(clip, group, volume, loop, true, worldPos, maxDistance);
}

void AudioEngine::Stop(AudioHandle handle) {
    if (m_impl == nullptr || !handle.Valid()) {
        return;
    }
    for (auto it = m_impl->voices.begin(); it != m_impl->voices.end(); ++it) {
        if ((*it)->handle.id == handle.id) {
            m_impl->CleanupVoice(*it);
            m_impl->voices.erase(it);
            m_impl->stats.activeVoices = static_cast<int>(m_impl->voices.size());
            return;
        }
    }
}

void AudioEngine::StopAll() {
    if (m_impl == nullptr) {
        return;
    }
    for (auto& voice : m_impl->voices) {
        m_impl->CleanupVoice(voice);
    }
    m_impl->voices.clear();
    m_impl->stats.activeVoices = 0;
}

void AudioEngine::SetGroupVolume(AudioGroup group, float volume) {
    if (m_impl == nullptr) {
        return;
    }
    const int index = static_cast<int>(group);
    if (index < 0 || index >= kAudioGroupCount) {
        return;
    }
    const float clamped = std::clamp(volume, 0.0f, 1.0f);
    m_impl->groupVolume[static_cast<std::size_t>(index)] = clamped;
    if (!m_impl->groupsInitialized) {
        return;
    }
    switch (group) {
        case AudioGroup::Master:
            ma_sound_group_set_volume(&m_impl->master, clamped);
            break;
        case AudioGroup::Music:
            ma_sound_group_set_volume(&m_impl->music, clamped);
            break;
        case AudioGroup::SFX:
            ma_sound_group_set_volume(&m_impl->sfx, clamped);
            break;
        case AudioGroup::Ambient:
            ma_sound_group_set_volume(&m_impl->ambient, clamped);
            break;
    }
}

float AudioEngine::GroupVolume(AudioGroup group) const {
    if (m_impl == nullptr) {
        return 0.0f;
    }
    const int index = static_cast<int>(group);
    if (index < 0 || index >= kAudioGroupCount) {
        return 0.0f;
    }
    return m_impl->groupVolume[static_cast<std::size_t>(index)];
}

void AudioEngine::SetListener(const glm::vec2& listener) {
    if (m_impl != nullptr) {
        m_impl->listener = listener;
    }
}

const glm::vec2& AudioEngine::Listener() const {
    static const glm::vec2 kZero{0.0f, 0.0f};
    return m_impl != nullptr ? m_impl->listener : kZero;
}

void AudioEngine::SetWalkable(core::SoundWalkableQuery walkable) {
    if (m_impl != nullptr) {
        m_impl->walkable = std::move(walkable);
    }
}

const AudioEngineOptions& AudioEngine::Options() const {
    static const AudioEngineOptions kDefaults{};
    return m_impl != nullptr ? m_impl->options : kDefaults;
}

const AudioEngine::Stats& AudioEngine::GetStats() const {
    static const Stats kEmpty{};
    return m_impl != nullptr ? m_impl->stats : kEmpty;
}

}  // namespace zl::audio
