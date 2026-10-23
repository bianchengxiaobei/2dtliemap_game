#pragma once

#include <cstdint>
#include <string_view>

#include "characters/components/CharacterComponents.h"

namespace zl::ecs {
class EcsWorld;
struct Velocity;
}  // namespace zl::ecs

namespace zl::characters {

// M4-T4：Velocity → Facing + AnimState 帧推进。
// M14-T3：按移动/攻击/受击/死亡切片段；片段计时与事件帧由本系统推进。
//
// 朝向取"移动速度在屏幕坐标下的主轴"：
//   屏幕速度 sx = vx - vy, sy = vx + vy（IsoMath 投影的线性部分），
//   |sy| >= |sx| 取上/下，否则取左/右。
// 静止时朝向保持最后值；Idle 片段仍按低帧率呼吸。
class AnimationSystem {
public:
    static constexpr float kMoveThreshold = 0.1f;  // 格/秒：低于此视为静止

    // 片段契约（与 assets/asset_manifest.json 的 animations 段一致并由加载器校验）。
    // eventMask 的 bit i 表示片段内第 i 帧触发一次 event；循环片段每轮重置。
    struct ClipSpec {
        ecs::AnimClip clip = ecs::AnimClip::Legacy;
        const char* name = "legacy";
        std::uint8_t framesPerDir = 2;
        float fps = 8.0f;
        bool loop = true;
        std::uint8_t eventMask = 0;
        ecs::AnimEvent event = ecs::AnimEvent::None;
    };

    void Update(ecs::EcsWorld& world, float dt) const;

    // 世界格速度 → 4 向朝向（纯函数，可单测）
    static ecs::Direction FacingFromVelocity(float vx, float vy);

    // 片段表/名字/解析（纯函数，可单测；AssetProvider 也用它校验 manifest）
    static const ClipSpec& Spec(ecs::AnimClip clip);
    static const char* ClipName(ecs::AnimClip clip);
    static bool ParseClipName(std::string_view name, ecs::AnimClip* clip);
    static ecs::AnimClip SelectClip(const ecs::Velocity* velocity,
                                    const ecs::ActionState* action,
                                    const ecs::Health* health);

    // 推进片段计时/帧/事件；调用方负责在片段切换时重置 frame/time/eventMaskDone。
    static void Advance(ecs::AnimState& anim, const ClipSpec& spec, float dt);
};

}  // namespace zl::characters
