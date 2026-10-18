#pragma once

#include <glm/glm.hpp>

namespace zl::core {
class Input;
}

namespace zl::ecs {
class EcsWorld;
}

namespace zl::characters {

// M4-T4：本地输入 → MoveIntent（docs/00 §6.1 固定链首位）。
//
// WASD 是**屏幕语义**（W = 屏幕上、D = 屏幕右）。等距 2:1 投影（IsoMath::ToScreen）下
// 屏幕方向 ≠ 世界格方向：这里做等距矫正（反解投影的线性部分），使"按 W 角色朝屏幕上方走"。
// 输出 MoveIntent 是世界格空间的速度（格/秒）——物理/AI/寻路全部用世界格空间，语义统一。
//
// 屏幕方向 s（单位向量，y 向下）→ 世界格方向 g（未归一）：
//   IsoMath: s.x = (gx - gy) * kTileW/2, s.y = (gx + gy) * kTileH/2
//   反解：    gx - gy = s.x / (kTileW/2)，gx + gy = s.y / (kTileH/2)
// 该变换是线性的：先反解再在格空间归一，屏幕方向保持不变（只统一世界速度）。
class InputSystem {
public:
    // M12-T1：命令模式——MoveInput（屏幕方向）→ MoveIntent（世界速度）。
    void Update(ecs::EcsWorld& world) const;

    // 直连模式/单测：原始键盘 → MoveIntent（M4-T4 行为保持不变）。
    void Update(core::Input& input, ecs::EcsWorld& world) const;

    // 屏幕方向 → 世界格方向（等距矫正；不归一）。纯函数（可单测）。
    static glm::vec2 ScreenDirToGridDir(const glm::vec2& screenDir);
};

}  // namespace zl::characters
