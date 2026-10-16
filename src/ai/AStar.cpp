#include "ai/AStar.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

#include "ai/Passability.h"

namespace zl::ai {
namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();

// 快照区域（格坐标），节点数组按此区域的行主序索引
struct Region {
    int minX = 0;
    int minY = 0;
    int width = 0;
    int height = 0;

    int IndexOf(int x, int y) const { return (y - minY) * width + (x - minX); }
};

// Octile 距离：8 向可走时的最短距离（可采纳启发式）
float OctileDistance(int dx, int dy, float diagonalCost) {
    const int ax = std::abs(dx);
    const int ay = std::abs(dy);
    const int diagonalSteps = std::min(ax, ay);
    const int straightSteps = std::max(ax, ay) - diagonalSteps;
    return static_cast<float>(straightSteps) + diagonalCost * static_cast<float>(diagonalSteps);
}

struct OpenNode {
    float f = 0.0f;
    float h = 0.0f;
    int x = 0;
    int y = 0;
};

// 小顶堆比较器：f 小优先；f 相同取 h 小（更贴近目标，减少平铺展开）
struct OpenNodeGreater {
    bool operator()(const OpenNode& a, const OpenNode& b) const {
        if (a.f != b.f) {
            return a.f > b.f;
        }
        return a.h > b.h;
    }
};

}  // namespace

AStarResult PlanPath(const PathSnapshot& snapshot, int sx, int sy, int gx, int gy,
                     const AStarOptions& options) {
    const auto begin = std::chrono::steady_clock::now();
    AStarResult result;

    const auto finish = [&result, begin](AStarResult::Outcome outcome) {
        result.outcome = outcome;
        const auto end = std::chrono::steady_clock::now();
        result.ms = std::chrono::duration<double, std::milli>(end - begin).count();
        return result;
    };

    // 区域外交互不可能；目标格阻挡 = 调用方未做目标校验（T7 会先纠正到相邻可走格）
    if (!snapshot.InRegion(sx, sy) || !snapshot.InRegion(gx, gy)) {
        return finish(AStarResult::Outcome::Invalid);
    }
    if (snapshot.IsBlocked(gx, gy)) {
        return finish(AStarResult::Outcome::Unreachable);
    }
    if (sx == gx && sy == gy) {
        return finish(AStarResult::Outcome::Found);
    }

    const Region region{snapshot.MinGX(), snapshot.MinGY(),
                        snapshot.MaxGX() - snapshot.MinGX() + 1,
                        snapshot.MaxGY() - snapshot.MinGY() + 1};
    const std::size_t count = static_cast<std::size_t>(region.width) *
                              static_cast<std::size_t>(region.height);
    std::vector<float> gScore(count, kInfinity);
    std::vector<std::int32_t> cameFrom(count, -1);
    std::vector<std::uint8_t> closed(count, 0u);

    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeGreater> open;
    const int startIndex = region.IndexOf(sx, sy);
    gScore[static_cast<std::size_t>(startIndex)] = 0.0f;
    open.push(OpenNode{OctileDistance(sx - gx, sy - gy, options.diagonalCost),
                       OctileDistance(sx - gx, sy - gy, options.diagonalCost), sx, sy});

    // 8 向：E, SE, S, SW, W, NW, N, NE（后 4 个为对角）
    constexpr int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    constexpr int kDy[8] = {0, 1, 1, 1, 0, -1, -1, -1};

    while (!open.empty()) {
        const OpenNode node = open.top();
        open.pop();
        const int index = region.IndexOf(node.x, node.y);
        if (closed[static_cast<std::size_t>(index)] != 0u) {
            continue;  // 惰性删除的过期节点
        }
        closed[static_cast<std::size_t>(index)] = 1u;
        ++result.expanded;

        if (node.x == gx && node.y == gy) {
            // 回溯（cameFrom 存父节点的 region 索引）
            std::vector<GridCell> reversed;
            int cur = index;
            while (cur != startIndex && cur >= 0) {
                reversed.push_back(GridCell{region.minX + cur % region.width,
                                            region.minY + cur / region.width});
                cur = cameFrom[static_cast<std::size_t>(cur)];
            }
            std::reverse(reversed.begin(), reversed.end());
            result.cells = std::move(reversed);
            result.cost = gScore[static_cast<std::size_t>(index)];
            return finish(AStarResult::Outcome::Found);
        }

        if (options.maxExpansions > 0 && result.expanded >= options.maxExpansions) {
            return finish(AStarResult::Outcome::BudgetExceeded);
        }

        const float nodeG = gScore[static_cast<std::size_t>(index)];
        for (int dir = 0; dir < 8; ++dir) {
            const int nx = node.x + kDx[dir];
            const int ny = node.y + kDy[dir];
            // 区域外/未填充/阻挡（PathSnapshot 统一判定）
            if (snapshot.IsBlocked(nx, ny)) {
                continue;
            }
            const bool diagonal = kDx[dir] != 0 && kDy[dir] != 0;
            if (diagonal && (snapshot.IsBlocked(node.x + kDx[dir], node.y) ||
                             snapshot.IsBlocked(node.x, node.y + kDy[dir]))) {
                continue;  // 禁止穿角：对角移动要求两个正交邻格可走
            }
            const float tentative =
                nodeG + (diagonal ? options.diagonalCost : 1.0f);
            const int nextIndex = region.IndexOf(nx, ny);
            if (tentative >= gScore[static_cast<std::size_t>(nextIndex)]) {
                continue;
            }
            gScore[static_cast<std::size_t>(nextIndex)] = tentative;
            cameFrom[static_cast<std::size_t>(nextIndex)] = index;
            const float h = OctileDistance(nx - gx, ny - gy, options.diagonalCost);
            open.push(OpenNode{tentative + h, h, nx, ny});
        }
    }

    return finish(AStarResult::Outcome::Unreachable);
}

}  // namespace zl::ai
