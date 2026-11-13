#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace zl::ecs {

// M4-T1：公共组件定稿（docs/00 §6.1 / docs/02 §5.6）。
// 组件 = 纯数据 struct：无方法、无虚函数、无逻辑。
// 领域组件（Health / AiState / AnimState / ItemInstance…）放各自模块，不在这里。

// FNV-1a 32 位哈希：逻辑资源名（"object.tree"）→ 稳定数值 id。
// SpriteRef 只存哈希；渲染收集（M4-T2）在 AssetProvider 侧建 hash→帧 的反查表。
constexpr std::uint32_t HashName(std::string_view name) noexcept {
    std::uint32_t h = 2166136261u;
    for (const char c : name) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

// 世界位置：连续格坐标（1.0 = 一格），z 为楼层（[0, kMaxZ)）。
// 与 render::IsoMath::ToScreen(float, float, int) 同一坐标系。
struct Position {
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
};

// 速度：格/秒（模拟单位，MovementSystem 积分用）
struct Velocity {
    float x = 0.0f;
    float y = 0.0f;
};

// 碰撞分类位：双方 category & mask != 0 才互相作用（M4-T3 对齐 Bullet）
enum ColliderCategory : std::uint32_t {
    kColliderStatic = 1u << 0,      // 墙 / 家具（M4-T3 由世界数据批量生成）
    kColliderCharacter = 1u << 1,   // 玩家 / 僵尸
    kColliderItem = 1u << 2,        // 掉落物
    kColliderProjectile = 1u << 3,  // 投射物
    kColliderAll = 0xFFFFFFFFu,
};

// 碰撞体：圆柱近似（半径 + 高度，格单位）。
// M4-T3 由 PhysicsSyncSystem 同步到 Bullet；M4-T6 据此生成 passability 位图。
struct Collider {
    float radius = 0.30f;   // 水平半径（PZ 角色约 0.3 格）
    float height = 1.80f;   // 高度
    std::uint32_t category = kColliderCharacter;
    std::uint32_t mask = kColliderAll;
};

// 渲染引用：sprite = HashName(逻辑资源名)；layer 为同格绘制层级提示
// （地面/装饰/家具/角色……的分层，M4-T2 渲染收集细化）；tint 为 0xRRGGBBAA。
struct SpriteRef {
    std::uint32_t sprite = 0;
    int layer = 0;
    std::uint32_t tint = 0xFFFFFFFFu;
};

struct SpriteScale {
    float scale = 1.0f;
};

// 稳定 ID：跨存档 / 脚本 / 事件 / 跨模块引用一律用它；
// 绝不跨 tick 保存 entt::entity 数值。EcsWorld::Create 自动附着，
// 从 1 开始单调递增、永不复用（0 = 无效）。
struct StableId {
    std::uint64_t id = 0;
};

struct BaseSiegeZombie {};

struct NoCombatTarget {};

// M23-F4-T5：友方 NPC 标记。死亡事件/击杀统计不得把聚落守卫当作僵尸；
// 具体阵营/关系系统留后续，本标记只服务最小友军语义。
struct FriendlyNpc {};

// M19-F2：POI 固定守军标记（poiId 用于按区域统计）。带此标记的僵尸不参与 LOD 虚拟化，
// 避免玩家离开后 POI 威胁被回收；守军实体随存档保留。
struct PoiGuard {
    std::string poiId;
};

// M24-T4：随机事件临时敌人标记。仅运行时存在，不进入存档；eventId 用于把击杀
// 只记到对应随机事件，避免普通僵尸或其它事件完成“击杀事件敌人”目标。
struct RandomEventActor {
    std::string eventId;
    int index = 0;
};

struct WorldEventActor {
    std::string eventId;
    std::string target;
    int index = 0;
};

struct WorldEventContainer {
    std::string eventId;
    int index = 0;
};

}  // namespace zl::ecs
