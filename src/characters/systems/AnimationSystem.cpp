#include "characters/systems/AnimationSystem.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <entt/entt.hpp>

#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"

namespace zl::characters {
namespace {

constexpr std::uint8_t BitForFrame(std::uint8_t frame) {
    return static_cast<std::uint8_t>(1u << frame);
}

}  // namespace

ecs::Direction AnimationSystem::FacingFromVelocity(float vx, float vy) {
    const float sx = vx - vy;  // 屏幕 x 分量（线性部分，比例无关）
    const float sy = vx + vy;  // 屏幕 y 分量
    if (std::fabs(sy) >= std::fabs(sx)) {
        return sy < 0.0f ? ecs::Direction::Up : ecs::Direction::Down;
    }
    return sx < 0.0f ? ecs::Direction::Left : ecs::Direction::Right;
}

const AnimationSystem::ClipSpec& AnimationSystem::Spec(ecs::AnimClip clip) {
    static const ClipSpec kSpecs[] = {
        {ecs::AnimClip::Legacy, "legacy", 2, 8.0f, true, 0, ecs::AnimEvent::None},
        {ecs::AnimClip::Idle, "idle", 2, 3.0f, true, 0, ecs::AnimEvent::None},
        // 走路沿用旧 4 向 × 2 帧；事件帧 1 每轮触发一次脚步。
        {ecs::AnimClip::Walk, "walk", 2, 5.0f, true, 0x02u, ecs::AnimEvent::Footstep},
        // 攻击 3 帧；第 1 帧是挥击命中/音效事件。
        {ecs::AnimClip::Attack, "attack", 3, 10.0f, false, 0x02u, ecs::AnimEvent::Hit},
        {ecs::AnimClip::Hit, "hit", 1, 8.0f, false, 0, ecs::AnimEvent::None},
        {ecs::AnimClip::Death, "death", 2, 6.0f, false, 0, ecs::AnimEvent::None},
    };
    const auto index = static_cast<std::size_t>(clip);
    if (index >= std::size(kSpecs)) {
        return kSpecs[0];
    }
    return kSpecs[index];
}

const char* AnimationSystem::ClipName(ecs::AnimClip clip) { return Spec(clip).name; }

bool AnimationSystem::ParseClipName(std::string_view name, ecs::AnimClip* clip) {
    if (clip == nullptr) {
        return false;
    }
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(ecs::AnimClip::Count); ++i) {
        const auto value = static_cast<ecs::AnimClip>(i);
        if (name == Spec(value).name) {
            *clip = value;
            return true;
        }
    }
    return false;
}

ecs::AnimClip AnimationSystem::SelectClip(const ecs::Velocity* velocity,
                                          const ecs::ActionState* action,
                                          const ecs::Health* health) {
    if (health != nullptr && health->current <= 0.0f) {
        return ecs::AnimClip::Death;
    }
    if (action != nullptr) {
        switch (action->action) {
            case ecs::ActorAction::Dead:
                return ecs::AnimClip::Death;
            case ecs::ActorAction::Attacking:
            case ecs::ActorAction::Howling:
                return ecs::AnimClip::Attack;
            case ecs::ActorAction::Hit:
                return ecs::AnimClip::Hit;
            default:
                break;
        }
    }
    if (velocity != nullptr) {
        const float speed2 = velocity->x * velocity->x + velocity->y * velocity->y;
        if (speed2 >= kMoveThreshold * kMoveThreshold) {
            return ecs::AnimClip::Walk;
        }
    }
    return ecs::AnimClip::Idle;
}

void AnimationSystem::Advance(ecs::AnimState& anim, const ClipSpec& spec, float dt) {
    if (spec.framesPerDir == 0 || spec.fps <= 0.0f || !std::isfinite(dt) || dt < 0.0f) {
        anim.frame = 0;
        anim.time = 0.0f;
        anim.framesPerDir = spec.framesPerDir;
        anim.fps = spec.fps;
        return;
    }

    anim.framesPerDir = spec.framesPerDir;
    anim.fps = spec.fps;
    const float cycleSeconds = static_cast<float>(spec.framesPerDir) / spec.fps;
    const std::uint8_t previousFrame = anim.frame;

    bool wrapped = false;
    if (spec.loop) {
        anim.time += dt;
        if (anim.time >= cycleSeconds) {
            anim.time = std::fmod(anim.time, cycleSeconds);
            wrapped = true;
            anim.eventMaskDone = 0;
        }
    } else {
        anim.time = std::min(anim.time + dt, cycleSeconds);
    }

    const int maxFrame = static_cast<int>(spec.framesPerDir) - 1;
    const int frame = std::clamp(static_cast<int>(anim.time * spec.fps), 0, maxFrame);
    anim.frame = static_cast<std::uint8_t>(frame);

    // 一次性片段前进时，事件帧可能在两个 tick 之间被跨过；按"已到达"触发。
    // 循环片段回卷后先清空事件位，再允许新一轮触发。
    if (wrapped && frame < previousFrame) {
        anim.eventMaskDone = 0;
    }
    if (spec.event != ecs::AnimEvent::None && spec.eventMask != 0) {
        std::uint8_t fired = 0;
        for (std::uint8_t bit = 0; bit < spec.framesPerDir; ++bit) {
            if (frame < static_cast<int>(bit)) {
                break;
            }
            const std::uint8_t mask = BitForFrame(bit);
            if ((spec.eventMask & mask) == 0) {
                continue;
            }
            if ((anim.eventMaskDone & mask) != 0) {
                continue;
            }
            anim.eventMaskDone = static_cast<std::uint8_t>(anim.eventMaskDone | mask);
            fired = 1;
        }
        if (fired != 0) {
            anim.lastEvent = spec.event;
            ++anim.eventSerial;
        }
    }
}

void AnimationSystem::Update(ecs::EcsWorld& world, float dt) const {
    entt::registry& registry = world.Registry();
    registry.view<ecs::Velocity, ecs::Facing, ecs::AnimState>().each(
        [&](const ecs::Entity entity, const ecs::Velocity& velocity, ecs::Facing& facing,
            ecs::AnimState& anim) {
            const auto* action = registry.try_get<ecs::ActionState>(entity);
            const auto* health = registry.try_get<ecs::Health>(entity);
            const ecs::AnimClip clip = SelectClip(&velocity, action, health);
            const ClipSpec& spec = Spec(clip);
            if (anim.clip != clip) {
                anim.clip = clip;
                anim.frame = 0;
                anim.time = 0.0f;
                anim.eventMaskDone = 0;
                anim.framesPerDir = spec.framesPerDir;
                anim.fps = spec.fps;
            }

            const float speed2 = velocity.x * velocity.x + velocity.y * velocity.y;
            // M30-T4：受击/格挡击退速度也会写进 Velocity；若据此改朝向，
            // 连续格挡会在第一次成功后把角色转向远离攻击者，后续攻击全部
            // 落在格挡扇区外。硬直或击退尚未衰减时保持原朝向，恢复后再
            // 由正常移动更新。
            bool knockbackHeld = false;
            if (const auto* hit = registry.try_get<ecs::HitReaction>(entity);
                hit != nullptr) {
                const float hitSpeed2 = hit->vx * hit->vx + hit->vy * hit->vy;
                knockbackHeld =
                    (std::isfinite(hit->staggerLeft) && hit->staggerLeft > 0.0f) ||
                    (std::isfinite(hitSpeed2) &&
                     hitSpeed2 >= kMoveThreshold * kMoveThreshold);
            }
            if (!knockbackHeld && speed2 >= kMoveThreshold * kMoveThreshold) {
                facing.dir = FacingFromVelocity(velocity.x, velocity.y);
            }
            Advance(anim, spec, dt);
        });
}

}  // namespace zl::characters
