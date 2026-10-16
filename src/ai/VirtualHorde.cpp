#include "ai/VirtualHorde.h"

#include <algorithm>
#include <cmath>

namespace zl::ai {

std::uint64_t VirtualHordeStore::AddHorde(float cx, float cy, int count, float spread) {
    if (count <= 0) {
        return 0;
    }
    VirtualHorde horde;
    horde.id = m_nextId++;
    horde.centerX = cx;
    horde.centerY = cy;
    horde.spread = spread;
    horde.count = count;
    m_hordes.push_back(horde);
    return horde.id;
}

bool VirtualHordeStore::RemoveHorde(std::uint64_t id) {
    for (std::size_t i = 0; i < m_hordes.size(); ++i) {
        if (m_hordes[i].id == id) {
            m_hordes[i] = m_hordes.back();
            m_hordes.pop_back();
            return true;
        }
    }
    return false;
}

int VirtualHordeStore::TakeZombies(std::uint64_t id, int n) {
    if (n <= 0) {
        return 0;
    }
    for (std::size_t i = 0; i < m_hordes.size(); ++i) {
        if (m_hordes[i].id != id) {
            continue;
        }
        const int taken = std::min(n, m_hordes[i].count);
        m_hordes[i].count -= taken;
        if (m_hordes[i].count <= 0) {
            m_hordes[i] = m_hordes.back();
            m_hordes.pop_back();
        }
        return taken;
    }
    return 0;
}

void VirtualHordeStore::Update(float dt, float attractX, float attractY, float stopDistance) {
    const float stop = std::max(0.0f, stopDistance);
    for (VirtualHorde& horde : m_hordes) {
        const float dx = attractX - horde.centerX;
        const float dy = attractY - horde.centerY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= stop || dist <= 0.001f) {
            continue;  // 已在驻留距离内（或重合）：不再靠近
        }
        const float step = std::min(horde.speed * dt, dist - stop);
        horde.centerX += dx / dist * step;
        horde.centerY += dy / dist * step;
    }
}

VirtualHordeStore::Stats VirtualHordeStore::GetStats(float fromX, float fromY) const {
    Stats stats;
    stats.hordes = static_cast<int>(m_hordes.size());
    for (const VirtualHorde& horde : m_hordes) {
        stats.zombies += horde.count;
        const float dx = horde.centerX - fromX;
        const float dy = horde.centerY - fromY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (stats.nearestDist < 0.0f || dist < stats.nearestDist) {
            stats.nearestDist = dist;
        }
    }
    return stats;
}

void VirtualHordeStore::Clear() {
    m_hordes.clear();
}

}  // namespace zl::ai
