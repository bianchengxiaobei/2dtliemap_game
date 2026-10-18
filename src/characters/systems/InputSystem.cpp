#include "characters/systems/InputSystem.h"

#include <cmath>

#include <entt/entt.hpp>

#include "characters/components/CharacterComponents.h"
#include "core/Constants.h"
#include "core/Input.h"
#include "ecs/EcsWorld.h"

namespace zl::characters {
namespace {

void ApplyScreenDirection(const glm::vec2& screen, const ecs::LocalControl& control,
                          ecs::MoveIntent& intent) {
    if (!std::isfinite(screen.x) || !std::isfinite(screen.y) ||
        (std::abs(screen.x) < 0.0001f && std::abs(screen.y) < 0.0001f)) {
        intent.x = 0.0f;
        intent.y = 0.0f;
        return;
    }
    const glm::vec2 dir =
        glm::normalize(InputSystem::ScreenDirToGridDir(glm::normalize(screen)));
    intent.x = dir.x * control.walkSpeed;
    intent.y = dir.y * control.walkSpeed;
}

}  // namespace

glm::vec2 InputSystem::ScreenDirToGridDir(const glm::vec2& screenDir) {
    const float halfW = static_cast<float>(core::kTileW) * 0.5f;
    const float halfH = static_cast<float>(core::kTileH) * 0.5f;
    const float a = screenDir.x / halfW;  // gx - gy
    const float b = screenDir.y / halfH;  // gx + gy
    return {(a + b) * 0.5f, (b - a) * 0.5f};
}

void InputSystem::Update(ecs::EcsWorld& world) const {
    entt::registry& registry = world.Registry();
    registry.view<ecs::PlayerTag, ecs::LocalControl, ecs::MoveIntent>().each(
        [&](const ecs::Entity entity, const ecs::LocalControl& control,
            ecs::MoveIntent& intent) {
            const auto* move = registry.try_get<ecs::MoveInput>(entity);
            ApplyScreenDirection(move != nullptr ? glm::vec2(move->x, move->y)
                                                 : glm::vec2(0.0f, 0.0f),
                                 control, intent);
        });
}

void InputSystem::Update(core::Input& input, ecs::EcsWorld& world) const {
    glm::vec2 screen(0.0f, 0.0f);
    if (input.IsActionDown("MoveUp")) {
        screen.y -= 1.0f;
    }
    if (input.IsActionDown("MoveDown")) {
        screen.y += 1.0f;
    }
    if (input.IsActionDown("MoveLeft")) {
        screen.x -= 1.0f;
    }
    if (input.IsActionDown("MoveRight")) {
        screen.x += 1.0f;
    }
    entt::registry& registry = world.Registry();
    registry.view<ecs::PlayerTag, ecs::LocalControl, ecs::MoveIntent>().each(
        [&](const ecs::Entity, const ecs::LocalControl& control,
            ecs::MoveIntent& intent) { ApplyScreenDirection(screen, control, intent); });
}

}  // namespace zl::characters
