#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class btRigidBody;

namespace zl::world {
class Chunk;
}

namespace zl::physics {

// M4-T3：Bullet 物理世界封装（docs/00 §6.2）。
//
// 坐标映射：Bullet (x, y, z) = 游戏 (世界格坐标 x, y, 高度)；高度 1.0 = 一层楼（96px）。
// 重力 0（俯视游戏）；角色胶囊锁 XY 平面（linearFactor(1,1,0)）、禁旋转。
//
// 线程模型（docs/00 §3.2）：只在主线程 tick 内使用（Step 由 PhysicsSyncSystem 调）。
// 静态体生命周期跟随 chunk 流式加载：挂接时 BuildChunkStatics，卸载前 RemoveChunkStatics。
class PhysicsWorld {
public:
    // 调试线框线段（世界空间；color 为 0xAABBGGRR，bgfx 色序）
    struct DebugLine {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float z0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        float z1 = 0.0f;
        std::uint32_t color = 0xFFFFFFFFu;
    };

    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // ---- 动态体（角色）----

    // 胶囊（半径 + 总高，格单位），中心置于脚底上方 height/2（z 层高度叠加）；
    // 返回裸指针挂进 ecs::RigidBody（本类拥有），用 DestroyBody 回收
    btRigidBody* CreateCharacterBody(float radius, float height, float gridX, float gridY, int z);
    void DestroyBody(btRigidBody* body);

    // ---- 静态体（墙/家具/树，按 chunk 批量生成/卸载）----

    // 遍历 chunk 的 z=0 层：任一对象 blocksMovement=true → 该格生成一个 Box（半格 0.5）。
    // 幂等：重复 Build 同一 chunk 先移除旧的。
    void BuildChunkStatics(int chunkX, int chunkY, const world::Chunk& chunk);
    void RemoveChunkStatics(int chunkX, int chunkY);
    void ClearAll();

    // ---- 步进 ----

    void Step(float dt);  // 固定 tick 内调用（1/60；自带子步与 CCD）

    // ---- 调试线框（btIDebugDraw 收集 → 主线程读取后转 bgfx 线段）----

    void SetDebugDraw(bool enabled);
    bool DebugDrawEnabled() const;
    const std::vector<DebugLine>& DebugLines() const;

    // ---- 统计 ----

    std::size_t StaticBodyCount() const;  // 全部 chunk 静态体
    std::size_t DynamicBodyCount() const;
    std::size_t ChunkStaticBodyCount(int chunkX, int chunkY) const;
    double LastStepMs() const;
    double AvgStepMs() const;  // 指数滑动平均（0.9/0.1）

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace zl::physics
