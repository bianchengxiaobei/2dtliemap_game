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
#include "ai/PathfindingService.h"
#include "ai/Passability.h"
#include "ai/VirtualHorde.h"
#include "ai/components/AiComponents.h"
#include "ai/systems/AiSystem.h"
#include "ai/systems/LodSystem.h"
#include "ai/systems/PerceptionSystem.h"
#include "ai/systems/SpawnLodSystem.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/AnimationSystem.h"
#include "characters/systems/CombatSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/SpatialIndex.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/EventProbe.h"
#include "game/CameraFollow.h"
#include "game/WorldQueries.h"
#include "game/ZombieFactory.h"
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
constexpr float kMarkerLineThickness = 3.0f;
constexpr float kStatsLogInterval = 3.0f;
constexpr float kPerfLogInterval = 5.0f;
constexpr uint32_t kGridColor = 0x30ffffff;   // 白色低透明（ABGR: A=0x30）
constexpr uint32_t kHoverColor = 0xff30e0ff;  // 黄色高亮
constexpr uint32_t kAxisXColor = 0xc04040ff;  // 红
constexpr uint32_t kAxisYColor = 0xc040ff40;  // 绿
// 上层（z≥1）透明度策略（M3-T6 临时方案）：楼上整体 35% 不透明，看得到室内
constexpr uint32_t kUpperZColor = 0x59ffffff;
// M4-T7：寻路目标标记（SpriteBatch 色，格式同 kHoverColor）
constexpr uint32_t kMarkerActiveColor = 0xff40e0ff;  // 黄：寻路中/跟随中（常显）
constexpr uint32_t kMarkerArriveColor = 0xff50ff60;  // 绿：到达（限时）
constexpr uint32_t kMarkerFailColor = 0xff3030ff;    // 红：不可达（限时）
// M4-T7：HUD 提示文本色（bgfx debug text 属性字节）。
// 注意：bgfx 内建 debug 字体只支持 ASCII/Latin-1 —— HUD 提示文案一律用英文（中文只进日志，
// 否则渲染成乱码；M5-T1 冒烟发现后统一整改）。
constexpr uint32_t kNoticeInfoColor = 0x0E;  // 黄
constexpr uint32_t kNoticeOkColor = 0x0A;    // 绿
constexpr uint32_t kNoticeFailColor = 0x0C;  // 红

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

    // 相机：对准 village_demo 北侧村庄（房群在 chunk (6..7, 1..2)）；边界=地图（M4-T5）
    render::Camera& camera = *m_ctx.camera;
    camera.SetViewport(m_ctx.renderer->FramebufferWidth(), m_ctx.renderer->FramebufferHeight());
    camera.SetCenter(render::ToScreen(70.0f, 18.0f));
    m_followCamera = config.Get<bool>("camera.follow", true);
    m_followRate = static_cast<float>(config.Get<double>("camera.followRate", 8.0));

    // 自动巡航压测（M3-T7；F8 手动切换）
    m_cruiseEnabled = config.Get<bool>("world.autoCruise", false);
    m_cruiseSpeed = static_cast<float>(config.Get<double>("world.cruiseSpeed", 30.0));
    m_cruiseRadius = static_cast<float>(config.Get<double>("world.cruiseRadius", 40.0));
    m_cruiseCenterX = static_cast<float>(config.Get<double>("world.cruiseCenterX", 500.0));
    m_cruiseCenterY = static_cast<float>(config.Get<double>("world.cruiseCenterY", 500.0));
    m_cruiseMinutes = config.Get<double>("world.cruiseMinutes", 5.0);
    // M5-T4：演示枪声基础可闻半径（隔墙衰减由感知系统的传播模型按听者计算）
    m_gunshotRadius = static_cast<float>(config.Get<double>("sounds.gunshotRadius", 30.0));
    // M5-T6：玩家挥击参数（伤害管线入口，CombatSystem 消费 AttackIntent）
    m_playerAttackDamage = static_cast<float>(config.Get<double>("combat.playerDamage", 40.0));
    m_playerAttackRange = static_cast<float>(config.Get<double>("combat.playerRange", 1.7));
    m_playerAttackKnockback =
        static_cast<float>(config.Get<double>("combat.playerKnockback", 3.0));
    m_playerAttackStagger =
        static_cast<float>(config.Get<double>("combat.playerStaggerSeconds", 0.35));
    if (m_cruiseEnabled) {
        camera.SetCenter(render::ToScreen(m_cruiseCenterX + m_cruiseRadius, m_cruiseCenterY));
        ZL_INFO("perf", "自动巡航开启：圆心=({:.0f},{:.0f}) 半径={:.0f} 速度={:.0f}格/s 时长={:.1f}min",
                m_cruiseCenterX, m_cruiseCenterY, m_cruiseRadius, m_cruiseSpeed, m_cruiseMinutes);
    }
    ApplyCameraMode();

    // M4-T2/T3：ECS 演示实体（村庄附近漂移；物理体由 Bullet 驱动）
    SpawnDemoEntities();
    // M4-T4：玩家实体由 OnFrame 延迟生成（等目标 chunk 加载完成）

    ZL_INFO("render", "按键：WASD 移动玩家 | 左键 点击移动 | SPACE 攻击 | F6 演示击杀 | F7 开枪（声音引怪） | F4 自由相机 | 方向键/右键拖拽 平移相机 | 滚轮 缩放 | F8 自动巡航 | F9 网格线 | F10 物理线框 | F1 暂停 | F2 倍速 | F5 热重载 | ESC 退出");
}

void WorldDemoState::ApplyCameraMode() {
    render::Camera& camera = *m_ctx.camera;
    const bool cruiseActive = m_cruiseEnabled && !m_cruiseFinished;
    if (m_followCamera && !cruiseActive) {
        // 跟随模式：相机中心夹取到地图格范围的投影 AABB 内（边缘不移出地图）
        const world::MapLoader::Meta& meta = m_ctx.chunkManager->Map();
        const float gx0 = static_cast<float>(meta.chunkX0 * core::kChunkSize);
        const float gy0 = static_cast<float>(meta.chunkY0 * core::kChunkSize);
        const float gx1 = static_cast<float>((meta.chunkX0 + meta.chunksX) * core::kChunkSize);
        const float gy1 = static_cast<float>((meta.chunkY0 + meta.chunksY) * core::kChunkSize);
        glm::vec2 minPx;
        glm::vec2 maxPx;
        MapPixelBounds(gx0, gy0, gx1, gy1, &minPx, &maxPx);
        camera.SetBounds(minPx, maxPx);
        m_followSnapped = false;  // 下次跟随直接落位（不做缓动）
    } else {
        camera.ClearBounds();  // 自由相机/巡航：无边界（开发漫游/压测）
    }
}

void WorldDemoState::UpdateCameraFollow(double frameDt) {
    if (!m_followCamera || (m_cruiseEnabled && !m_cruiseFinished)) {
        return;  // 自由模式或巡航接管
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    const auto* pos = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (pos == nullptr) {
        return;
    }
    render::Camera& camera = *m_ctx.camera;
    const glm::vec2 target = render::ToScreen(pos->x, pos->y, 0);
    if (!m_followSnapped) {
        camera.SetCenter(target);  // 首次/切回：直接落位，避免长距离缓动
        m_followSnapped = true;
        ZL_INFO("render", "相机跟随就位：玩家格 ({:.1f}, {:.1f})", pos->x, pos->y);
        return;
    }
    camera.SetCenter(CameraFollowStep(camera.Center(), target, m_followRate,
                                      static_cast<float>(frameDt)));
}

ecs::Entity WorldDemoState::SpawnCharacter(float px, float py) {
    ecs::EcsWorld& world = *m_ctx.ecs;
    entt::registry& registry = world.Registry();
    const ecs::Entity e = world.Create();
    registry.emplace<ecs::Position>(e, ecs::Position{px, py, 0});
    registry.emplace<ecs::Velocity>(e, ecs::Velocity{});
    registry.emplace<ecs::Collider>(e, ecs::Collider{});
    registry.emplace<ecs::SpriteRef>(
        e, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0, 0xFFFFFFFFu});
    registry.emplace<ecs::Facing>(e, ecs::Facing{});
    registry.emplace<ecs::AnimState>(e, ecs::AnimState{});
    registry.emplace<ecs::Health>(e, ecs::Health{});  // M5-T1：死亡流程（HP<=0 → 尸体）
    registry.emplace<ecs::Needs>(e, ecs::Needs{});

    // M4-T3：物理胶囊（位置/速度由 Bullet 接管）；M4-T4 动画读物理回写速度
    if (m_ctx.physics != nullptr) {
        const ecs::Collider def;
        ecs::RigidBody rigid;
        rigid.body = m_ctx.physics->CreateCharacterBody(def.radius, def.height, px, py, 0);
        registry.emplace<ecs::RigidBody>(e, rigid);
    }
    return e;
}

bool WorldDemoState::IsSolidSquare(int wx, int wy) const {
    return IsSquareBlocked(*m_ctx.world, wx, wy, 0);
}

void WorldDemoState::EnsurePlayerSpawned() {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    // M5-T1：玩家死亡（实体已回收）→ 清引用重新生成（尸体留在原地）
    if (m_player != entt::null && !m_ctx.ecs->Alive(m_player)) {
        ZL_INFO("game", "玩家实体已死亡，重新生成（旧实体 ID {}）",
                m_ctx.ecs->IdOf(m_player).id);
        m_player = entt::null;
        m_lastPlayerPathState = ecs::PathStatus::None;
        m_markerPathActive = false;
    }
    if (m_player != entt::null) {
        return;
    }
    // 目标点 = 默认相机中心（村庄北）。必须先等目标 chunk 加载完成：
    // 否则"未加载=阻挡"会让螺旋扫到远处已加载的外围（M4-T4 冒烟踩坑）
    constexpr int kTargetX = 70;
    constexpr int kTargetY = 18;
    const world::ChunkCoord targetChunk = world::ChunkOf(kTargetX, kTargetY);
    const world::Chunk* target = FindChunk(targetChunk.cx, targetChunk.cy);
    if (target == nullptr || target->State() != world::ChunkState::Loaded) {
        return;  // 下帧再试
    }
    // 螺旋外扩找可走格（M4-T7：改用 ai::FindNearestWalkable 统一环序）
    int spawnX = 0;
    int spawnY = 0;
    const bool found = ai::FindNearestWalkable(
        kTargetX, kTargetY, 24, [this](int wx, int wy) { return !IsSolidSquare(wx, wy); },
        &spawnX, &spawnY);
    if (!found) {
        return;  // 全扫不到（理论不出现）：下帧再试
    }
    m_player = SpawnCharacter(static_cast<float>(spawnX) + 0.5f,
                              static_cast<float>(spawnY) + 0.5f);
    entt::registry& registry = m_ctx.ecs->Registry();
    registry.emplace<ecs::PlayerTag>(m_player);
    registry.emplace<ecs::LocalControl>(m_player, ecs::LocalControl{});
    registry.emplace<ecs::MoveIntent>(m_player, ecs::MoveIntent{});
    registry.emplace<ecs::PathRequest>(m_player, ecs::PathRequest{});  // M4-T7：点击移动
    registry.emplace<ecs::PathFollow>(m_player, ecs::PathFollow{});
    registry.emplace<ecs::AttackIntent>(m_player, ecs::AttackIntent{});  // M5-T6：空格挥击
    const int radius = std::max(std::abs(spawnX - kTargetX), std::abs(spawnY - kTargetY));
    ZL_INFO("game", "玩家实体生成：格 ({}, {})（目标 ({}, {})，搜索半径 {}）", spawnX, spawnY,
            kTargetX, kTargetY, radius);
}

void WorldDemoState::SetNotice(std::string text, uint32_t color, float seconds) {
    m_noticeText = std::move(text);
    m_noticeColor = color;
    m_noticeTimer = seconds;
}

void WorldDemoState::HandleClickToMove(const render::GridPick& pick) {
    if (m_ctx.ecs == nullptr || m_player == entt::null || !pick.hit) {
        return;  // 玩家未生成 / 拾取无效
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* request = registry.try_get<ecs::PathRequest>(m_player);
    if (request == nullptr) {
        ZL_WARN("game", "点击移动：玩家缺少 PathRequest 组件（内部错误）");
        return;
    }

    int targetX = pick.x;
    int targetY = pick.y;

    // 1) 地图范围校验（chunk 边界闭区间）
    const world::MapLoader::Meta& meta = m_ctx.chunkManager->Map();
    const world::ChunkCoord targetChunk = world::ChunkOf(targetX, targetY);
    const bool insideMap = meta.chunksX > 0 && meta.chunksY > 0 &&
                           targetChunk.cx >= meta.chunkX0 &&
                           targetChunk.cx < meta.chunkX0 + meta.chunksX &&
                           targetChunk.cy >= meta.chunkY0 &&
                           targetChunk.cy < meta.chunkY0 + meta.chunksY;
    if (!insideMap) {
        SetNotice("out of map bounds", kNoticeFailColor, 2.5f);
        ZL_INFO("game", "点击移动：目标格 ({}, {}) 超出地图（忽略）", targetX, targetY);
        return;
    }

    // 2) chunk 已加载校验（未加载 = 通行未知，不发起）
    const world::Chunk* chunk = FindChunk(targetChunk.cx, targetChunk.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        SetNotice("target chunk not loaded", kNoticeFailColor, 2.5f);
        ZL_INFO("game", "点击移动：目标格 ({}, {}) 所在 chunk 未加载（忽略）", targetX, targetY);
        return;
    }

    // 3) 目标格校验：阻挡 → 就近可走格（Chebyshev 环 ≤4；仍找不到 = 不可达，直接反馈）
    bool adjusted = false;
    if (IsSolidSquare(targetX, targetY)) {
        int nearX = 0;
        int nearY = 0;
        const int clickedX = targetX;
        const int clickedY = targetY;
        const bool found = ai::FindNearestWalkable(
            targetX, targetY, 4,
            [this](int wx, int wy) { return !IsSolidSquare(wx, wy); }, &nearX, &nearY);
        if (!found) {
            SetNotice("unreachable: target (" + std::to_string(targetX) + "," +
                          std::to_string(targetY) + ")",
                      kNoticeFailColor, 3.0f);
            m_markerX = targetX;
            m_markerY = targetY;
            m_markerTimer = 3.0f;
            m_markerColor = kMarkerFailColor;
            ZL_INFO("game", "点击移动：目标格 ({}, {}) 附近无可行格（不可达反馈）", targetX,
                    targetY);
            return;
        }
        adjusted = true;
        targetX = nearX;
        targetY = nearY;
        ZL_INFO("game", "点击移动：目标格 ({}, {}) 阻挡 → 就近可走格 ({}, {})", clickedX,
                clickedY, targetX, targetY);
    }

    // 4) 写 PathRequest（PathFollowSystem 下一 tick 提交后台查询）
    request->goalX = targetX;
    request->goalY = targetY;
    request->newRequest = true;
    SetNotice(adjusted ? "adjusted target; pathfinding..." : "pathfinding...", kNoticeInfoColor,
              1.2f);
    ZL_INFO("game", "点击移动：玩家 → 目标格 ({}, {})", targetX, targetY);
}

void WorldDemoState::UpdatePathFeedback(double frameDt) {
    if (m_noticeTimer > 0.0f) {
        m_noticeTimer = std::max(0.0f, m_noticeTimer - static_cast<float>(frameDt));
    }
    if (m_markerTimer > 0.0f) {
        m_markerTimer = std::max(0.0f, m_markerTimer - static_cast<float>(frameDt));
    }
    m_markerPathActive = false;
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    const auto* request = m_ctx.ecs->Registry().try_get<ecs::PathRequest>(m_player);
    if (request == nullptr) {
        return;
    }
    const ecs::PathStatus state = request->state;
    if (state != m_lastPlayerPathState) {
        if (state == ecs::PathStatus::Arrived) {
            SetNotice("arrived", kNoticeOkColor, 1.5f);
            m_markerX = request->goalX;
            m_markerY = request->goalY;
            m_markerTimer = 1.5f;
            m_markerColor = kMarkerArriveColor;
        } else if (state == ecs::PathStatus::Failed) {
            SetNotice("unreachable: target (" + std::to_string(request->goalX) + "," +
                          std::to_string(request->goalY) + ")",
                      kNoticeFailColor, 3.0f);
            m_markerX = request->goalX;
            m_markerY = request->goalY;
            m_markerTimer = 3.0f;
            m_markerColor = kMarkerFailColor;
        }
        m_lastPlayerPathState = state;
    }
    if (state == ecs::PathStatus::Pending || state == ecs::PathStatus::Following) {
        m_markerX = request->goalX;  // 寻路中/跟随中：黄色目标标记常显
        m_markerY = request->goalY;
        m_markerPathActive = true;
        m_markerColor = kMarkerActiveColor;
    }
}

void WorldDemoState::SpawnDemoEntities() {
    if (m_ctx.ecs == nullptr) {
        return;  // 无 ECS 上下文（理论不出现）：纯世界渲染降级
    }
    ecs::EcsWorld& world = *m_ctx.ecs;
    std::mt19937 rng(20261016u);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    // 游荡区域对准默认相机视野（村北，world (70,18) 附近）
    m_demoBoxX0 = 44.0f;
    m_demoBoxY0 = 4.0f;
    m_demoBoxX1 = 96.0f;
    m_demoBoxY1 = 32.0f;

    constexpr int kCount = 40;
    for (int i = 0; i < kCount; ++i) {
        const float px = m_demoBoxX0 + u01(rng) * (m_demoBoxX1 - m_demoBoxX0);
        const float py = m_demoBoxY0 + u01(rng) * (m_demoBoxY1 - m_demoBoxY0);
        SpawnZombie(world, m_ctx.physics, px, py);  // M5-T7：僵尸工厂（与实体化共用构成）
    }
    ZL_INFO("ecs", "生成 {} 个 AI 游荡者（AiSystem + PerceptionSystem；区域 x[{:.0f},{:.0f}] y[{:.0f},{:.0f}]）",
            kCount, m_demoBoxX0, m_demoBoxX1, m_demoBoxY0, m_demoBoxY1);

    // M5-T5/T7：虚拟僵尸群落种子（统计层，不入 registry）—— 演示的 200 只"离屏僵尸"；
    // SpawnLodSystem 会把 populationTarget 内、焦点半径外的实体折回这里（AddOrMerge），
    // 焦点接近时再实体化 —— live + virtual 与 populationTarget 对齐（人口守恒）
    if (m_ctx.virtualHordes != nullptr) {
        m_ctx.virtualHordes->Clear();
        constexpr int kHordes = 5;
        constexpr int kPerHorde = 40;  // 合计 200
        for (int i = 0; i < kHordes; ++i) {
            const float angle = static_cast<float>(i) * (6.2831853f / kHordes);
            const float radius = 55.0f + static_cast<float>(i) * 5.0f;
            m_ctx.virtualHordes->AddHorde(70.0f + std::cos(angle) * radius,
                                          18.0f + std::sin(angle) * radius, kPerHorde,
                                          /*spread=*/8.0f);
        }
        const auto stats = m_ctx.virtualHordes->GetStats(70.0f, 18.0f);
        ZL_INFO("ai", "虚拟群落种子：{} 群 / {} 只（统计层，不入 registry；最近 {:.0f} 格）",
                stats.hordes, stats.zombies, stats.nearestDist);
    }
}

// M5-T2 临时"感知"桩已删除：M5-T3 起由 ai::PerceptionSystem（视锥 + 视线 + 听觉）接管。

void WorldDemoState::KillNearestDemoEntity() {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();

    // 参考点：玩家（未生成时用相机中心）
    glm::vec2 ref = render::FromScreen(m_ctx.camera->Center(), 0);
    if (m_player != entt::null) {
        if (const auto* playerPos = registry.try_get<ecs::Position>(m_player)) {
            ref = {playerPos->x, playerPos->y};
        }
    }

    ecs::Entity best = entt::null;
    float bestDistSq = 0.0f;
    registry.view<ecs::AiState, ecs::Health, ecs::Position>().each(
        [&](const ecs::Entity e, const ecs::AiState&, const ecs::Health& health,
            const ecs::Position& pos) {
            if (!m_ctx.ecs->Alive(e) || health.current <= 0.0f) {
                return;  // continue
            }
            const float dx = pos.x - ref.x;
            const float dy = pos.y - ref.y;
            const float distSq = dx * dx + dy * dy;
            if (best == entt::null || distSq < bestDistSq) {
                best = e;
                bestDistSq = distSq;
            }
        });
    if (best == entt::null) {
        SetNotice("demo kill: no live target nearby", kNoticeFailColor, 2.0f);
        return;
    }
    // M5-T6：改走统一伤害管线（硬直/击退/闪红 + 致死交给 DeathSystem 的倒地窗口）
    const auto* pos = registry.try_get<ecs::Position>(best);
    const std::uint64_t id = m_ctx.ecs->IdOf(best).id;
    if (m_ctx.combat != nullptr) {
        ecs::DamageEvent event;
        event.sourceId = m_player != entt::null ? m_ctx.ecs->IdOf(m_player).id : 0;
        event.type = ecs::DamageType::Debug;
        event.amount = 999.0f;
        float dx = pos != nullptr ? pos->x - ref.x : 1.0f;
        float dy = pos != nullptr ? pos->y - ref.y : 1.0f;
        const float len = std::sqrt(dx * dx + dy * dy);
        event.dirX = len > 0.0001f ? dx / len : 0.7071f;
        event.dirY = len > 0.0001f ? dy / len : 0.7071f;
        event.knockback = 2.5f;
        event.staggerSeconds = 0.35f;
        m_ctx.combat->ApplyDamage(*m_ctx.ecs, best, event);
    } else {
        registry.get<ecs::Health>(best).current = 0.0f;  // 无战斗系统（降级）：直接归零
    }
    SetNotice("demo kill: id " + std::to_string(id) + " -> corpse", kNoticeInfoColor, 2.5f);
    ZL_INFO("characters", "演示击杀：稳定 ID {}（格 ({:.1f}, {:.1f})）伤害管线致死（F6）", id,
            pos != nullptr ? pos->x : 0.0f, pos != nullptr ? pos->y : 0.0f);
}

void WorldDemoState::EmitGunshot() {
    if (m_ctx.ecs == nullptr || m_ctx.events == nullptr || m_player == entt::null) {
        SetNotice("gunshot: player not spawned", kNoticeFailColor, 2.0f);
        return;
    }
    const auto* playerPos = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (playerPos == nullptr) {
        return;
    }
    core::SoundEvent shot;
    shot.x = playerPos->x;
    shot.y = playerPos->y;
    shot.radius = m_gunshotRadius;
    shot.type = core::SoundType::Gunshot;
    // M5-T4：发射方只发事件（不感知感知系统）；tick 末 FlushQueued 投递 → PushSound → 下 tick 感知消费
    m_ctx.events->EmitQueued(core::kOnSound, shot);
    SetNotice("gunshot! r=" + std::to_string(static_cast<int>(m_gunshotRadius)) +
                  " (walls attenuate)",
              kNoticeInfoColor, 2.0f);
    ZL_INFO("ai", "枪声（F7）：声源 ({:.1f}, {:.1f}) 基础半径 {:.0f} 格（隔墙逐格衰减）", shot.x,
            shot.y, shot.radius);
}

void WorldDemoState::PlayerAttack() {
    if (m_ctx.ecs == nullptr || m_ctx.combat == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* attack = registry.try_get<ecs::AttackIntent>(m_player);
    const auto* playerPos = registry.try_get<ecs::Position>(m_player);
    if (attack == nullptr || playerPos == nullptr) {
        return;
    }

    // 自动瞄准：最近的可受伤僵尸（≤3 格）。演示用（M5-T6 无武器/鼠标朝向；M6 接装备后重做）
    constexpr float kAutoAimRange = 3.0f;
    ecs::Entity best = entt::null;
    float bestDistSq = kAutoAimRange * kAutoAimRange;
    registry.view<ecs::AiState, ecs::Health, ecs::Position>().each(
        [&](const ecs::Entity e, const ecs::AiState&, const ecs::Health& health,
            const ecs::Position& pos) {
            if (!m_ctx.ecs->Alive(e) || health.current <= 0.0f) {
                return;  // continue
            }
            const float dx = pos.x - playerPos->x;
            const float dy = pos.y - playerPos->y;
            const float distSq = dx * dx + dy * dy;
            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                best = e;
            }
        });
    if (best == entt::null) {
        SetNotice("attack: no target within 3.0", kNoticeFailColor, 1.5f);
        return;
    }

    const auto* targetPos = registry.try_get<ecs::Position>(best);
    float dx = targetPos->x - playerPos->x;
    float dy = targetPos->y - playerPos->y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0001f) {
        dx /= len;
        dy /= len;
    } else {
        dx = 1.0f;  // 完全重叠：给个确定方向
        dy = 0.0f;
    }
    attack->targetId = m_ctx.ecs->IdOf(best).id;
    attack->dirX = dx;
    attack->dirY = dy;
    attack->damage = m_playerAttackDamage;
    attack->range = m_playerAttackRange;
    attack->knockback = m_playerAttackKnockback;
    attack->staggerSeconds = m_playerAttackStagger;
    attack->arcDegrees = 100.0f;
    attack->type = ecs::DamageType::Blunt;
    attack->pending = true;  // 下一 tick CombatSystem 消费（扇形判定 → 伤害管线）
    if (auto* facing = registry.try_get<ecs::Facing>(m_player); facing != nullptr) {
        facing->dir = characters::AnimationSystem::FacingFromVelocity(dx, dy);  // 挥击朝向反馈
    }
    SetNotice("attack: swing at id " + std::to_string(attack->targetId), kNoticeInfoColor, 1.2f);
    ZL_INFO("characters", "玩家挥击（SPACE）：目标稳定 ID {}（中心距 {:.2f} 格，伤害 {:.0f}）",
            attack->targetId, static_cast<double>(std::sqrt(bestDistSq)), m_playerAttackDamage);
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
    m_player = entt::null;
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
    // 感知/AI/位移全部由固定链驱动（Perception → Ai → PathFollow → Movement → PhysicsSync）。
}

void WorldDemoState::OnFrame(double frameDt, double alpha) {
    (void)alpha;
    HandleFrameInput(frameDt);
    UpdatePathFeedback(frameDt);  // M4-T7：点击移动反馈（提示文字/目标标记计时与状态跃迁）
    UpdateCruise(frameDt);
    UpdateCameraFollow(frameDt);  // M4-T5：跟随在巡航之后（巡航接管时不生效）

    // 流式加载：相机中心（世界像素 → 世界格坐标）
    const glm::vec2 camGrid = render::FromScreen(m_ctx.camera->Center(), 0);
    m_ctx.chunkManager->Update(camGrid.x, camGrid.y);
    m_ctx.chunkManager->ProcessResults();

    // M4-T4：目标 chunk 加载完成后生成玩家（村庄中心附近找可走格）
    EnsurePlayerSpawned();

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
        ai::PathfindingService::Stats pathStats{};
        if (m_ctx.pathfinding != nullptr) {
            pathStats = m_ctx.pathfinding->GetStats();
        }
        ZL_INFO("render",
                "世界统计: FPS={:.0f} | 实例={} | 四边形={} | 批次={} | drawcall={} | zoom={:.2f} | "
                "chunk 驻留={} 加载中={} | 相机格=({:.0f},{:.0f}) | 累计加载={} 卸载={} 取消={} 失败={} | "
                "ECS={} 索引={} | 物理: 静态={} 动态={} step={:.2f}ms | 寻路: 查询={} 完成={} 平均={:.2f}ms",
                stats.fps, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
                m_ctx.sprites->DrawCallsThisFrame(), stats.drawCalls, m_ctx.camera->Zoom(),
                chunkStats.loadedChunks, chunkStats.loadingChunks, camGrid.x, camGrid.y,
                chunkStats.totalLoaded, chunkStats.totalUnloaded, chunkStats.totalCancelled,
                chunkStats.totalFailed, m_ctx.ecs != nullptr ? m_ctx.ecs->AliveCount() : 0u,
                m_ctx.spatialIndex != nullptr ? m_ctx.spatialIndex->EntityCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->StaticBodyCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->DynamicBodyCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->AvgStepMs() : 0.0,
                static_cast<unsigned long long>(pathStats.submitted),
                static_cast<unsigned long long>(pathStats.completed), pathStats.avgMs);
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
        ApplyCameraMode();  // 巡航结束：回跟随模式恢复地图边界
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
    if (input.IsActionPressed("DemoKill")) {
        KillNearestDemoEntity();  // M5-T1：演示击杀（HP 归零 → 尸体）
    }
    if (input.IsActionPressed("Attack")) {
        PlayerAttack();  // M5-T6：空格挥击（自动瞄准最近僵尸 → 伤害管线）
    }
    if (input.IsActionPressed("DemoGunshot")) {
        EmitGunshot();  // M5-T4：演示开枪（大半径 SoundEvent → 感知 → 僵尸来查看）
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
    if (input.IsActionPressed("ToggleFreeCam")) {
        m_followCamera = !m_followCamera;
        ApplyCameraMode();
        ZL_INFO("render", "F4 相机模式: {}（{}）", m_followCamera ? "跟随玩家" : "自由",
                m_followCamera ? "边界=地图 AABB" : "无边界，方向键/拖拽平移");
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
        ApplyCameraMode();  // 巡航=无边界；结束回跟随=恢复地图边界
        ZL_INFO("perf", "F8 自动巡航: {}（{:.1f} 分钟，速度 {:.0f} 格/s，半径 {:.0f}）",
                m_cruiseEnabled ? "开" : "关", m_cruiseMinutes, m_cruiseSpeed, m_cruiseRadius);
    }

    // 平移：方向键（仅自由相机模式；M4-T5 跟随模式下由 UpdateCameraFollow 接管）
    glm::vec2 move(0.0f, 0.0f);
    if (!m_followCamera) {
        if (input.IsActionDown("PanUp")) {
            move.y -= 1.0f;
        }
        if (input.IsActionDown("PanDown")) {
            move.y += 1.0f;
        }
        if (input.IsActionDown("PanLeft")) {
            move.x -= 1.0f;
        }
        if (input.IsActionDown("PanRight")) {
            move.x += 1.0f;
        }
    }
    if (move.x != 0.0f || move.y != 0.0f) {
        camera.PanBy(glm::normalize(move) * (kPanSpeedPxPerSecond * static_cast<float>(frameDt) /
                                             camera.Zoom()));
    }

    // 拖拽平移（右键/中键；自由相机模式）
    const glm::vec2 mouse(input.MouseX(), input.MouseY());
    const bool dragging = !m_followCamera && (input.IsMouseButtonDown(1) || input.IsMouseButtonDown(2));
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

    // M4-T7：左键点击移动（拾取 → 校验 → PathRequest；巡航期间不接管）
    if (input.IsMouseButtonPressed(0) && !(m_cruiseEnabled && !m_cruiseFinished)) {
        HandleClickToMove(pick);
    }
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
    return FindChunkInWorld(*m_ctx.world, chunkX, chunkY);
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
    // M4-T7：寻路目标标记（黄=进行中常显；绿/红=到达/不可达 限时）
    if (m_markerPathActive || m_markerTimer > 0.0f) {
        const glm::vec2 center =
            render::ToScreen(static_cast<float>(m_markerX), static_cast<float>(m_markerY));
        const glm::vec2 top = center + glm::vec2(0.0f, -22.0f);
        const glm::vec2 right = center + glm::vec2(44.0f, 0.0f);
        const glm::vec2 bottom = center + glm::vec2(0.0f, 22.0f);
        const glm::vec2 left = center + glm::vec2(-44.0f, 0.0f);
        batch.DrawLine(top, right, kMarkerLineThickness, m_markerColor);
        batch.DrawLine(right, bottom, kMarkerLineThickness, m_markerColor);
        batch.DrawLine(bottom, left, kMarkerLineThickness, m_markerColor);
        batch.DrawLine(left, top, kMarkerLineThickness, m_markerColor);
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
    bgfx::dbgTextPrintf(0, 6, 0x0f, "zoom: %.2f | cam: %s | hover: (%d, %d) | provider: %s",
                        m_ctx.camera->Zoom(),
                        m_followCamera ? (m_cruiseEnabled && !m_cruiseFinished ? "cruise" : "follow")
                                       : "free",
                        m_hoverX, m_hoverY, m_ctx.assets->Provider().c_str());
    // M4-T4：玩家状态（位置/朝向/动画帧/移动意图）
    if (m_ctx.ecs != nullptr && m_player != entt::null) {
        entt::registry& registry = m_ctx.ecs->Registry();
        const auto* playerPos = registry.try_get<ecs::Position>(m_player);
        const auto* playerFacing = registry.try_get<ecs::Facing>(m_player);
        const auto* playerAnim = registry.try_get<ecs::AnimState>(m_player);
        const auto* playerIntent = registry.try_get<ecs::MoveIntent>(m_player);
        if (playerPos != nullptr) {
            const auto* playerHealth = registry.try_get<ecs::Health>(m_player);
            bgfx::dbgTextPrintf(
                0, 7, 0x0f, "player: (%.1f, %.1f) dir=%u frame=%u intent=(%.1f, %.1f) hp=%.0f",
                playerPos->x, playerPos->y,
                playerFacing != nullptr ? static_cast<unsigned>(playerFacing->dir) : 0u,
                playerAnim != nullptr ? static_cast<unsigned>(playerAnim->frame) : 0u,
                playerIntent != nullptr ? playerIntent->x : 0.0f,
                playerIntent != nullptr ? playerIntent->y : 0.0f,
                playerHealth != nullptr ? playerHealth->current : 0.0f);
        }
    }
    // M4-T6：寻路统计（提交/完成/失败分解 + 后台耗时 + 位图缓存）
    if (m_ctx.pathfinding != nullptr && m_ctx.ecs != nullptr) {
        const ai::PathfindingService::Stats pathStats = m_ctx.pathfinding->GetStats();
        int following = 0;
        m_ctx.ecs->Registry().view<ecs::PathFollow>().each(
            [&](const ecs::Entity, const ecs::PathFollow& follow) {
                if (follow.status == ecs::PathStatus::Following) {
                    ++following;
                }
            });
        bgfx::dbgTextPrintf(
            0, 8, 0x0f,
            "path: query=%llu found=%llu unreach=%llu invalid=%llu budget=%llu | pending=%d avg=%.2fms | cached=%zu | following=%d",
            static_cast<unsigned long long>(pathStats.submitted),
            static_cast<unsigned long long>(pathStats.found),
            static_cast<unsigned long long>(pathStats.unreachable),
            static_cast<unsigned long long>(pathStats.invalid),
            static_cast<unsigned long long>(pathStats.budget), pathStats.pending,
            pathStats.avgMs, m_ctx.pathfinding->CachedChunks(), following);
    }
    // M5-T2/T3：AI 状态分布 + 感知统计（每 tick 统计；swings = 攻击脉冲累计）
    if (m_ctx.aiSystem != nullptr) {
        const ai::AiSystem::Stats aiStats = m_ctx.aiSystem->GetStats();
        const ai::PerceptionSystem::Stats percStats =
            m_ctx.perception != nullptr ? m_ctx.perception->GetStats()
                                        : ai::PerceptionSystem::Stats{};
        bgfx::dbgTextPrintf(
            0, 10, 0x0f,
            "ai: idle=%d wander=%d chase=%d search=%d attack=%d dead=%d | swings=%llu | "
            "seeing=%d alert=%d heard=%d",
            aiStats.idle, aiStats.wander, aiStats.chase, aiStats.search, aiStats.attack,
            aiStats.dead, static_cast<unsigned long long>(aiStats.swings), percStats.seeing,
            percStats.alerted, percStats.heard);
    }
    bgfx::dbgTextPrintf(0, 9, 0x07,
                        "WASD/LMB move | SPACE attack | F6 demo-kill | F7 gunshot | F4 cam | arrows/RMB pan | scroll zoom | F9 grid | F1 pause | F2 speed | F5 reload | ESC quit");
    // M5-T5：LOD 档位 + 虚拟群落统计（虚拟化 = 统计层，不入 registry）
    if (m_ctx.lod != nullptr) {
        const ai::LodSystem::Stats lodStats = m_ctx.lod->GetStats();
        ai::VirtualHordeStore::Stats vStats{};
        if (m_ctx.virtualHordes != nullptr) {
            vStats = m_ctx.virtualHordes->GetStats(camGrid.x, camGrid.y);
        }
        bgfx::dbgTextPrintf(0, 11, 0x0f,
                            "lod: full=%d (60Hz) mid=%d (30Hz) far=%d (10Hz) | virtual: hordes=%d zombies=%d nearest=%.0f",
                            lodStats.fullCount, lodStats.midCount, lodStats.farCount,
                            vStats.hordes, vStats.zombies, vStats.nearestDist);
    }
    // M4-T7：点击移动反馈（限时提示：寻路中/已到达/不可达）
    if (m_noticeTimer > 0.0f && !m_noticeText.empty()) {
        bgfx::dbgTextPrintf(0, 12, static_cast<std::uint8_t>(m_noticeColor), "%s",
                            m_noticeText.c_str());
    }
    // M5-T6：战斗统计（挥击/命中/伤害/击杀累计 + 本帧硬直数）
    if (m_ctx.combat != nullptr) {
        const characters::CombatSystem::Stats combatStats = m_ctx.combat->GetStats();
        bgfx::dbgTextPrintf(
            0, 13, 0x0f, "combat: swings=%llu hits=%llu dmg=%.0f kills=%llu | staggered=%d",
            static_cast<unsigned long long>(combatStats.swings),
            static_cast<unsigned long long>(combatStats.hits), combatStats.damageDealt,
            static_cast<unsigned long long>(combatStats.kills), combatStats.staggered);
    }
    // M5-T7：刷怪/虚拟化统计（live/virtual 存量 + 本局累计 实体化/虚拟化/补给）
    if (m_ctx.spawnLod != nullptr) {
        const ai::SpawnLodSystem::Stats spawnStats = m_ctx.spawnLod->GetStats();
        bgfx::dbgTextPrintf(0, 14, 0x0f,
                            "spawn: live=%d virtual=%d hordes=%d | mat=%llu virt=%llu refill=%llu",
                            spawnStats.live, spawnStats.virtualZombies, spawnStats.hordes,
                            static_cast<unsigned long long>(spawnStats.materialized),
                            static_cast<unsigned long long>(spawnStats.virtualized),
                            static_cast<unsigned long long>(spawnStats.spawned));
    }
}

}  // namespace zl::game
