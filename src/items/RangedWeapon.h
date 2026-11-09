#pragma once

#include <cstdint>
#include <string>

#include "items/ItemComponents.h"
#include "items/ItemDef.h"

namespace zl::items {

// M23-F1-T3：单把远程武器的纯运行时状态（冷却/换弹），不进入存档。
// 弹匣数保存在 ItemInstance.loadedAmmo；本结构只保存由时间推进的临时状态。
enum class RangedReloadEvent : std::uint8_t {
    None = 0,
    Completed = 1,
    Cancelled = 2,
};

struct RangedWeaponRuntime {
    float cooldownLeft = 0.0f;
    bool reloading = false;
    float reloadLeft = 0.0f;
    int reloadAmount = 0;
    std::string reloadWeaponId;

    // 每次固定 tick 调用；只推进冷却，不自动完成换弹。
    void Tick(float dt);

    bool CanFire(const ItemDef& def, const ecs::ItemInstance& weapon,
                 std::string* reason = nullptr) const;

    // 可用弹药数量由调用方从背包统计；成功时锁定本次换弹量。
    bool StartReload(const ItemDef& def, const ecs::ItemInstance& weapon,
                     int availableAmmo, std::string* reason = nullptr);

    // 换弹计时；武器被卸下/切换/损坏时返回 Cancelled。
    RangedReloadEvent TickReload(float dt, const ItemDef* currentDef,
                                 const ecs::ItemInstance* currentWeapon,
                                 std::string* reason = nullptr);

    void CancelReload(std::string* reason = nullptr);
};

// M23-F1-T4：按飞行距离把基础伤害插值到 falloffScale。
// falloffStart 前满伤害，falloffEnd 后固定为 damage * falloffScale；
// falloffEnd == falloffStart 视为在起点后立即衰减，避免除零。
float RangedDamageAtDistance(float damage, float distance, float falloffStart,
                             float falloffEnd, float falloffScale);
float RangedDamageAtDistance(const RangedCombat& ranged, float distance);

// 弹丸在总散布锥内的确定性偏移（[-spread/2, +spread/2] 度）。
// 使用 shotSerial + pelletIndex 的稳定哈希，保证同一发/同一弹丸可复现。
float RangedSpreadOffsetDegrees(const RangedCombat& ranged,
                                std::uint32_t shotSerial,
                                std::uint32_t pelletIndex);

}  // namespace zl::items
