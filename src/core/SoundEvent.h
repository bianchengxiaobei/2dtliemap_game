#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace zl::core {

// 事件总线键（docs/03 §5 事件表：OnSound）
inline constexpr std::string_view kOnSound = "OnSound";

// M5-T4：声音事件（事件总线载荷类型；M5-T3/M5-T4 感知系统消费）。
// 规范：发射方只填"声源位置 + 基础可闻半径 + 类型"（武器/动作决定），
// 经事件总线（EmitQueued(kOnSound, …)）投递；隔墙衰减由消费方按听者位置计算：
//   effectiveRadius = radius × wallAttenuationPerCell^穿墙格数
// （M11-T4 起音频系统与 AI 听觉共用同一套衰减参数。）
enum class SoundType : std::uint8_t {
    Generic = 0,       // 泛用
    Gunshot = 1,       // 枪声（大半径）
    Impact = 2,        // 撞击（中半径）
    Footstep = 3,      // 玩家脚步（M11-T3；clip 区分地面材质）
    ZombieGrowl = 4,   // 僵尸低吼（M11-T3）
    Ui = 5,            // UI 点击/拾取（M11-T3；2D 播放）
    Vehicle = 6,       // 载具引擎/行驶噪音（M25-T4）
};

struct SoundEvent {
    float x = 0.0f;       // 声源位置（世界格）
    float y = 0.0f;
    float radius = 0.0f;  // 基础可闻半径（格，无遮挡）；隔墙衰减由感知系统按听者计算
    SoundType type = SoundType::Generic;
    // M11-T3：可选音频 clip 名（不含 .wav）。脚步为 footstep_grass/dirt/wood；
    // 空则由音频系统按 type 选择默认音效。
    std::string clip;
};

}  // namespace zl::core
