#include "ai/systems/LodSystem.h"

#include "ai/components/AiComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"

namespace zl::ai {

void LodSystem::Update(ecs::EcsWorld& world) {
    const std::uint32_t tick = m_tick++;
    m_stats = Stats{};

    const float fullSq = m_params.fullRadius * m_params.fullRadius;
    const float midSq = m_params.midRadius * m_params.midRadius;

    world.Registry().view<ecs::LodState, ecs::Position>().each(
        [&](const ecs::Entity, ecs::LodState& lod, const ecs::Position& position) {
            const float dx = position.x - m_focusX;
            const float dy = position.y - m_focusY;
            const float distSq = dx * dx + dy * dy;

            if (distSq <= fullSq) {
                lod.tier = 0;
                lod.updateThisTick = true;
                lod.dtScale = 1.0f;
                ++m_stats.fullCount;
            } else if (distSq <= midSq) {
                lod.tier = 1;
                lod.updateThisTick = (tick % 2u) == 0u;
                lod.dtScale = 2.0f;
                ++m_stats.midCount;
            } else {
                lod.tier = 2;
                lod.updateThisTick = (tick % 6u) == 0u;
                lod.dtScale = 6.0f;
                ++m_stats.farCount;
            }
            if (lod.updateThisTick) {
                ++m_stats.updated;
            }
        });
}

}  // namespace zl::ai
