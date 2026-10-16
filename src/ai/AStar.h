#pragma once

#include <cstdint>
#include <vector>

namespace zl::ai {

class PathSnapshot;

// 世界格坐标
struct GridCell {
    int x = 0;
    int y = 0;
};

// 8 向 A* 选项（默认即推荐配置）
struct AStarOptions {
    float diagonalCost = 1.41421356f;  // √2：对角代价修正（非 1）
    int maxExpansions = 200000;        // 展开节点上限（0 = 不限）；超限报 BudgetExceeded
};

struct AStarResult {
    enum class Outcome : std::uint8_t {
        Found = 0,
        Unreachable = 1,     // 目标封闭（如八邻全堵）
        Invalid = 2,         // 起点/终点在快照区域外，或目标格本身阻挡
        BudgetExceeded = 3,  // 展开数超预算
    };

    Outcome outcome = Outcome::Invalid;
    std::vector<GridCell> cells;  // Found：路点序列（含终点，不含起点）
    int expanded = 0;             // 展开节点数（性能诊断）
    float cost = 0.0f;            // 路径代价（格；对角按 diagonalCost）
    double ms = 0.0;              // PlanPath 内部计时
};

// 8 向 A*：Octile 启发式、对角 √2 代价、禁止穿角（对角移动要求两个正交邻格均可走）。
// 起点即使在阻挡格也允许（可走出被堵点）；目标格阻挡直接判 Unreachable。
// 纯函数、无状态、线程安全；只读 snapshot。
AStarResult PlanPath(const PathSnapshot& snapshot, int sx, int sy, int gx, int gy,
                     const AStarOptions& options = {});

}  // namespace zl::ai
