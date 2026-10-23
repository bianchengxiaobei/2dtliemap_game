#include "game/AnimationManifest.h"

#include <cmath>
#include <string>
#include <utility>

#include "characters/systems/AnimationSystem.h"

namespace zl::game {
namespace {

void SetError(std::string* error, const std::string& text) {
    if (error != nullptr) {
        *error = text;
    }
}

bool EventFromName(std::string_view name, ecs::AnimEvent* event) {
    if (event == nullptr) {
        return false;
    }
    if (name == "footstep") {
        *event = ecs::AnimEvent::Footstep;
        return true;
    }
    if (name == "hit") {
        *event = ecs::AnimEvent::Hit;
        return true;
    }
    return false;
}

}  // namespace

bool ParseAnimationManifest(const nlohmann::json& binding, AnimationManifest* manifest,
                            std::string* error) {
    if (manifest == nullptr) {
        SetError(error, "动画 manifest 输出为空");
        return false;
    }
    *manifest = AnimationManifest{};

    const auto animationsIt = binding.find("animations");
    if (animationsIt == binding.end()) {
        return true;  // 旧 provider：没有 v2，按旧 8 帧回退
    }
    if (!animationsIt->is_object()) {
        SetError(error, "manifest.animations 必须是对象");
        return false;
    }

    for (const auto& [name, value] : animationsIt->items()) {
        ecs::AnimClip clip = ecs::AnimClip::Legacy;
        if (!characters::AnimationSystem::ParseClipName(name, &clip) ||
            clip == ecs::AnimClip::Legacy) {
            SetError(error, "未知动画片段: " + name);
            return false;
        }
        const auto index = static_cast<std::size_t>(clip);
        if (manifest->present[index]) {
            SetError(error, "重复动画片段: " + name);
            return false;
        }
        if (!value.is_object()) {
            SetError(error, "动画片段必须是对象: " + name);
            return false;
        }

        AnimationManifestClip parsed;
        parsed.clip = clip;
        const auto framesPerDirIt = value.find("framesPerDir");
        if (framesPerDirIt == value.end() || !framesPerDirIt->is_number_integer()) {
            SetError(error, "动画片段缺少 framesPerDir: " + name);
            return false;
        }
        const int framesPerDir = framesPerDirIt->get<int>();
        if (framesPerDir < 1 || framesPerDir > 8) {
            SetError(error, "动画片段 framesPerDir 越界: " + name);
            return false;
        }
        parsed.framesPerDir = static_cast<std::uint8_t>(framesPerDir);

        const auto fpsIt = value.find("fps");
        if (fpsIt == value.end() || !fpsIt->is_number()) {
            SetError(error, "动画片段缺少 fps: " + name);
            return false;
        }
        parsed.fps = fpsIt->get<float>();
        if (!std::isfinite(parsed.fps) || parsed.fps <= 0.0f) {
            SetError(error, "动画片段 fps 非法: " + name);
            return false;
        }

        const auto loopIt = value.find("loop");
        if (loopIt == value.end() || !loopIt->is_boolean()) {
            SetError(error, "动画片段缺少 loop: " + name);
            return false;
        }
        parsed.loop = loopIt->get<bool>();

        const auto framesIt = value.find("frames");
        if (framesIt == value.end() || !framesIt->is_array() ||
            framesIt->size() != static_cast<std::size_t>(framesPerDir) * 4u) {
            SetError(error, "动画片段 frames 数量必须为 framesPerDir * 4: " + name);
            return false;
        }
        parsed.frames.reserve(framesIt->size());
        for (const auto& frame : *framesIt) {
            if (!frame.is_string() || frame.get<std::string>().empty()) {
                SetError(error, "动画片段帧名非法: " + name);
                return false;
            }
            parsed.frames.push_back(frame.get<std::string>());
        }

        const auto eventsIt = value.find("events");
        if (eventsIt != value.end()) {
            if (!eventsIt->is_array()) {
                SetError(error, "动画片段 events 必须是数组: " + name);
                return false;
            }
            ecs::AnimEvent eventType = ecs::AnimEvent::None;
            for (const auto& event : *eventsIt) {
                if (!event.is_object()) {
                    SetError(error, "动画事件必须是对象: " + name);
                    return false;
                }
                const auto frameIt = event.find("frame");
                const auto typeIt = event.find("type");
                if (frameIt == event.end() || !frameIt->is_number_integer() ||
                    typeIt == event.end() || !typeIt->is_string()) {
                    SetError(error, "动画事件缺少 frame/type: " + name);
                    return false;
                }
                const int frame = frameIt->get<int>();
                if (frame < 0 || frame >= framesPerDir) {
                    SetError(error, "动画事件帧越界: " + name);
                    return false;
                }
                ecs::AnimEvent parsedEvent = ecs::AnimEvent::None;
                if (!EventFromName(typeIt->get<std::string>(), &parsedEvent)) {
                    SetError(error, "未知动画事件类型: " + name);
                    return false;
                }
                if (eventType != ecs::AnimEvent::None && eventType != parsedEvent) {
                    SetError(error, "同一片段不能混合多种动画事件: " + name);
                    return false;
                }
                eventType = parsedEvent;
                const std::uint8_t mask =
                    static_cast<std::uint8_t>(1u << static_cast<unsigned>(frame));
                if ((parsed.eventMask & mask) != 0) {
                    SetError(error, "动画事件帧重复: " + name);
                    return false;
                }
                parsed.eventMask = static_cast<std::uint8_t>(parsed.eventMask | mask);
            }
            parsed.event = eventType;
        }

        const auto& canonical = characters::AnimationSystem::Spec(clip);
        if (parsed.framesPerDir != canonical.framesPerDir ||
            std::fabs(parsed.fps - canonical.fps) > 0.0001f ||
            parsed.loop != canonical.loop || parsed.eventMask != canonical.eventMask ||
            parsed.event != canonical.event) {
            SetError(error, "动画片段元数据与运行时契约不一致: " + name);
            return false;
        }

        manifest->clips[index] = std::move(parsed);
        manifest->present[index] = true;
    }

    for (std::uint8_t i = 1; i < static_cast<std::uint8_t>(ecs::AnimClip::Count); ++i) {
        if (!manifest->present[i]) {
            SetError(error, "动画 v2 缺少片段: " +
                                 std::string(characters::AnimationSystem::ClipName(
                                     static_cast<ecs::AnimClip>(i))));
            return false;
        }
    }
    return true;
}

}  // namespace zl::game
