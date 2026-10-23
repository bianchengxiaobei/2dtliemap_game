#pragma once

#include <cstdint>

#include "characters/components/CharacterComponents.h"

namespace zl::characters {

// M14-T3：主手武器挂点（屏幕像素，相对角色 DrawItem 中心）。
// 当前仍是图标叠加而非骨骼蒙皮；偏移随朝向/动作/帧变化，攻击第 1 帧向前送。
struct WeaponSocketOffset {
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
};

inline WeaponSocketOffset PrimaryHandSocket(ecs::Direction direction,
                                            ecs::ActorAction action,
                                            std::uint8_t frame) {
    WeaponSocketOffset offset;
    switch (direction) {
        case ecs::Direction::Down:
            offset = {20.0f, -22.0f, 0.55f};
            break;
        case ecs::Direction::Up:
            offset = {16.0f, -20.0f, 0.55f};
            break;
        case ecs::Direction::Left:
            offset = {-22.0f, -22.0f, 0.55f};
            break;
        case ecs::Direction::Right:
            offset = {22.0f, -22.0f, 0.55f};
            break;
    }

    if (action == ecs::ActorAction::Attacking) {
        const bool strike = frame >= 1;
        switch (direction) {
            case ecs::Direction::Down:
                offset = {strike ? 24.0f : 16.0f, strike ? -14.0f : -24.0f, 0.62f};
                break;
            case ecs::Direction::Up:
                offset = {strike ? 12.0f : 17.0f, strike ? -38.0f : -24.0f, 0.62f};
                break;
            case ecs::Direction::Left:
                offset = {strike ? -34.0f : -22.0f, strike ? -24.0f : -25.0f, 0.62f};
                break;
            case ecs::Direction::Right:
                offset = {strike ? 34.0f : 22.0f, strike ? -24.0f : -25.0f, 0.62f};
                break;
        }
    } else if (action == ecs::ActorAction::Hit) {
        offset.y += 4.0f;
        offset.scale = 0.48f;
    } else if (action == ecs::ActorAction::Dead) {
        offset = {9.0f, -8.0f, 0.42f};
    }
    return offset;
}

}  // namespace zl::characters
