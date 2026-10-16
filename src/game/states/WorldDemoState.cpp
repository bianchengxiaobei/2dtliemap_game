#include "game/states/WorldDemoState.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

#include "core/Config.h"
#include "core/Constants.h"
#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "core/Input.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"
#include "ecs/SpatialIndex.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/EventProbe.h"
#include "physics/PhysicsWorld.h"
#include "platform/Window.h"
#include "render/Camera.h"
#include "render/IsoMath.h"
#include "render/Renderer.h"
#include "render/SpriteBatch.h"
#include "world/Chunk.h"
#include "world/ChunkManager.h"
#include "world/World.h"
#include "world/WorldCoords.h"

namespace zl::game {
namespace {

constexpr float kPanSpeedPxPerSecond = 900.0f;
constexpr float kGridLineThickness = 1.2f;
constexpr float kHoverLineThickness = 2.5f;
constexpr float kStatsLogInterval = 3.0f;
constexpr float kPerfLogInterval = 5.0f;
constexpr uint32_t kGridColor = 0x30ffffff;   // 白色低透明（ABGR: A=0x30）
constexpr uint32_t kHoverColor = 0xff30e0ff;  // 黄色高亮
constexpr uint32_t kAxisXColor = 0xc04040ff;  // 红
constexpr uint32_t kAxisYColor = 0xc040ff40;  // 绿
// 上层（z≥1）透明度策略（M3-T6 临时方案）：楼上整体 35% 不透明，看得到室内
constexpr uint32_t kUpperZColor = 0x59ffffff;

// 工作集内存（压测记录用；非 Windows 返回 0）
uint64_t WorkingSetBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return counters.WorkingSetSize;
    }
#endif
    return 0;
}

}  // namespace

void WorldDemoState::OnEnter() {
    core::Config& config = *m_ctx.config;
    ZL_INFO("render", "进入 WorldDemo（M3 世界模型 + 流式加载演示）");

    const std::string manifest =
        config.Get<std::string>("assets.manifest", "assets/asset_manifest.json");
    const std::string provider = config.Get<std::string>("assets.provider", "own");
    if (!m_ctx.assets->Init(manifest, provider, *m_ctx.atlases)) {
        ZL_ERROR("render", "资源初始化失败，世界将不可见");
    }

    // 相机：对准 village_demo 北侧村庄（房群在 chunk (6..7, 1..2)）；无边界（世界无限）
    render::Camera& camera = *m_ctx.camera;
    camera.SetViewport(m_ctx.renderer->FramebufferWidth(), m_ctx.renderer->FramebufferHeight());
    camera.ClearBounds();
    camera.SetCenter(render::ToScreen(70.0f, 18.0f));

    // 自动巡航压测（M3-T7；F8 手动切换）
    m_cruiseEnabled = config.Get<bool>("world.autoCruise", false);
    m_cruiseSpeed = static_cast<float>(config.Get<double>("world.cruiseSpeed", 30.0));
    m_cruiseRadius = static_cast<float>(config.Get<double>("world.cruiseRadius", 40.0));
    m_cruiseCenterX = static_cast<float>(config.Get<double>("world.cruiseCenterX", 500.0));
    m_cruiseCenterY = static_cast<float>(config.Get<double>("world.cruiseCenterY", 500.0));
    m_cruiseMinutes = config.Get<double>("world.cruiseMinutes", 5.0);
    if (m_cruiseEnabled) {
        camera.SetCenter(render::ToScreen(m_cruiseCenterX + m_cruiseRadius, m_cruiseCenterY));
        ZL_INFO("perf", "自动巡航开启：圆心=({:.0f},{:.0f}) 半径={:.0f} 速度={:.0f}格/s 时长={:.1f}min",
                m_cruiseCenterX, m_cruiseCenterY, m_cruiseRadius, m_cruiseSpeed, m_cruiseMinutes);
    }

    // M4-T2/T3：ECS 演示实体（村庄附近漂移；物理体由 Bullet 驱动）
    SpawnDemoEntities();

    ZL_INFO("render", "按键：WASD/右键拖拽 平移 | 滚轮 缩放 | F8 自动巡航 | F9 网格线 | F10 物理线框 | F1 暂停 | F2 倍速 | F5 热重载 | ESC 退出");
}

void WorldDemoState::SpawnDemoEntities() {
    if (m_ctx.ecs == nullptr) {
        return;  // 无 ECS 上下文（理论不出现）：纯世界渲染降级
    }
    ecs::EcsWorld& world = *m_ctx.ecs;
    std::mt19937 rng(20261016u);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    // 漂移盒对准默认相机视野（村北，world (70,18) 附近）
    m_demoBoxX0 = 44.0f;
    m_demoBoxY0 = 4.0f;
    m_demoBoxX1 = 96.0f;
    m_demoBoxY1 = 32.0f;

    constexpr int kCount = 40;
    const uint32_t sprite = ecs::HashName("character.placeholder");
    entt::registry& registry = world.Registry();
    for (int i = 0; i < kCount; ++i) {
        const ecs::Entity e = world.Create();
        const float px = m_demoBoxX0 + u01(rng) * (m_demoBoxX1 - m_demoBoxX0);
        const float py = m_demoBoxY0 + u01(rng) * (m_demoBoxY1 - m_demoBoxY0);
        const float angle = u01(rng) * 6.2831853f;
        const float speed = 0.8f + u01(rng) * 1.6f;
        registry.emplace<ecs::Position>(e, ecs::Position{px, py, 0});
        registry.emplace<ecs::Velocity>(
            e, ecs::Velocity{std::cos(angle) * speed, std::sin(angle) * speed});
        registry.emplace<ecs::Collider>(e, ecs::Collider{});
        registry.emplace<ecs::SpriteRef>(e, ecs::SpriteRef{sprite, 0, 0xFFFFFFFFu});

        // M4-T3：物理体（胶囊参数与 Collider 一致）；位置/速度由 Bullet 接管
        if (m_ctx.physics != nullptr) {
            const ecs::Collider def;
            ecs::RigidBody rigid;
            rigid.body = m_ctx.physics->CreateCharacterBody(def.radius, def.height, px, py, 0);
            registry.emplace<ecs::RigidBody>(e, rigid);
        }
    }
    m_demoEntityCount = kCount;
    ZL_INFO("ecs", "生成 {} 个演示实体（漂移盒 x[{:.0f},{:.0f}] y[{:.0f},{:.0f}]，精灵名哈希 {:#x}）",
            kCount, m_demoBoxX0, m_demoBoxX1, m_demoBoxY0, m_demoBoxY1, sprite);
}

void WorldDemoState::DespawnDemoEntities() {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    registry.view<ecs::Position, ecs::Velocity>().each(
        [&](const ecs::Entity e, ecs::Position&, ecs::Velocity&) {
            if (m_ctx.physics != nullptr) {
                if (const auto* rigid = registry.try_get<ecs::RigidBody>(e);
                    rigid != nullptr && rigid->body != nullptr) {
                    m_ctx.physics->DestroyBody(rigid->body);
                }
            }
            m_ctx.ecs->Destroy(e);
        });
    m_ctx.ecs->Flush();
    m_demoEntityCount = 0;
}

void WorldDemoState::OnExit() {
    // 先清演示实体（含物理体），再汇报统计
    DespawnDemoEntities();
    const auto& stats = m_ctx.chunkManager->GetStats();
    ZL_INFO("render", "退出 WorldDemo：本状态共 {} tick；chunk 累计 加载 {} / 卸载 {} / 取消 {}",
            m_ticksInState, stats.totalLoaded, stats.totalUnloaded, stats.totalCancelled);
}

void WorldDemoState::OnTick(double tickDt) {
    (void)tickDt;
    ++m_ticksInState;
    // M4-T3：位移由 MovementSystem + PhysicsSyncSystem（固定链）驱动；
    // 这里只剩"入场导向"脚手架——实体出盒后把速度分量反向（撞墙的自然阻挡交给物理）。
    if (m_ctx.ecs != nullptr && m_demoEntityCount > 0) {
        m_ctx.ecs->Registry().view<ecs::Position, ecs::Velocity>().each(
            [&](const ecs::Entity, const ecs::Position& p, ecs::Velocity& v) {
                if (p.x < m_demoBoxX0) {
                    v.x = std::fabs(v.x);
                } else if (p.x > m_demoBoxX1) {
                    v.x = -std::fabs(v.x);
                }
                if (p.y < m_demoBoxY0) {
                    v.y = std::fabs(v.y);
                } else if (p.y > m_demoBoxY1) {
                    v.y = -std::fabs(v.y);
                }
            });
    }
}

void WorldDemoState::OnFrame(double frameDt, double alpha) {
    (void)alpha;
    HandleFrameInput(frameDt);
    UpdateCruise(frameDt);

    // 流式加载：相机中心（世界像素 → 世界格坐标）
    const glm::vec2 camGrid = render::FromScreen(m_ctx.camera->Center(), 0);
    m_ctx.chunkManager->Update(camGrid.x, camGrid.y);
    m_ctx.chunkManager->ProcessResults();

    CollectWorld();
    // M4-T2：ECS 实体 → 同列表的"高度精灵"通道；之后统一全局画家序排序
    if (m_ctx.ecs != nullptr && m_ctx.spatialIndex != nullptr) {
        RenderCollectSystem::Collect(*m_ctx.ecs, *m_ctx.spatialIndex, *m_ctx.assets, m_view,
                                     m_items);
    }
    std::sort(m_items.begin(), m_items.end(), render::DrawItemLess);
    DrawWorld();
    DrawDebugOverlay();
    DrawPhysicsDebug();
    DrawDebugText();

    m_statsTimer += static_cast<float>(frameDt);
    if (m_statsTimer >= kStatsLogInterval) {
        m_statsTimer = 0.0f;
        const render::Renderer::Stats stats = m_ctx.renderer->GetStats();
        const auto& chunkStats = m_ctx.chunkManager->GetStats();
        ZL_INFO("render",
                "世界统计: FPS={:.0f} | 实例={} | 四边形={} | 批次={} | drawcall={} | zoom={:.2f} | "
                "chunk 驻留={} 加载中={} | 相机格=({:.0f},{:.0f}) | 累计加载={} 卸载={} 取消={} 失败={} | "
                "ECS={} 索引={} | 物理: 静态={} 动态={} step={:.2f}ms",
                stats.fps, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
                m_ctx.sprites->DrawCallsThisFrame(), stats.drawCalls, m_ctx.camera->Zoom(),
                chunkStats.loadedChunks, chunkStats.loadingChunks, camGrid.x, camGrid.y,
                chunkStats.totalLoaded, chunkStats.totalUnloaded, chunkStats.totalCancelled,
                chunkStats.totalFailed, m_ctx.ecs != nullptr ? m_ctx.ecs->AliveCount() : 0u,
                m_ctx.spatialIndex != nullptr ? m_ctx.spatialIndex->EntityCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->StaticBodyCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->DynamicBodyCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->AvgStepMs() : 0.0);
    }

    // 压测采样（巡航期间每 5s 一条 perf 行）
    m_perfTimer += static_cast<float>(frameDt);
    if (m_perfTimer >= kPerfLogInterval) {
        m_perfTimer = 0.0f;
        if (m_cruiseEnabled && !m_cruiseFinished) {
            LogPerfSample("cruise");
        }
    }
}

void WorldDemoState::UpdateCruise(double frameDt) {
    if (!m_cruiseEnabled || m_cruiseFinished) {
        return;
    }
    m_cruiseElapsed += frameDt;
    m_cruiseAngle += static_cast<float>(frameDt) * (m_cruiseSpeed / m_cruiseRadius);
    if (m_cruiseAngle > 2.0f * 3.14159265f) {
        m_cruiseAngle -= 2.0f * 3.14159265f;
    }
    const float gx = m_cruiseCenterX + m_cruiseRadius * std::cos(m_cruiseAngle);
    const float gy = m_cruiseCenterY + m_cruiseRadius * std::sin(m_cruiseAngle);
    m_ctx.camera->SetCenter(render::ToScreen(gx, gy));

    if (m_cruiseElapsed >= m_cruiseMinutes * 60.0) {
        m_cruiseFinished = true;
        LogPerfSample("cruise-done");
        const auto& chunkStats = m_ctx.chunkManager->GetStats();
        ZL_INFO("perf",
                "巡航结束：{:.0f}s | chunk 累计 加载={} 卸载={} 取消={} 失败={} | 驻留={}（回到稳态）",
                m_cruiseElapsed, chunkStats.totalLoaded, chunkStats.totalUnloaded,
                chunkStats.totalCancelled, chunkStats.totalFailed, chunkStats.loadedChunks);
    }
}

void WorldDemoState::LogPerfSample(const char* tag) {
    const render::Renderer::Stats stats = m_ctx.renderer->GetStats();
    const auto& chunkStats = m_ctx.chunkManager->GetStats();
    const double p50 = m_ctx.chunkManager->LoadMsPercentile(0.50);
    const double p95 = m_ctx.chunkManager->LoadMsPercentile(0.95);
    const double maxMs = m_ctx.chunkManager->LoadMsPercentile(1.0);
    ZL_INFO("perf",
            "[{}] t={:.0f}s | FPS={:.0f} | 实例={} 四边形={} | drawcall={} | chunk 驻留={} 加载中={} | "
            "单块解析 P50={:.2f}ms P95={:.2f}ms max={:.2f}ms（样本 {}）| 累计加载={} | "
            "物理 静态={} 动态={} step={:.2f}ms | 工作集={:.1f}MB",
            tag, m_cruiseElapsed, stats.fps, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
            stats.drawCalls, chunkStats.loadedChunks, chunkStats.loadingChunks, p50, p95, maxMs,
            m_ctx.chunkManager->RecentLoadMs().size(),
            static_cast<unsigned long long>(chunkStats.totalLoaded),
            m_ctx.physics != nullptr ? m_ctx.physics->StaticBodyCount() : 0u,
            m_ctx.physics != nullptr ? m_ctx.physics->DynamicBodyCount() : 0u,
            m_ctx.physics != nullptr ? m_ctx.physics->AvgStepMs() : 0.0,
            static_cast<double>(WorkingSetBytes()) / (1024.0 * 1024.0));
}

void WorldDemoState::HandleFrameInput(double frameDt) {
    core::Input& input = *m_ctx.input;
    core::EventBus& events = *m_ctx.events;
    render::Camera& camera = *m_ctx.camera;

    if (input.IsActionPressed("Quit")) {
        ZL_INFO("render", "退出请求（Quit）");
        events.Emit(EventProbe::kInputActionEvent, std::string("Quit"));
        m_ctx.window->SetShouldClose(true);
    }
    if (input.IsActionPressed("Pause")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("Pause"));
        core::GameTime& time = *m_ctx.gameTime;
        if (time.GetSpeed() == core::GameTime::Speed::Paused) {
            time.SetSpeed(core::GameTime::Speed::X1);
        } else {
            time.SetSpeed(core::GameTime::Speed::Paused);
        }
        ZL_INFO("render", "F1 暂停切换 → 当前 {}", core::GameTime::SpeedName(time.GetSpeed()));
        events.Emit(EventProbe::kSpeedChangedEvent, time.SpeedScale());
    }
    if (input.IsActionPressed("CycleSpeed")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("CycleSpeed"));
        m_ctx.gameTime->CycleSpeed();
        ZL_INFO("render", "F2 倍速循环 → 当前 {}",
                core::GameTime::SpeedName(m_ctx.gameTime->GetSpeed()));
        events.Emit(EventProbe::kSpeedChangedEvent, m_ctx.gameTime->SpeedScale());
    }
    if (input.IsActionPressed("ReloadBindings")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("ReloadBindings"));
        m_ctx.reloadConfigAndBindings();
    }
    if (input.IsActionPressed("ToggleDebugOverlay")) {
        m_showGrid = !m_showGrid;
        ZL_INFO("render", "F9 网格叠加: {}", m_showGrid ? "开" : "关");
    }
    if (input.IsActionPressed("TogglePhysicsDebug")) {
        m_physicsDebug = !m_physicsDebug;
        m_wireDiagCount = 0;
        if (m_ctx.physics != nullptr) {
            m_ctx.physics->SetDebugDraw(m_physicsDebug);
        }
        ZL_INFO("physics", "F10 物理线框: {}", m_physicsDebug ? "开" : "关");
    }
    if (input.IsActionPressed("ToggleCruise")) {
        m_cruiseEnabled = !m_cruiseEnabled;
        m_cruiseFinished = false;
        m_cruiseElapsed = 0.0;
        m_cruiseAngle = 0.0f;
        if (m_cruiseEnabled) {
            // 从圆周起点开始：以当前相机位置为圆心
            const glm::vec2 camGrid = render::FromScreen(camera.Center(), 0);
            m_cruiseCenterX = camGrid.x;
            m_cruiseCenterY = camGrid.y;
        }
        ZL_INFO("perf", "F8 自动巡航: {}（{:.1f} 分钟，速度 {:.0f} 格/s，半径 {:.0f}）",
                m_cruiseEnabled ? "开" : "关", m_cruiseMinutes, m_cruiseSpeed, m_cruiseRadius);
    }

    // 平移：WASD（屏幕空间速度恒定）
    glm::vec2 move(0.0f, 0.0f);
    if (input.IsActionDown("MoveUp")) {
        move.y -= 1.0f;
    }
    if (input.IsActionDown("MoveDown")) {
        move.y += 1.0f;
    }
    if (input.IsActionDown("MoveLeft")) {
        move.x -= 1.0f;
    }
    if (input.IsActionDown("MoveRight")) {
        move.x += 1.0f;
    }
    if (move.x != 0.0f || move.y != 0.0f) {
        camera.PanBy(glm::normalize(move) * (kPanSpeedPxPerSecond * static_cast<float>(frameDt) /
                                             camera.Zoom()));
    }

    // 拖拽平移（右键/中键）
    const glm::vec2 mouse(input.MouseX(), input.MouseY());
    const bool dragging = input.IsMouseButtonDown(1) || input.IsMouseButtonDown(2);
    if (dragging && m_prevMouseValid) {
        camera.PanBy(-(mouse - m_prevMouse) / camera.Zoom());
    }
    m_prevMouse = mouse;
    m_prevMouseValid = true;

    // 滚轮缩放（挡位吸附）
    m_scrollAccum += input.ScrollDeltaY();
    if (m_scrollAccum >= 1.0f) {
        camera.ZoomStep(1);
        m_scrollAccum = 0.0f;
    } else if (m_scrollAccum <= -1.0f) {
        camera.ZoomStep(-1);
        m_scrollAccum = 0.0f;
    }

    // 悬停拾取（无限世界：hit 恒真）
    const render::GridPick pick = PickMouseCell(mouse);
    m_hoverHit = pick.hit;
    m_hoverX = pick.x;
    m_hoverY = pick.y;
}

glm::vec2 WorldDemoState::MouseToFramebuffer(const glm::vec2& mousePx) const {
    const int winW = std::max(1, m_ctx.window->Width());
    const int winH = std::max(1, m_ctx.window->Height());
    const float sx = static_cast<float>(m_ctx.renderer->FramebufferWidth()) /
                     static_cast<float>(winW);
    const float sy = static_cast<float>(m_ctx.renderer->FramebufferHeight()) /
                     static_cast<float>(winH);
    return {mousePx.x * sx, mousePx.y * sy};
}

render::GridPick WorldDemoState::PickMouseCell(const glm::vec2& mousePx) const {
    const glm::vec2 fb = MouseToFramebuffer(mousePx);
    const glm::vec2 world = m_ctx.camera->ScreenToWorld(fb);
    return render::PickCellUnbounded(world, 0);
}

world::Chunk* WorldDemoState::FindChunk(int chunkX, int chunkY) const {
    const world::CellCoord cell{world::FloorDiv(chunkX, core::kChunksPerCell),
                                world::FloorDiv(chunkY, core::kChunksPerCell)};
    world::Cell* owner = m_ctx.world->GetCell(cell.cx, cell.cy);
    if (owner == nullptr) {
        return nullptr;
    }
    const world::ChunkLocal local = world::ChunkLocalOf(chunkX, chunkY);
    return owner->GetChunk(local.lcx, local.lcy);
}

void WorldDemoState::PushObject(const world::SquareObject& object, int wx, int wy, int z,
                                world::SquareLayer layer) {
    const std::vector<AssetProvider::Entry>* frames = m_ctx.assets->Frames(object.sprite);
    if (frames == nullptr || frames->empty()) {
        return;  // 逻辑名缺失（应在 manifest 检查，运行时静默跳过）
    }
    const AssetProvider::Entry& entry =
        (*frames)[render::HashPick(wx, wy, static_cast<uint32_t>(layer)) % frames->size()];
    if (entry.atlas == nullptr || entry.frame == nullptr) {
        return;
    }
    const render::AtlasFrame& frame = *entry.frame;

    render::DrawItem item;
    item.texture = entry.atlas->Texture();
    item.rect = render::SpriteDrawRect(static_cast<float>(wx), static_cast<float>(wy), z, frame.fx,
                                       frame.fy, frame.ox, frame.oy, frame.w, frame.h);
    item.rect.x += object.offsetX;
    item.rect.y += object.offsetY;
    item.u0 = frame.u0;
    item.v0 = frame.v0;
    item.u1 = frame.u1;
    item.v1 = frame.v1;
    item.color = z >= 1 ? kUpperZColor : 0xffffffff;
    // 两段式：仅 z=0 的地面进"地面批"；其余（含 z≥1 的一切）走全局画家序
    item.stage = (z == 0 && layer == world::SquareLayer::Floor) ? 0 : 1;
    item.z = z;
    item.diag = wx + wy;
    item.gx = wx;
    item.layer = static_cast<int>(layer);
    item.tieBreaker = 0;  // 世界对象同格同层视为无序（0）
    m_items.push_back(item);
}

void WorldDemoState::CollectWorld() {
    m_items.clear();
    m_collectedChunks = 0;
    m_missingChunks = 0;

    render::Camera& camera = *m_ctx.camera;
    const glm::vec2 half = camera.VisibleSize() * 0.5f + glm::vec2(192.0f, 512.0f);
    const glm::vec2 minW = camera.Center() - half;
    const glm::vec2 maxW = camera.Center() + half;

    // 可见范围换算到格坐标（四角反解，留 1 格余量；含树高/墙高余量已在上面的 half 里）
    const glm::vec2 g0 = render::FromScreen(minW, 0);
    const glm::vec2 g1 = render::FromScreen(maxW, 0);
    const glm::vec2 g2 = render::FromScreen({minW.x, maxW.y}, 0);
    const glm::vec2 g3 = render::FromScreen({maxW.x, minW.y}, 0);
    m_view.gxMin = static_cast<int>(std::floor(std::min({g0.x, g1.x, g2.x, g3.x}))) - 1;
    m_view.gxMax = static_cast<int>(std::ceil(std::max({g0.x, g1.x, g2.x, g3.x}))) + 1;
    m_view.gyMin = static_cast<int>(std::floor(std::min({g0.y, g1.y, g2.y, g3.y}))) - 1;
    m_view.gyMax = static_cast<int>(std::ceil(std::max({g0.y, g1.y, g2.y, g3.y}))) + 1;
    const int gxMin = m_view.gxMin;
    const int gxMax = m_view.gxMax;
    const int gyMin = m_view.gyMin;
    const int gyMax = m_view.gyMax;

    const int cx0 = world::FloorDiv(gxMin, core::kChunkSize);
    const int cx1 = world::FloorDiv(gxMax, core::kChunkSize);
    const int cy0 = world::FloorDiv(gyMin, core::kChunkSize);
    const int cy1 = world::FloorDiv(gyMax, core::kChunkSize);

    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            world::Chunk* chunk = FindChunk(cx, cy);
            if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
                ++m_missingChunks;  // 后台还在加载：本帧跳过（下帧补齐）
                continue;
            }
            ++m_collectedChunks;

            // 逐 z 层收集（z=0 地面起步；z≥1 楼上，空层快速跳过）
            for (int z = 0; z < core::kMaxZ; ++z) {
                for (int ly = 0; ly < core::kChunkSize; ++ly) {
                    const int wy = cy * core::kChunkSize + ly;
                    if (wy < gyMin || wy > gyMax) {
                        continue;
                    }
                    for (int lx = 0; lx < core::kChunkSize; ++lx) {
                        const int wx = cx * core::kChunkSize + lx;
                        if (wx < gxMin || wx > gxMax) {
                            continue;
                        }
                        const world::GridSquare& square = chunk->Square(lx, ly, z);
                        if (square.IsEmpty()) {
                            continue;
                        }
                        for (int layerIndex = 0; layerIndex < world::kSquareLayerCount;
                             ++layerIndex) {
                            const auto layer = static_cast<world::SquareLayer>(layerIndex);
                            for (const world::SquareObject& object : square.Layer(layer)) {
                                PushObject(object, wx, wy, z, layer);
                            }
                        }
                    }
                }
            }
        }
    }

    // 排序：地面批 + 对象批统一画家序（含 M4-T2 追加的实体高度精灵），见 OnFrame
}

void WorldDemoState::DrawWorld() {
    render::Camera& camera = *m_ctx.camera;
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjMatrix();
    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::World), &view, &proj);

    const glm::vec2 half = camera.VisibleSize() * 0.5f + glm::vec2(192.0f, 512.0f);
    const glm::vec2 minW = camera.Center() - half;
    const glm::vec2 maxW = camera.Center() + half;

    render::SpriteBatch& batch = *m_ctx.sprites;
    batch.Begin(static_cast<bgfx::ViewId>(render::ViewId::World));
    for (const render::DrawItem& item : m_items) {
        const render::SpriteRect& rect = item.rect;
        if (rect.x + rect.w < minW.x || rect.x > maxW.x || rect.y + rect.h < minW.y ||
            rect.y > maxW.y) {
            continue;
        }
        batch.DrawSprite(item.texture, rect, item.u0, item.v0, item.u1, item.v1, item.color);
    }
    batch.End();
}

void WorldDemoState::DrawDebugOverlay() {
    if (!m_showGrid && !m_hoverHit) {
        return;
    }
    render::Camera& camera = *m_ctx.camera;
    render::SpriteBatch& batch = *m_ctx.sprites;

    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjMatrix();
    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::Debug), &view, &proj);

    const glm::vec2 half = camera.VisibleSize() * 0.5f;
    const glm::vec2 minW = camera.Center() - half;
    const glm::vec2 maxW = camera.Center() + half;
    const glm::vec2 g0 = render::FromScreen(minW, 0);
    const glm::vec2 g1 = render::FromScreen(maxW, 0);
    const glm::vec2 g2 = render::FromScreen({minW.x, maxW.y}, 0);
    const glm::vec2 g3 = render::FromScreen({maxW.x, minW.y}, 0);
    const int gxMin = static_cast<int>(std::floor(std::min({g0.x, g1.x, g2.x, g3.x}))) - 1;
    const int gxMax = static_cast<int>(std::ceil(std::max({g0.x, g1.x, g2.x, g3.x}))) + 1;
    const int gyMin = static_cast<int>(std::floor(std::min({g0.y, g1.y, g2.y, g3.y}))) - 1;
    const int gyMax = static_cast<int>(std::ceil(std::max({g0.y, g1.y, g2.y, g3.y}))) + 1;

    batch.Begin(static_cast<bgfx::ViewId>(render::ViewId::Debug));
    if (m_showGrid) {
        for (int gx = gxMin; gx <= gxMax; ++gx) {
            const float f = static_cast<float>(gx) - 0.5f;
            batch.DrawLine(render::ToScreen(f, static_cast<float>(gyMin) - 0.5f),
                           render::ToScreen(f, static_cast<float>(gyMax) - 0.5f),
                           kGridLineThickness, kGridColor);
        }
        for (int gy = gyMin; gy <= gyMax; ++gy) {
            const float f = static_cast<float>(gy) - 0.5f;
            batch.DrawLine(render::ToScreen(static_cast<float>(gxMin) - 0.5f, f),
                           render::ToScreen(static_cast<float>(gxMax) - 0.5f, f),
                           kGridLineThickness, kGridColor);
        }
        batch.DrawLine(render::ToScreen(0.0f, 0.0f), render::ToScreen(6.0f, 0.0f), 3.0f,
                       kAxisXColor);
        batch.DrawLine(render::ToScreen(0.0f, 0.0f), render::ToScreen(0.0f, 6.0f), 3.0f,
                       kAxisYColor);
    }
    if (m_hoverHit) {
        const glm::vec2 center =
            render::ToScreen(static_cast<float>(m_hoverX), static_cast<float>(m_hoverY));
        const glm::vec2 top = center + glm::vec2(0.0f, -16.0f);
        const glm::vec2 right = center + glm::vec2(32.0f, 0.0f);
        const glm::vec2 bottom = center + glm::vec2(0.0f, 16.0f);
        const glm::vec2 left = center + glm::vec2(-32.0f, 0.0f);
        batch.DrawLine(top, right, kHoverLineThickness, kHoverColor);
        batch.DrawLine(right, bottom, kHoverLineThickness, kHoverColor);
        batch.DrawLine(bottom, left, kHoverLineThickness, kHoverColor);
        batch.DrawLine(left, top, kHoverLineThickness, kHoverColor);
    }
    batch.End();
}

void WorldDemoState::DrawPhysicsDebug() {
    if (m_ctx.physics == nullptr || !m_ctx.physics->DebugDrawEnabled()) {
        return;
    }
    const std::vector<physics::PhysicsWorld::DebugLine>& lines = m_ctx.physics->DebugLines();
    if (lines.empty()) {
        return;
    }
    const std::vector<AssetProvider::Entry>* frames = m_ctx.assets->Frames("ui.pixel");
    if (frames == nullptr || frames->empty() || (*frames)[0].atlas == nullptr) {
        return;
    }
    const AssetProvider::Entry& entry = (*frames)[0];
    const render::AtlasFrame& white = *entry.frame;

    render::Camera& camera = *m_ctx.camera;
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjMatrix();
    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::Debug), &view, &proj);

    render::SpriteBatch& batch = *m_ctx.sprites;
    batch.Begin(static_cast<bgfx::ViewId>(render::ViewId::Debug));

    // 线段 → 采样点四边形的集合（SpriteBatch 只画轴对齐四边形，点步进对任意角度都成立）。
    // 注意：线框四边形数量是"线段像素长度 / 采样间距"量级（实测 Bullet 一次 ~7000 条线段），
    // 必须做可见剔除 + 帧预算，否则会打爆 SpriteBatch 的单批上限（16384 四边形/帧）。
    constexpr float kDotSize = 3.4f;   // 单个采样点（四边形）边长
    constexpr float kDotStep = 10.0f;  // 采样间距（线框呈虚线感，调试足够）
    constexpr int kMaxWireQuads = 13500;
    const glm::vec2 half = camera.VisibleSize() * 0.5f + glm::vec2(160.0f, 320.0f);
    const glm::vec2 minW = camera.Center() - half;
    const glm::vec2 maxW = camera.Center() + half;
    constexpr float kFloorPx = static_cast<float>(core::kFloorPx);

    int quads = 0;
    for (const physics::PhysicsWorld::DebugLine& line : lines) {
        if (quads >= kMaxWireQuads) {
            break;
        }
        const glm::vec2 a = render::ToScreen(line.x0, line.y0) - glm::vec2(0.0f, line.z0 * kFloorPx);
        const glm::vec2 b = render::ToScreen(line.x1, line.y1) - glm::vec2(0.0f, line.z1 * kFloorPx);
        // 屏幕 AABB 剔除（完全在视野外的线段直接跳过）
        if (std::min(a.x, b.x) > maxW.x || std::max(a.x, b.x) < minW.x ||
            std::min(a.y, b.y) > maxW.y || std::max(a.y, b.y) < minW.y) {
            continue;
        }
        const glm::vec2 delta = b - a;
        const float length = glm::length(delta);
        const int steps = static_cast<int>(length / kDotStep) + 1;
        for (int i = 0; i <= steps && quads < kMaxWireQuads; ++i, ++quads) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const glm::vec2 point = a + delta * t;
            batch.DrawSprite(entry.atlas->Texture(),
                             {point.x - kDotSize * 0.5f, point.y - kDotSize * 0.5f, kDotSize,
                              kDotSize},
                             white.u0, white.v0, white.u1, white.v1, line.color);
        }
    }
    batch.End();

    ++m_wireDiagCount;
    if (m_wireDiagCount <= 2) {
        ZL_INFO("physics", "线框诊断：线段 {} 条，绘制四边形 {} 个", lines.size(), quads);
    }
}

void WorldDemoState::DrawDebugText() {
    const render::Renderer::Stats stats = m_ctx.renderer->GetStats();
    const auto& chunkStats = m_ctx.chunkManager->GetStats();
    const auto& now = m_ctx.gameTime->Now();
    const std::string timeText = core::FormatDateTime(now);
    const glm::vec2 camGrid = render::FromScreen(m_ctx.camera->Center(), 0);

    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(0, 1, 0x0f, "ZomboidLike M3 | State: %s | backend: %s", Name(),
                        m_ctx.renderer->BackendName());
    bgfx::dbgTextPrintf(0, 2, 0x0f, "FPS: %.0f | TPS: %.0f | frame: %.1fms | drawcall: %u",
                        m_ctx.loop->Fps(), m_ctx.loop->Tps(), m_ctx.loop->LastFrameMs(),
                        stats.drawCalls);
    bgfx::dbgTextPrintf(0, 3, 0x0f, "tick: %llu | %s | speed: %s | %s",
                        static_cast<unsigned long long>(m_ctx.loop->TotalTicks()),
                        timeText.c_str(), core::GameTime::SpeedName(m_ctx.gameTime->GetSpeed()),
                        m_ctx.gameTime->IsNight() ? "night" : "day");
    bgfx::dbgTextPrintf(0, 4, 0x0f, "world: camGrid=(%.0f,%.0f) | instances: %zu | quads: %u | batches: %u",
                        camGrid.x, camGrid.y, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
                        m_ctx.sprites->DrawCallsThisFrame());
    bgfx::dbgTextPrintf(0, 5, 0x0f, "chunks: resident=%d loading=%d | visible=%d missing=%d | total: +%llu -%llu cancel=%llu",
                        chunkStats.loadedChunks, chunkStats.loadingChunks, m_collectedChunks,
                        m_missingChunks, static_cast<unsigned long long>(chunkStats.totalLoaded),
                        static_cast<unsigned long long>(chunkStats.totalUnloaded),
                        static_cast<unsigned long long>(chunkStats.totalCancelled));
    bgfx::dbgTextPrintf(0, 6, 0x0f, "zoom: %.2f | hover: (%d, %d) | provider: %s",
                        m_ctx.camera->Zoom(), m_hoverX, m_hoverY,
                        m_ctx.assets->Provider().c_str());
    bgfx::dbgTextPrintf(0, 8, 0x07,
                        "WASD/RMB drag pan | wheel zoom | F9 grid | F1 pause | F2 speed | F5 reload | ESC quit");
}

}  // namespace zl::game
