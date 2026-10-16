#include "game/states/WorldDemoState.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/Config.h"
#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "core/Input.h"
#include "core/Log.h"
#include "game/EventProbe.h"
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
constexpr uint32_t kGridColor = 0x30ffffff;   // 白色低透明（ABGR: A=0x30）
constexpr uint32_t kHoverColor = 0xff30e0ff;  // 黄色高亮
constexpr uint32_t kAxisXColor = 0xc04040ff;  // 红
constexpr uint32_t kAxisYColor = 0xc040ff40;  // 绿
// 上层（z≥1）透明度策略（M3-T6 临时方案）：楼上整体 35% 不透明，看得到室内
constexpr uint32_t kUpperZColor = 0x59ffffff;

// 变体选取哈希：同一格每帧稳定（不闪烁），不同格/层/盐值分布不同
uint32_t HashPick(int x, int y, uint32_t salt) {
    uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^
                 salt * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
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

    ZL_INFO("render", "按键：WASD/右键拖拽 平移 | 滚轮 缩放 | F9 网格线 | F1 暂停 | F2 倍速 | F5 热重载 | ESC 退出");
}

void WorldDemoState::OnExit() {
    const auto& stats = m_ctx.chunkManager->GetStats();
    ZL_INFO("render", "退出 WorldDemo：本状态共 {} tick；chunk 累计 加载 {} / 卸载 {} / 取消 {}",
            m_ticksInState, stats.totalLoaded, stats.totalUnloaded, stats.totalCancelled);
}

void WorldDemoState::OnTick(double tickDt) {
    (void)tickDt;
    ++m_ticksInState;
}

void WorldDemoState::OnFrame(double frameDt, double alpha) {
    (void)alpha;
    HandleFrameInput(frameDt);

    // 流式加载：相机中心（世界像素 → 世界格坐标）
    const glm::vec2 camGrid = render::FromScreen(m_ctx.camera->Center(), 0);
    m_ctx.chunkManager->Update(camGrid.x, camGrid.y);
    m_ctx.chunkManager->ProcessResults();

    CollectWorld();
    DrawWorld();
    DrawDebugOverlay();
    DrawDebugText();

    m_statsTimer += static_cast<float>(frameDt);
    if (m_statsTimer >= kStatsLogInterval) {
        m_statsTimer = 0.0f;
        const render::Renderer::Stats stats = m_ctx.renderer->GetStats();
        const auto& chunkStats = m_ctx.chunkManager->GetStats();
        ZL_INFO("render",
                "世界统计: FPS={:.0f} | 实例={} | 四边形={} | 批次={} | drawcall={} | zoom={:.2f} | "
                "chunk 驻留={} 加载中={} | 相机格=({:.0f},{:.0f}) | 累计加载={} 卸载={} 取消={} 失败={}",
                stats.fps, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
                m_ctx.sprites->DrawCallsThisFrame(), stats.drawCalls, m_ctx.camera->Zoom(),
                chunkStats.loadedChunks, chunkStats.loadingChunks, camGrid.x, camGrid.y,
                chunkStats.totalLoaded, chunkStats.totalUnloaded, chunkStats.totalCancelled,
                chunkStats.totalFailed);
    }
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
        (*frames)[HashPick(wx, wy, static_cast<uint32_t>(layer)) % frames->size()];
    if (entry.atlas == nullptr || entry.frame == nullptr) {
        return;
    }
    const render::AtlasFrame& frame = *entry.frame;

    DrawItem item;
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
    const int gxMin = static_cast<int>(std::floor(std::min({g0.x, g1.x, g2.x, g3.x}))) - 1;
    const int gxMax = static_cast<int>(std::ceil(std::max({g0.x, g1.x, g2.x, g3.x}))) + 1;
    const int gyMin = static_cast<int>(std::floor(std::min({g0.y, g1.y, g2.y, g3.y}))) - 1;
    const int gyMax = static_cast<int>(std::ceil(std::max({g0.y, g1.y, g2.y, g3.y}))) + 1;

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

    // 两段排序：地面批先画（stage 0，内部顺序无关紧要）；其余按全局画家序
    std::sort(m_items.begin(), m_items.end(), [](const DrawItem& a, const DrawItem& b) {
        if (a.stage != b.stage) {
            return a.stage < b.stage;
        }
        if (a.z != b.z) {
            return a.z < b.z;
        }
        if (a.diag != b.diag) {
            return a.diag < b.diag;
        }
        if (a.gx != b.gx) {
            return a.gx < b.gx;
        }
        return a.layer < b.layer;
    });
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
    for (const DrawItem& item : m_items) {
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
