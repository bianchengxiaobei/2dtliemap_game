#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "characters/components/CharacterComponents.h"

namespace zl::game {

// M14-T2：manifest 中角色 provider 的动画 v2 契约。
// frames 顺序固定为 dir * framesPerDir + frame，dir: down/left/right/up。
// 片段元数据（帧数/fps/循环/事件帧）必须与 AnimationSystem::Spec 一致；
// 加载器在启动时校验，避免“图集更新了但运行时还是旧节奏”。
struct AnimationManifestClip {
    ecs::AnimClip clip = ecs::AnimClip::Legacy;
    std::uint8_t framesPerDir = 0;
    float fps = 0.0f;
    bool loop = true;
    std::uint8_t eventMask = 0;
    ecs::AnimEvent event = ecs::AnimEvent::None;
    std::vector<std::string> frames;
};

struct AnimationManifest {
    std::array<AnimationManifestClip, static_cast<std::size_t>(ecs::AnimClip::Count)> clips;
    std::array<bool, static_cast<std::size_t>(ecs::AnimClip::Count)> present{};

    bool Empty() const {
        for (const bool value : present) {
            if (value) {
                return false;
            }
        }
        return true;
    }

    const AnimationManifestClip* Find(ecs::AnimClip clip) const {
        const auto index = static_cast<std::size_t>(clip);
        return index < clips.size() && present[index] ? &clips[index] : nullptr;
    }
};

// binding = manifest 的某个 provider 条目（含 atlas/frames，可选 animations）。
// animations 缺失表示继续使用旧 8 帧；出现时必须完整包含 idle/walk/attack/hit/death。
bool ParseAnimationManifest(const nlohmann::json& binding, AnimationManifest* manifest,
                            std::string* error = nullptr);

}  // namespace zl::game
