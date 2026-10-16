#pragma once

#include <cstdint>

namespace zl::core {

// M5-T4：声音事件（事件总线载荷类型；M5-T3 感知系统消费）。
// 发射方（开枪/脚步/撞击…）填好传播模型算出的 radius 后经事件总线投递，
// PerceptionSystem 按 (距离 ≤ radius) 判别可闻。
enum class SoundType : std::uint8_t {
    Generic = 0,  // 泛用（脚步/环境）
    Gunshot = 1,  // 枪声（大半径）
    Impact = 2,   // 撞击（中半径）
};

struct SoundEvent {
    float x = 0.0f;       // 声源位置（世界格）
    float y = 0.0f;
    float radius = 0.0f;  // 可闻半径（含隔墙衰减后的结果；M5-T4 传播模型计算）
    SoundType type = SoundType::Generic;
};

}  // namespace zl::core
