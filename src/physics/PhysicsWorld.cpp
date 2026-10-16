#include "physics/PhysicsWorld.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <utility>

#include <btBulletDynamicsCommon.h>

#include "core/Constants.h"
#include "core/Log.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"

namespace zl::physics {
namespace {

constexpr const char* kLogModule = "physics";
constexpr float kFloorHeightUnits = 1.0f;  // 一层楼 = 1.0 高度单位（96px）
constexpr float kCharacterMass = 70.0f;
constexpr std::size_t kMaxDebugLines = 30000;  // 调试线框上限（防爆内存）

std::uint64_t PackKey(int cx, int cy) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(cy));
}

// Bullet 调试绘制接收器：只收集线段（转换到 bgfx 由渲染侧做）
class DebugDrawerImpl final : public btIDebugDraw {
public:
    std::vector<PhysicsWorld::DebugLine> lines;
    int mode = btIDebugDraw::DBG_DrawWireframe;

    void drawLine(const btVector3& from, const btVector3& to, const btVector3& color) override {
        if (lines.size() >= kMaxDebugLines) {
            return;
        }
        const auto c8 = [](btScalar v) {
            return static_cast<std::uint32_t>(std::clamp(v, btScalar(0), btScalar(1)) * 255.0f +
                                              0.5f);
        };
        PhysicsWorld::DebugLine line;
        line.x0 = static_cast<float>(from.x());
        line.y0 = static_cast<float>(from.y());
        line.z0 = static_cast<float>(from.z());
        line.x1 = static_cast<float>(to.x());
        line.y1 = static_cast<float>(to.y());
        line.z1 = static_cast<float>(to.z());
        // Bullet (r,g,b) → bgfx 0xAABBGGRR
        line.color = 0xFF000000u | (c8(color.z()) << 16) | (c8(color.y()) << 8) | c8(color.x());
        lines.push_back(line);
    }

    void drawContactPoint(const btVector3&, const btVector3&, btScalar, int,
                          const btVector3&) override {}
    void reportErrorWarning(const char*) override {}
    void draw3dText(const btVector3&, const char*) override {}
    void setDebugMode(int m) override { mode = m; }
    int getDebugMode() const override { return mode; }

    // 胶囊线框重写为稀疏表示：默认实现是 8×8 经纬网格（单角色 ~156 条线），
    // 几十个角色就会打爆 SpriteBatch 配额；这里只画 1 轴线 + 4 竖线 + 上下双环（~13 条）。
    // 本工程只用 btCapsuleShapeZ（upAxis=2）。
    void drawCapsule(btScalar radius, btScalar halfHeight, int upAxis, const btTransform& transform,
                     const btVector3& color) override {
        (void)upAxis;  // 恒为 2（z 轴）
        const btVector3 center = transform.getOrigin();
        drawLine(center + btVector3(0.0f, 0.0f, -(halfHeight + radius)),
                 center + btVector3(0.0f, 0.0f, halfHeight + radius), color);
        constexpr float kQuarter = 1.5707963f;
        for (int k = 0; k < 4; ++k) {
            const float a = static_cast<float>(k) * kQuarter;
            const btVector3 offset(radius * std::cos(a), radius * std::sin(a), 0.0f);
            drawLine(center + offset + btVector3(0.0f, 0.0f, -halfHeight),
                     center + offset + btVector3(0.0f, 0.0f, halfHeight), color);

            const float a2 = a + kQuarter;
            const btVector3 next(radius * std::cos(a2), radius * std::sin(a2), 0.0f);
            drawLine(center + offset + btVector3(0.0f, 0.0f, halfHeight),
                     center + next + btVector3(0.0f, 0.0f, halfHeight), color);
            drawLine(center + offset + btVector3(0.0f, 0.0f, -halfHeight),
                     center + next + btVector3(0.0f, 0.0f, -halfHeight), color);
        }
    }
};

void DestroyRigidBodyNow(btDiscreteDynamicsWorld* world, btRigidBody* body) {
    world->removeRigidBody(body);
    delete body->getMotionState();
    delete body->getCollisionShape();
    delete body;
}

}  // namespace

struct PhysicsWorld::Impl {
    btDefaultCollisionConfiguration* config = nullptr;
    btCollisionDispatcher* dispatcher = nullptr;
    btDbvtBroadphase* broadphase = nullptr;
    btSequentialImpulseConstraintSolver* solver = nullptr;
    btDiscreteDynamicsWorld* world = nullptr;

    std::unordered_map<std::uint64_t, std::vector<btRigidBody*>> chunkStatics;  // chunk → 静态体
    std::vector<btRigidBody*> dynamicBodies;

    DebugDrawerImpl debugDrawer;
    bool debugEnabled = false;

    double lastStepMs = 0.0;
    double avgStepMs = 0.0;
};

PhysicsWorld::PhysicsWorld() : m_impl(std::make_unique<Impl>()) {
    m_impl->config = new btDefaultCollisionConfiguration();
    m_impl->dispatcher = new btCollisionDispatcher(m_impl->config);
    m_impl->broadphase = new btDbvtBroadphase();
    m_impl->solver = new btSequentialImpulseConstraintSolver();
    m_impl->world =
        new btDiscreteDynamicsWorld(m_impl->dispatcher, m_impl->broadphase, m_impl->solver,
                                    m_impl->config);
    m_impl->world->setGravity(btVector3(0.0f, 0.0f, 0.0f));  // 俯视 2D：无重力
    m_impl->world->setDebugDrawer(&m_impl->debugDrawer);
}

PhysicsWorld::~PhysicsWorld() {
    ClearAll();
    delete m_impl->world;
    delete m_impl->solver;
    delete m_impl->broadphase;
    delete m_impl->dispatcher;
    delete m_impl->config;
}

btRigidBody* PhysicsWorld::CreateCharacterBody(float radius, float height, float gridX, float gridY,
                                               int z) {
    radius = std::max(0.05f, radius);
    height = std::max(height, radius * 2.0f + 0.05f);

    // 胶囊：圆柱段 = 总高 - 2r（btCapsuleShapeZ 沿 z 轴，竖直站立）
    auto* shape = new btCapsuleShapeZ(radius, height - 2.0f * radius);
    btVector3 inertia(0.0f, 0.0f, 0.0f);
    shape->calculateLocalInertia(kCharacterMass, inertia);

    const btVector3 origin(gridX, gridY,
                           static_cast<float>(z) * kFloorHeightUnits + height * 0.5f);
    auto* motion = new btDefaultMotionState(btTransform(btQuaternion::getIdentity(), origin));
    btRigidBody::btRigidBodyConstructionInfo info(kCharacterMass, motion, shape, inertia);
    auto* body = new btRigidBody(info);

    body->setLinearFactor(btVector3(1.0f, 1.0f, 0.0f));   // 锁 z：仅平面运动
    body->setAngularFactor(btVector3(0.0f, 0.0f, 0.0f));  // 不旋转（胶囊保持竖直）
    body->setFriction(0.0f);  // 速度每 tick 直接驱动，低摩擦保证沿墙滑动顺滑
    body->setRestitution(0.0f);
    body->setDamping(0.0f, 0.0f);
    body->setCcdMotionThreshold(radius * 0.5f);       // 高速防穿墙
    body->setCcdSweptSphereRadius(radius * 0.9f);
    body->setSleepingThresholds(0.05f, 0.05f);
    body->setCustomDebugColor(btVector3(0.35f, 0.95f, 1.0f));  // 线框：青色（默认色不可辨）

    m_impl->world->addRigidBody(body);
    m_impl->dynamicBodies.push_back(body);
    return body;
}

void PhysicsWorld::DestroyBody(btRigidBody* body) {
    if (body == nullptr) {
        return;
    }
    const auto it = std::find(m_impl->dynamicBodies.begin(), m_impl->dynamicBodies.end(), body);
    if (it != m_impl->dynamicBodies.end()) {
        m_impl->dynamicBodies.erase(it);
    } else {
        ZL_WARN(kLogModule, "DestroyBody：句柄不在动态体表（重复回收？已忽略）");
        return;
    }
    DestroyRigidBodyNow(m_impl->world, body);
}

void PhysicsWorld::BuildChunkStatics(int chunkX, int chunkY, const world::Chunk& chunk) {
    const std::uint64_t key = PackKey(chunkX, chunkY);
    if (m_impl->chunkStatics.contains(key)) {
        RemoveChunkStatics(chunkX, chunkY);  // 幂等：重挂接先清旧体
    }
    std::vector<btRigidBody*>& bodies = m_impl->chunkStatics[key];

    const int originX = world::Chunk::WorldOriginX(chunkX);
    const int originY = world::Chunk::WorldOriginY(chunkY);
    for (int ly = 0; ly < core::kChunkSize; ++ly) {
        for (int lx = 0; lx < core::kChunkSize; ++lx) {
            const world::GridSquare& square = chunk.Square(lx, ly, 0);
            if (square.IsEmpty()) {
                continue;
            }
            bool solid = false;
            for (int layerIndex = 0; layerIndex < world::kSquareLayerCount && !solid; ++layerIndex) {
                const auto layer = static_cast<world::SquareLayer>(layerIndex);
                for (const world::SquareObject& object : square.Layer(layer)) {
                    if (object.blocksMovement) {
                        solid = true;
                        break;
                    }
                }
            }
            if (!solid) {
                continue;
            }

            // 每格一个 Box：中心 = 格中心（浮点格坐标 = 整数），半格 0.5，高度 [0,1]
            const btVector3 center(static_cast<float>(originX + lx),
                                   static_cast<float>(originY + ly), 0.5f);
            auto* shape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
            auto* motion =
                new btDefaultMotionState(btTransform(btQuaternion::getIdentity(), center));
            btRigidBody::btRigidBodyConstructionInfo info(0.0f, motion, shape);
            auto* body = new btRigidBody(info);
            body->setFriction(0.0f);
            body->setCustomDebugColor(btVector3(1.0f, 0.9f, 0.2f));  // 线框：黄色（默认休眠绿色看不见）
            m_impl->world->addRigidBody(body);
            bodies.push_back(body);
        }
    }
}

void PhysicsWorld::RemoveChunkStatics(int chunkX, int chunkY) {
    const auto it = m_impl->chunkStatics.find(PackKey(chunkX, chunkY));
    if (it == m_impl->chunkStatics.end()) {
        return;
    }
    for (btRigidBody* body : it->second) {
        DestroyRigidBodyNow(m_impl->world, body);
    }
    m_impl->chunkStatics.erase(it);
}

void PhysicsWorld::ClearAll() {
    for (const auto& [key, bodies] : m_impl->chunkStatics) {
        (void)key;
        for (btRigidBody* body : bodies) {
            DestroyRigidBodyNow(m_impl->world, body);
        }
    }
    m_impl->chunkStatics.clear();
    for (btRigidBody* body : m_impl->dynamicBodies) {
        DestroyRigidBodyNow(m_impl->world, body);
    }
    m_impl->dynamicBodies.clear();
}

void PhysicsWorld::Step(float dt) {
    if (dt <= 0.0f) {
        return;
    }
    const auto begin = std::chrono::steady_clock::now();
    m_impl->world->stepSimulation(dt, 4, 1.0f / 60.0f);
    if (m_impl->debugEnabled) {
        m_impl->debugDrawer.lines.clear();
        m_impl->world->debugDrawWorld();
    }
    const auto end = std::chrono::steady_clock::now();
    m_impl->lastStepMs = std::chrono::duration<double, std::milli>(end - begin).count();
    m_impl->avgStepMs = m_impl->avgStepMs <= 0.0
                            ? m_impl->lastStepMs
                            : m_impl->avgStepMs * 0.9 + m_impl->lastStepMs * 0.1;
}

void PhysicsWorld::SetDebugDraw(bool enabled) {
    m_impl->debugEnabled = enabled;
    if (!enabled) {
        m_impl->debugDrawer.lines.clear();
    }
}

bool PhysicsWorld::DebugDrawEnabled() const {
    return m_impl->debugEnabled;
}

const std::vector<PhysicsWorld::DebugLine>& PhysicsWorld::DebugLines() const {
    return m_impl->debugDrawer.lines;
}

std::size_t PhysicsWorld::StaticBodyCount() const {
    std::size_t total = 0;
    for (const auto& [key, bodies] : m_impl->chunkStatics) {
        (void)key;
        total += bodies.size();
    }
    return total;
}

std::size_t PhysicsWorld::DynamicBodyCount() const {
    return m_impl->dynamicBodies.size();
}

std::size_t PhysicsWorld::ChunkStaticBodyCount(int chunkX, int chunkY) const {
    const auto it = m_impl->chunkStatics.find(PackKey(chunkX, chunkY));
    return it == m_impl->chunkStatics.end() ? 0u : it->second.size();
}

double PhysicsWorld::LastStepMs() const {
    return m_impl->lastStepMs;
}

double PhysicsWorld::AvgStepMs() const {
    return m_impl->avgStepMs;
}

}  // namespace zl::physics
