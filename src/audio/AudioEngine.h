#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "core/SoundPropagation.h"

namespace zl::audio {

// M11-T1：音量组。Master 是 Music/SFX/Ambient 的父组。
enum class AudioGroup : std::uint8_t {
    Master = 0,
    Music = 1,
    SFX = 2,
    Ambient = 3,
};
inline constexpr int kAudioGroupCount = 4;

const char* AudioGroupName(AudioGroup group);

struct AudioHandle {
    std::uint64_t id = 0;
    bool Valid() const { return id != 0; }
    explicit operator bool() const { return Valid(); }
};

struct AudioEngineOptions {
    std::string root = "assets/audio";
    // "default"：系统默认播放设备；"null"：miniaudio 无设备降级（测试/无声冒烟）；
    // "off"：完全不创建音频引擎。设备不可用时一律静音运行，不阻断游戏。
    std::string device = "default";
    int maxVoices = 64;
    float masterVolume = 0.65f;
    float musicVolume = 0.70f;
    float sfxVolume = 0.85f;
    float ambientVolume = 0.55f;
    float referenceDistance = 2.0f;
    float maxDistance = 48.0f;
    float wallAttenuation = 0.6f;  // 与 perception.wallAttenuation 同一参数
};

// 距离 + 屏幕横向声像 + 隔墙衰减结果（纯函数，便于单测）。
struct SpatialGain {
    float gain = 0.0f;   // 已包含距离与隔墙衰减
    float pan = 0.0f;    // -1 左 .. +1 右（耳机侧向可辨）
    float distance = 0.0f;
    int walls = 0;
};

SpatialGain ComputeSpatial(const glm::vec2& listener, const glm::vec2& source,
                           float referenceDistance, float maxDistance,
                           float wallAttenuation,
                           const core::SoundWalkableQuery& walkable);

// 单线程使用（主线程提交 + 更新；miniaudio 内部音频线程只读取声音节点）。
// 设备不可用时所有 Play 返回空 handle，游戏逻辑无需分支。
class AudioEngine {
public:
    struct Stats {
        int activeVoices = 0;
        std::uint64_t played = 0;
        std::uint64_t dropped = 0;
        std::uint64_t loadFailures = 0;
        bool deviceAvailable = false;
        bool nullDevice = false;
    };

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // 返回 false 仅表示 device=off 或 miniaudio 初始化失败；调用方仍可安全调用其余接口。
    bool Init(const AudioEngineOptions& options);
    void Shutdown();
    bool IsReady() const;
    bool IsDeviceAvailable() const;

    void Update(float dt);

    AudioHandle Play2D(std::string_view clip, AudioGroup group = AudioGroup::SFX,
                       float volume = 1.0f, bool loop = false);
    AudioHandle Play3D(std::string_view clip, const glm::vec2& worldPos,
                       AudioGroup group = AudioGroup::SFX, float volume = 1.0f,
                       bool loop = false, float maxDistance = 0.0f,
                       bool allowPitch = false);
    void Stop(AudioHandle handle);
    void StopAll();

    // 长音（如载具引擎）运行中更新；allowPitch=false 的声音忽略 SetPitch。
    void SetVolume(AudioHandle handle, float volume);
    void SetPitch(AudioHandle handle, float pitch);
    void SetPosition(AudioHandle handle, const glm::vec2& worldPos);

    void SetGroupVolume(AudioGroup group, float volume);
    float GroupVolume(AudioGroup group) const;

    // 听者 = 玩家相机中心（世界格坐标）；Update 时重新计算所有 3D 声源的距离/声像。
    void SetListener(const glm::vec2& listener);
    const glm::vec2& Listener() const;

    // AI 与音频共用的阻挡查询；未设置时不做隔墙衰减。
    void SetWalkable(core::SoundWalkableQuery walkable);

    const AudioEngineOptions& Options() const;
    const Stats& GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace zl::audio
