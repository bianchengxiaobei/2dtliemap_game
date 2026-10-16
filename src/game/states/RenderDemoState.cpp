#include "game/states/RenderDemoState.h"

#include <algorithm>
#include <cmath>
#include <random>
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

namespace zl::game {
namespace {

constexpr float kPanSpeedPxPerSecond = 900.0f;
constexpr float kGridLineThickness = 1.2f;
constexpr float kHoverLineThickness = 2.5f;
constexpr float kStatsLogInterval = 5.0f;
constexpr uint32_t kGridColor = 0x30ffffff;    // 白色低透明（ABGR: A=0x30）
constexpr uint32_t kHoverColor = 0xff30e0ff;   // 黄色高亮
constexpr uint32_t kAxisXColor = 0xc04040ff;   // 红
constexpr uint32_t kAxisYColor = 0xc040ff40;   // 绿

// 相机边界留白：上要容下树高（256+），左右/下留一点
constexpr glm::vec2 kBoundsMarginMin(-96.0f, -384.0f);
constexpr glm::vec2 kBoundsMarginMax(96.0f, 128.0f);

}  // namespace

void RenderDemoState::OnEnter() {
    core::Config& config = *m_ctx.config;
    ZL_INFO("render", "进入 RenderDemo（M2 渲染地基演示）");

    // 资源：清单 + provider（own / placeholder_pz）
    const std::string manifest =
        config.Get<std::string>("assets.manifest", "assets/asset_manifest.json");
    const std::string provider = config.Get<std::string>("assets.provider", "own");
    if (!m_ctx.assets->Init(manifest, provider, *m_ctx.atlases)) {
        ZL_ERROR("render", "资源初始化失败，地图将为空");
    }

    m_mapW = std::max(4, config.Get<int>("renderDemo.mapSize", 32));
    m_mapH = m_mapW;
    BuildMap();

    // 相机：对准地图中心，边界 = 地图包围盒 + 留白
    render::Camera& camera = *m_ctx.camera;
    camera.SetViewport(m_ctx.renderer->FramebufferWidth(), m_ctx.renderer->FramebufferHeight());
    const glm::vec2 worldMin =
        render::ToScreen(0.0f, static_cast<float>(m_mapH - 1)) + kBoundsMarginMin;
    const glm::vec2 worldMax =
        render::ToScreen(static_cast<float>(m_mapW - 1), 0.0f) + kBoundsMarginMax;
    camera.SetBounds(worldMin, worldMax);
    camera.SetCenter(render::ToScreen((static_cast<float>(m_mapW) - 1.0f) * 0.5f,
                                      (static_cast<float>(m_mapH) - 1.0f) * 0.5f));

    ZL_INFO("render", "按键：WASD/右键拖拽 平移 | 滚轮 缩放 | F9 网格线 | F1 暂停 | F2 倍速 | F5 热重载 | ESC 退出");
}

void RenderDemoState::OnExit() {
    ZL_INFO("render", "退出 RenderDemo：本状态共 {} tick，实例 {} 个", m_ticksInState,
            m_items.size());
}

void RenderDemoState::OnTick(double tickDt) {
    (void)tickDt;
    ++m_ticksInState;
}

void RenderDemoState::BuildMap() {
    core::Config& config = *m_ctx.config;
    const uint32_t seed = static_cast<uint32_t>(config.Get<int>("renderDemo.seed", 42));
    const bool decorations = config.Get<bool>("renderDemo.decorations", true);

    m_items.clear();
    m_items.reserve(static_cast<size_t>(m_mapW) * m_mapH * 2 + 64);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> cellDist(0, m_mapW * m_mapH - 1);

    // 树：32×32 地图 10 棵（面积/100），大树
    m_treeCount = std::max(1, (m_mapW * m_mapH) / 100);
    std::vector<char> hasTree(static_cast<size_t>(m_mapW) * m_mapH, 0);
    for (int i = 0; i < m_treeCount; ++i) {
        hasTree[static_cast<size_t>(cellDist(rng))] = 1;
    }

    int floorCount = 0;
    int decorCount = 0;
    for (int gy = 0; gy < m_mapH; ++gy) {
        for (int gx = 0; gx < m_mapW; ++gx) {
            const bool dirt = (rng() % 100) < 8;
            const AssetProvider::Entry floorEntry =
                m_ctx.assets->Pick(dirt ? "tile.floor.dirt" : "tile.floor.grass", rng);
            AddItem(floorEntry, gx, gy, 0, Layer::Floor);
            ++floorCount;

            if (decorations && !dirt && (rng() % 100) < 45) {
                AddItem(m_ctx.assets->Pick("decor.grass_tuft", rng), gx, gy, 0, Layer::Decor);
                ++decorCount;
            }

            if (hasTree[static_cast<size_t>(gy) * m_mapW + gx] != 0) {
                AddItem(m_ctx.assets->Pick("object.tree", rng), gx, gy, 0, Layer::Object);
            }
        }
    }

    // 画家算法排序（docs/00 §5.3）：z 升序 → (x+y) 升序 → x 升序 → 格内层序
    std::stable_sort(m_items.begin(), m_items.end(), [](const DrawItem& a, const DrawItem& b) {
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

    ZL_INFO("render", "测试地图构建完成: {}x{}，地面 {}，草丛 {}，树 {}，实例总数 {}", m_mapW,
            m_mapH, floorCount, decorCount, m_treeCount, m_items.size());
}

void RenderDemoState::AddItem(const AssetProvider::Entry& entry, int gx, int gy, int z,
                              Layer layer) {
    if (entry.atlas == nullptr || entry.frame == nullptr) {
        return;
    }
    const render::AtlasFrame& f = *entry.frame;
    DrawItem item;
    item.texture = entry.atlas->Texture();
    item.rect = render::SpriteDrawRect(static_cast<float>(gx), static_cast<float>(gy), z, f.fx,
                                       f.fy, f.ox, f.oy, f.w, f.h);
    item.u0 = f.u0;
    item.v0 = f.v0;
    item.u1 = f.u1;
    item.v1 = f.v1;
    item.color = 0xffffffff;
    item.z = z;
    item.diag = gx + gy;
    item.gx = gx;
    item.layer = static_cast<int>(layer);
    m_items.push_back(item);
}

void RenderDemoState::OnFrame(double frameDt, double alpha) {
    (void)alpha;
    HandleFrameInput(frameDt);

    m_statsTimer += static_cast<float>(frameDt);
    DrawWorld();
    DrawDebugOverlay();
    DrawDebugText();

    if (m_statsTimer >= kStatsLogInterval) {
        m_statsTimer = 0.0f;
        const render::Renderer::Stats stats = m_ctx.renderer->GetStats();
        ZL_INFO("render",
                "统计: FPS={:.0f} | 实例={} | SpriteBatch 四边形={} | 批次={} | bgfx drawcall={} | 缩放={:.2f}",
                stats.fps, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
                m_ctx.sprites->DrawCallsThisFrame(), stats.drawCalls, m_ctx.camera->Zoom());
    }
}

void RenderDemoState::HandleFrameInput(double frameDt) {
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

    // 悬停拾取（验收：任意缩放/平移组合下与 IsoMath 反解一致）
    const render::GridPick pick = PickMouseCell(mouse);
    if (pick.hit != m_hoverHit || pick.x != m_hoverX || pick.y != m_hoverY) {
        const glm::vec2 fb = MouseToFramebuffer(mouse);
        const glm::vec2 world = m_ctx.camera->ScreenToWorld(fb);
        const glm::vec2 dc = render::ToScreen(static_cast<float>(pick.x), static_cast<float>(pick.y));
        ZL_INFO("render",
                "hover 链路: mouse=({:.1f},{:.1f}) fb=({:.1f},{:.1f}) world=({:.1f},{:.1f}) cell=({},{}) hit={} drawCenter=({:.1f},{:.1f}) cam=({:.1f},{:.1f}) zoom={:.2f}",
                mouse.x, mouse.y, fb.x, fb.y, world.x, world.y, pick.x, pick.y, pick.hit ? 1 : 0,
                dc.x, dc.y, m_ctx.camera->Center().x, m_ctx.camera->Center().y,
                m_ctx.camera->Zoom());
    }
    m_hoverHit = pick.hit;
    m_hoverX = pick.x;
    m_hoverY = pick.y;
}

glm::vec2 RenderDemoState::MouseToFramebuffer(const glm::vec2& mousePx) const {
    const int winW = std::max(1, m_ctx.window->Width());
    const int winH = std::max(1, m_ctx.window->Height());
    const float sx = static_cast<float>(m_ctx.renderer->FramebufferWidth()) /
                     static_cast<float>(winW);
    const float sy = static_cast<float>(m_ctx.renderer->FramebufferHeight()) /
                     static_cast<float>(winH);
    return {mousePx.x * sx, mousePx.y * sy};
}

render::GridPick RenderDemoState::PickMouseCell(const glm::vec2& mousePx) const {
    const glm::vec2 fb = MouseToFramebuffer(mousePx);
    const glm::vec2 world = m_ctx.camera->ScreenToWorld(fb);
    return render::PickCell(world, 0, m_mapW, m_mapH);
}

void RenderDemoState::DrawWorld() {
    render::Camera& camera = *m_ctx.camera;
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjMatrix();
    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::World), &view, &proj);

    // 可见矩形裁剪（含树高/位移余量）
    const glm::vec2 half = camera.VisibleSize() * 0.5f + glm::vec2(160.0f, 384.0f);
    const glm::vec2 minW = camera.Center() - half;
    const glm::vec2 maxW = camera.Center() + half;

    render::SpriteBatch& batch = *m_ctx.sprites;
    batch.Begin(static_cast<bgfx::ViewId>(render::ViewId::World));
    for (const DrawItem& item : m_items) {
        const render::SpriteRect& r = item.rect;
        if (r.x + r.w < minW.x || r.x > maxW.x || r.y + r.h < minW.y || r.y > maxW.y) {
            continue;
        }
        batch.DrawSprite(item.texture, r, item.u0, item.v0, item.u1, item.v1, item.color);
    }
    batch.End();
}

void RenderDemoState::DrawDebugOverlay() {
    if (!m_showGrid && !m_hoverHit) {
        return;
    }
    render::Camera& camera = *m_ctx.camera;
    render::SpriteBatch& batch = *m_ctx.sprites;

    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjMatrix();
    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::Debug), &view, &proj);

    // 可见范围换算到格坐标（留 1 格余量）
    const glm::vec2 half = camera.VisibleSize() * 0.5f;
    const glm::vec2 minW = camera.Center() - half;
    const glm::vec2 maxW = camera.Center() + half;
    const glm::vec2 g0 = render::FromScreen(minW, 0);
    const glm::vec2 g1 = render::FromScreen(maxW, 0);
    const glm::vec2 g2 = render::FromScreen({minW.x, maxW.y}, 0);
    const glm::vec2 g3 = render::FromScreen({maxW.x, minW.y}, 0);
    const float gxMinF = std::min({g0.x, g1.x, g2.x, g3.x});
    const float gxMaxF = std::max({g0.x, g1.x, g2.x, g3.x});
    const float gyMinF = std::min({g0.y, g1.y, g2.y, g3.y});
    const float gyMaxF = std::max({g0.y, g1.y, g2.y, g3.y});
    const int gxMin = std::max(static_cast<int>(std::floor(gxMinF)) - 1, 0);
    const int gxMax = std::min(static_cast<int>(std::ceil(gxMaxF)) + 1, m_mapW);
    const int gyMin = std::max(static_cast<int>(std::floor(gyMinF)) - 1, 0);
    const int gyMax = std::min(static_cast<int>(std::ceil(gyMaxF)) + 1, m_mapH);

    batch.Begin(static_cast<bgfx::ViewId>(render::ViewId::Debug));
    if (m_showGrid) {
        // 网格线：画在格边界（整数格坐标 + 0.5）上，与菱形边重合
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

        // 坐标轴：+x 红，+y 绿
        batch.DrawLine(render::ToScreen(0.0f, 0.0f), render::ToScreen(6.0f, 0.0f), 3.0f, kAxisXColor);
        batch.DrawLine(render::ToScreen(0.0f, 0.0f), render::ToScreen(0.0f, 6.0f), 3.0f, kAxisYColor);
    }
    if (m_hoverHit) {
        // 悬停格菱形描边（与拾取反解一致）
        const glm::vec2 c = render::ToScreen(static_cast<float>(m_hoverX), static_cast<float>(m_hoverY));
        const glm::vec2 top = c + glm::vec2(0.0f, -16.0f);
        const glm::vec2 right = c + glm::vec2(32.0f, 0.0f);
        const glm::vec2 bottom = c + glm::vec2(0.0f, 16.0f);
        const glm::vec2 left = c + glm::vec2(-32.0f, 0.0f);
        batch.DrawLine(top, right, kHoverLineThickness, kHoverColor);
        batch.DrawLine(right, bottom, kHoverLineThickness, kHoverColor);
        batch.DrawLine(bottom, left, kHoverLineThickness, kHoverColor);
        batch.DrawLine(left, top, kHoverLineThickness, kHoverColor);
    }
    batch.End();
}

void RenderDemoState::DrawDebugText() {
    const render::Renderer::Stats stats = m_ctx.renderer->GetStats();
    const auto& now = m_ctx.gameTime->Now();
    const std::string timeText = core::FormatDateTime(now);

    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(0, 1, 0x0f, "ZomboidLike M2 | State: %s | backend: %s", Name(),
                        m_ctx.renderer->BackendName());
    bgfx::dbgTextPrintf(0, 2, 0x0f, "FPS: %.0f | TPS: %.0f | frame: %.1fms | drawcall: %u",
                        m_ctx.loop->Fps(), m_ctx.loop->Tps(), m_ctx.loop->LastFrameMs(),
                        stats.drawCalls);
    bgfx::dbgTextPrintf(0, 3, 0x0f, "tick: %llu | %s | speed: %s | %s",
                        static_cast<unsigned long long>(m_ctx.loop->TotalTicks()),
                        timeText.c_str(), core::GameTime::SpeedName(m_ctx.gameTime->GetSpeed()),
                        m_ctx.gameTime->IsNight() ? "night" : "day");
    bgfx::dbgTextPrintf(0, 4, 0x0f, "map: %dx%d | instances: %zu | quads: %u | batches: %u", m_mapW,
                        m_mapH, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
                        m_ctx.sprites->DrawCallsThisFrame());
    bgfx::dbgTextPrintf(0, 5, 0x0f, "zoom: %.2f | hover: (%d, %d) %s | provider: %s",
                        m_ctx.camera->Zoom(), m_hoverX, m_hoverY,
                        m_hoverHit ? "hit" : "miss", m_ctx.assets->Provider().c_str());
    bgfx::dbgTextPrintf(0, 7, 0x07,
                        "WASD/RMB drag pan | wheel zoom | F9 grid | F1 pause | F2 speed | F5 reload | ESC quit");
}

}  // namespace zl::game
