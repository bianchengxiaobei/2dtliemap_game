#pragma once

#include <cstdint>
#include <string>

#include <entt/entt.hpp>

#include "core/GameTime.h"

namespace zl::game {
struct GameContext;
}

namespace zl::script {

// 脚本返回值用的三元位置（x/y 为连续格坐标，z 为楼层）。
struct ScriptVec3 {
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
};

// M7-T3：Host API 薄包装（docs/03 §4）：
//  - 只暴露简单签名（数值/字符串/稳定 ID 句柄），不暴露任何内部类；
//  - 实体一律用 StableId（0 = 无效；跨存档有效）；
//  - 本类不依赖任何脚本库——各后端（Lua/Python）把它直译为同名脚本表；
//  - API 版本：mod.json 的 "api" 字段与本常量比对（M7-T5 加载器执行校验）。
class ScriptApi {
public:
    static constexpr int kApiVersion = 1;

    void Attach(game::GameContext* ctx) { m_ctx = ctx; }

    // ---- Game ----
    std::int64_t GetTick() const;
    std::uint64_t GetPlayerId() const;  // 0 = 无玩家
    core::DateTime GetTime() const;     // 无游戏时间源时返回默认日期
    double GetTotalGameMinutes() const;

    // ---- Player ----
    bool GetPlayerPosition(ScriptVec3& out) const;
    bool TeleportPlayer(float x, float y, int z) const;  // 只改游戏状态，不改本对象
    bool GetPlayerHealth(float& out) const;
    int AddPlayerItem(const std::string& defId, int count) const;  // 返回实际加入数

    // ---- World ----
    bool IsPassable(int gx, int gy) const;  // z=0 地面层；未加载 chunk = 不可通行
    bool SpawnItem(const std::string& defId, float x, float y, int z) const;

    // ---- Zombies ----
    std::uint64_t SpawnZombie(float x, float y, int z) const;  // 0 = 失败
    int ZombieCount() const;
    bool GetZombiePosition(std::uint64_t handle, ScriptVec3& out) const;

    // ---- Items（只读注册表查询）----
    bool ItemExists(const std::string& defId) const;
    std::string ItemName(const std::string& defId) const;

private:
    entt::entity FindPlayer() const;

    game::GameContext* m_ctx = nullptr;
};

}  // namespace zl::script
