#include "characters/systems/AnimationSystem.h"

#include <cmath>

#include <entt/entt.hpp>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"

namespace zl::characters {

ecs::Direction AnimationSystem::FacingFromVelocity(float vx, float vy) {
    const float sx = vx - vy;  // 屏幕 x 分量（线性部分，比例无关）
    const float sy = vx + vy;  // 屏幕 y 分量
    if (std::fabs(sy) >= std::fabs(sx)) {
        return sy < 0.0f ? ecs::Direction::Up : ecs::Direction::Down;
    }
    return sx < 0.0f ? ecs::Direction::Left : ecs::Direction::Right;
}

void AnimationSystem::Update(ecs::EcsWorld& world, float dt) const {
    entt::registry& registry = world.Registry();
    registry.view<ecs::Velocity, ecs::Facing, ecs::AnimState>().each(
        [&](const ecs::Entity, const ecs::Velocity& velocity, ecs::Facing& facing,
            ecs::AnimState& anim) {
            const float speed2 = velocity.x * velocity.x + velocity.y * velocity.y;
            if (speed2 < kMoveThreshold * kMoveThreshold) {
                anim.frame = 0;  // 站立帧
                anim.time = 0.0f;
                return;
            }
            facing.dir = FacingFromVelocity(velocity.x, velocity.y);
            anim.time += dt;
            if (anim.framesPerDir > 1) {
                const float cycleSeconds = static_cast<float>(anim.framesPerDir) / anim.fps;
                if (anim.time >= cycleSeconds) {
                    anim.time = std::fmod(anim.time, cycleSeconds);  // 防无界增长
                }
                const int frame = static_cast<int>(anim.time * anim.fps);
                anim.frame =
                    static_cast<std::uint8_t>(frame % static_cast<int>(anim.framesPerDir));
            } else {
                anim.frame = 0;
            }
        });
}

}  // namespace zl::characters
