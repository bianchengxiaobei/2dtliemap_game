#include "game/states/WorldDemoState.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <utility>

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
#include "audio/SoundIds.h"
#include "audio/SoundManager.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/AnimationSystem.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/InputSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/SpatialIndex.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/EventProbe.h"
#include "game/CameraFollow.h"
#include "game/EntityReplication.h"
#include "game/NetworkClient.h"
#include "game/WorldQueries.h"
#include "game/WorldSimulation.h"
#include "game/states/MainMenuState.h"
#include "game/ZombieFactory.h"
#include "game/GroundItems.h"
#include "items/Crafting.h"
#include "items/Repair.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/RecipeRegistry.h"
#include "net/Command.h"
#include "net/CommandPipeline.h"
#include "physics/PhysicsWorld.h"
#include "platform/Window.h"
#include "render/Camera.h"
#include "render/IsoMath.h"
#include "render/Renderer.h"
#include "render/SpriteBatch.h"
#include "save/ChunkDiff.h"
#include "save/SaveManager.h"
#include "ui/UiSystem.h"
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
// M4-T7：提示色调（映射到 RmlUi HUD 的 notice 颜色类）。
// bgfx debug 字体只支持 ASCII；中文提示统一走 RmlUi HUD/背包状态行。
constexpr uint32_t kNoticeInfoColor = 0x0E;  // 黄
constexpr uint32_t kNoticeOkColor = 0x0A;    // 绿
constexpr uint32_t kNoticeFailColor = 0x0C;  // 红
constexpr const char* kHotbarActions[] = {"Hotbar1", "Hotbar2", "Hotbar3", "Hotbar4",
                                          "Hotbar5"};

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
    m_networkMode = m_ctx.networkClient != nullptr;
    ZL_INFO("render", "进入 WorldDemo（{}）",
            m_networkMode ? "M13 网络客户端" : "M3 单机世界");
    m_autoSaveOnExit =
        !m_networkMode && config.Get<bool>("save.autoSaveOnExit", true);
    m_ctx.activeSaveId.clear();
    m_ctx.activeSaveUseBackup = false;
    m_ctx.saveWarning.clear();

    const std::string manifest =
        config.Get<std::string>("assets.manifest", "assets/asset_manifest.json");
    const std::string provider = config.Get<std::string>("assets.provider", "own");
    if (!m_ctx.assets->Init(manifest, provider, *m_ctx.atlases)) {
        ZL_ERROR("render", "资源初始化失败，世界将不可见");
    }

    // 相机：对准 village_demo 北侧村庄（房群在 chunk (6..7, 1..2)）；边界=地图（M4-T5）
    render::Camera& camera = *m_ctx.camera;
    camera.SetViewport(m_ctx.renderer->FramebufferWidth(), m_ctx.renderer->FramebufferHeight());
    camera.SetCenter(render::ToScreen(70.0f, 24.0f));
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
    m_footstepStride =
        std::max(0.2f, static_cast<float>(config.Get<double>("audio.footstepStride", 0.9)));
    m_footstepAnchorValid = false;
    m_footstepDistance = 0.0f;
    // M5-T6：玩家挥击参数（伤害管线入口，CombatSystem 消费 AttackIntent）
    m_playerAttackDamage = static_cast<float>(config.Get<double>("combat.playerDamage", 40.0));
    m_playerAttackRange = static_cast<float>(config.Get<double>("combat.playerRange", 1.7));
    m_playerAttackKnockback =
        static_cast<float>(config.Get<double>("combat.playerKnockback", 3.0));
    m_playerAttackStagger =
        static_cast<float>(config.Get<double>("combat.playerStaggerSeconds", 0.35));
    // M6-T2：玩家背包容量（kg）
    m_playerCapacityKg =
        static_cast<float>(config.Get<double>("inventory.playerCapacityKg", 20.0));
    // M6-T3：地面物品拾取半径（格）+ 每次进入状态重新散落
    m_pickupRadius = static_cast<float>(config.Get<double>("inventory.pickupRadius", 1.6));
    m_inventoryRange =
        static_cast<float>(config.Get<double>("inventory.lootRange", 3.0));
    m_repairKitDefId =
        config.Get<std::string>("inventory.repairKit", "repair_kit");
    m_repairKitCount = std::max(1, config.Get<int>("inventory.repairKitCount", 1));
    m_repairAmount = std::clamp(
        static_cast<float>(config.Get<double>("inventory.repairAmount", 0.5)),
        0.01f, 1.0f);
    m_stationRange = std::clamp(
        static_cast<float>(config.Get<double>("crafting.stationRange", 2.5)),
        0.0f, 10.0f);
    m_stationLabels.clear();
    m_stationLabels.emplace("workbench", "工作台");
    m_stationLabels.emplace("campfire", "篝火");
    const auto& root = config.Raw();
    const auto crafting = root.find("crafting");
    if (crafting != root.end() && crafting->is_object()) {
        const auto labels = crafting->find("stationLabels");
        if (labels != crafting->end() && labels->is_object()) {
            for (const auto& [id, label] : labels->items()) {
                if (id.empty() || !label.is_string() || label.get<std::string>().empty()) {
                    ZL_WARN("crafting", "忽略非法制作站标签：{}", id);
                    continue;
                }
                m_stationLabels[id] = label.get<std::string>();
            }
        }
    }
    m_blueprintRecipes.clear();
    if (m_ctx.items != nullptr && m_ctx.recipes != nullptr) {
        for (const items::ItemDef& def : m_ctx.items->All()) {
            for (const std::string& recipeId : def.unlocksRecipes) {
                if (m_ctx.recipes->Find(recipeId) != nullptr) {
                    m_blueprintRecipes.insert(recipeId);
                }
            }
        }
    }
    m_demoItemsScattered = false;
    if (m_cruiseEnabled) {
        camera.SetCenter(render::ToScreen(m_cruiseCenterX + m_cruiseRadius, m_cruiseCenterY));
        ZL_INFO("perf", "自动巡航开启：圆心=({:.0f},{:.0f}) 半径={:.0f} 速度={:.0f}格/s 时长={:.1f}min",
                m_cruiseCenterX, m_cruiseCenterY, m_cruiseRadius, m_cruiseSpeed, m_cruiseMinutes);
    }
    ApplyCameraMode();

    m_ctx.onSaveFinished =
        [this](bool ok, bool created, const std::string& id,
               const std::string& error) {
            HandleSaveFinished(ok, created, id, error);
        };
    m_ctx.quickSave = [this]() -> bool {
        if (m_networkMode || m_ctx.saveManager == nullptr ||
            m_ctx.saveService == nullptr || m_ctx.saveComponents == nullptr ||
            m_ctx.ecs == nullptr || m_savePending || m_saveRequestPending) {
            return false;
        }
        RequestQuickSave(true);
        return true;
    };
    m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
    m_moveCommandValid = false;
    m_ctx.executePlayerCommand =
        [this](const net::Command& command, std::string& error) {
            return ExecutePlayerCommand(command, error);
        };

    m_loadedFromSave = !m_networkMode && !m_loadSaveId.empty() && LoadFromSave();
    if (!m_loadedFromSave && !m_networkMode) {
        // M4-T2/T3：ECS 演示实体（村庄附近漂移；物理体由 Bullet 驱动）
        SpawnDemoEntities();
    }
    // 单机：玩家实体由 OnFrame 延迟生成（等目标 chunk 加载完成）。
    // 网络：等待服务器分配 StableId + 首个全量快照，再生成纯渲染副本。

    if (m_ctx.ui != nullptr) {
        m_ctx.ui->HideMenu();
        m_ctx.ui->SetInventoryActionHandler(
            [this](const ui::InventoryAction& action) { SubmitInventoryAction(action); });
        m_ctx.ui->SetMenuActionHandler(
            [this](ui::MenuActionType action) { HandleMenuAction(action); });
        if (m_networkMode) {
            m_ctx.ui->SetSocialChatHandler(
                [this](const std::string& text, std::string& error) {
                    SendChat(text, error);
                    return error.empty();
                });
            RefreshSocialState();
        } else {
            m_ctx.ui->SetSocialChatHandler({});
            m_ctx.ui->SetSocialState({});
        }
    }
    ConsumeSaveWarning();

    ZL_INFO("render", "按键：WASD 移动玩家 | 左键 点击移动 | SPACE 攻击 | F 拾取 | E 使用 | R 装备 | G 丢弃 | I 背包（或右下角按钮） | Enter 聊天 | F6 演示击杀 | F7 开枪（声音引怪） | F4 自由相机 | 方向键/右键拖拽 平移相机 | 滚轮 缩放 | F8 自动巡航 | F9 网格线 | F10 物理线框 | F1 暂停 | F2 倍速 | F5 热重载 | F12 快速存档 | ESC 暂停菜单");
}

bool WorldDemoState::LoadFromSave() {
    if (m_ctx.saveManager == nullptr || m_ctx.ecs == nullptr ||
        m_ctx.saveComponents == nullptr) {
        m_ctx.saveWarning = "读取存档失败：存档服务未初始化";
        return false;
    }
    m_crafting.Cancel();
    m_craftBatchTotal = 0;
    m_craftBatchCompleted = 0;
    m_craftQueue.clear();

    save::SaveMetadata metadata;
    save::EntitySnapshot snapshot;
    save::SaveManager::LoadReport report;
    std::string error;
    const save::SaveManager::LoadResult result = m_ctx.saveManager->Load(
        m_loadSaveId, *m_ctx.saveComponents, &snapshot, &metadata, &error, &report);
    if (result != save::SaveManager::LoadResult::Ok) {
        m_ctx.saveWarning = "读取存档失败（" + m_loadSaveId + "）：" + error +
                            "；建议删除存档或回退备份";
        ZL_ERROR("save", "读取存档失败 '{}'：{}", m_loadSaveId, error);
        m_loadSaveId.clear();
        return false;
    }
    const std::string currentMap =
        m_ctx.chunkManager != nullptr ? m_ctx.chunkManager->Map().name : std::string();
    if (!metadata.mapId.empty() && !currentMap.empty() &&
        metadata.mapId != currentMap) {
        m_ctx.saveWarning = "存档地图不匹配：" + metadata.mapId + "（当前 " +
                            currentMap + "）";
        ZL_ERROR("save", "{}", m_ctx.saveWarning);
        m_loadSaveId.clear();
        return false;
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    std::vector<ecs::Entity> oldEntities;
    registry.view<ecs::StableId>().each(
        [&](const ecs::Entity entity, const ecs::StableId&) {
            if (m_ctx.ecs->Alive(entity)) {
                oldEntities.push_back(entity);
            }
        });
    for (const ecs::Entity entity : oldEntities) {
        if (m_ctx.physics != nullptr) {
            if (const auto* rigid = registry.try_get<ecs::RigidBody>(entity);
                rigid != nullptr && rigid->body != nullptr) {
                m_ctx.physics->DestroyBody(rigid->body);
            }
        }
        m_ctx.ecs->Destroy(entity);
    }
    m_ctx.ecs->Flush();
    m_player = entt::null;

    std::vector<ecs::Entity> loaded;
    if (!save::LoadEntities(snapshot, *m_ctx.ecs, *m_ctx.saveComponents, &loaded,
                            &error)) {
        m_ctx.saveWarning = "读取实体失败：" + error;
        ZL_ERROR("save", "{}", m_ctx.saveWarning);
        m_loadSaveId.clear();
        return false;
    }

    RestoreRuntimeComponents(loaded);
    RestoreCraftingJobFromEcs();
    m_saveId = report.usedBackup ? std::string() : m_loadSaveId;
    m_ctx.activeSaveId = m_loadSaveId;
    m_ctx.activeSaveUseBackup = report.usedBackup;
    m_demoItemsScattered = true;
    m_followSnapped = false;
    if (m_ctx.virtualHordes != nullptr) {
        m_ctx.virtualHordes->Clear();
    }
    if (report.usedBackup) {
        m_ctx.saveWarning = report.warning + "；本次会话保存将新建存档";
    }
    ZL_INFO("save", "已读取存档 '{}'：实体 {}，玩家 {}，缩略图 {}", m_saveId,
            loaded.size(), m_player != entt::null ? 1 : 0,
            metadata.thumbnail.empty() ? "无" : metadata.thumbnail);
    return true;
}

void WorldDemoState::RestoreRuntimeComponents(
    const std::vector<ecs::Entity>& loaded) {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    int players = 0;
    int characters = 0;
    for (const ecs::Entity entity : loaded) {
        if (!m_ctx.ecs->Alive(entity)) {
            continue;
        }
        const auto* position = registry.try_get<ecs::Position>(entity);
        if (position == nullptr) {
            continue;
        }
        const bool isPlayer = registry.all_of<ecs::PlayerTag>(entity);
        const bool isGroundItem = registry.all_of<ecs::ItemInstance>(entity);
        const bool isCorpse = registry.all_of<ecs::Corpse>(entity);
        const bool isCharacter =
            isPlayer || (registry.all_of<ecs::Health>(entity) &&
                         !isGroundItem && !isCorpse);
        if (!isCharacter) {
            continue;
        }

        if (!registry.all_of<ecs::Collider>(entity)) {
            registry.emplace<ecs::Collider>(entity);
        }
        if (m_ctx.physics != nullptr &&
            !registry.all_of<ecs::RigidBody>(entity)) {
            const auto* collider = registry.try_get<ecs::Collider>(entity);
            const ecs::Collider defaults;
            const float radius = collider != nullptr ? collider->radius : defaults.radius;
            const float height = collider != nullptr ? collider->height : defaults.height;
            ecs::RigidBody rigid;
            rigid.body = m_ctx.physics->CreateCharacterBody(
                radius, height, position->x, position->y, position->z);
            registry.emplace<ecs::RigidBody>(entity, rigid);
        }
        if (!registry.all_of<ecs::AnimState>(entity)) {
            registry.emplace<ecs::AnimState>(entity);
        }
        if (!registry.all_of<ecs::MoveInput>(entity)) {
            registry.emplace<ecs::MoveInput>(entity);
        }
        if (!registry.all_of<ecs::MoveIntent>(entity)) {
            registry.emplace<ecs::MoveIntent>(entity);
        }
        if (!registry.all_of<ecs::PathRequest>(entity)) {
            registry.emplace<ecs::PathRequest>(entity);
        }
        if (!registry.all_of<ecs::PathFollow>(entity)) {
            registry.emplace<ecs::PathFollow>(entity);
        }
        if (!registry.all_of<ecs::AttackIntent>(entity)) {
            registry.emplace<ecs::AttackIntent>(entity);
        }

        if (isPlayer) {
            if (!registry.all_of<ecs::LocalControl>(entity)) {
                registry.emplace<ecs::LocalControl>(entity);
            }
            if (!registry.all_of<ecs::Container>(entity)) {
                registry.emplace<ecs::Container>(
                    entity, ecs::Container{{}, m_playerCapacityKg});
            }
            if (!registry.all_of<ecs::Equipment>(entity)) {
                registry.emplace<ecs::Equipment>(entity);
            }
            if (!registry.all_of<ecs::KnownRecipes>(entity)) {
                registry.emplace<ecs::KnownRecipes>(entity);
            }
            if (!registry.all_of<ecs::CraftingJob>(entity)) {
                registry.emplace<ecs::CraftingJob>(entity);
            }
            if (m_player == entt::null) {
                m_player = entity;
            }
            ++players;
        } else {
            if (!registry.all_of<ecs::AiState>(entity)) {
                registry.emplace<ecs::AiState>(entity);
            }
            if (!registry.all_of<ecs::PerceptionState>(entity)) {
                ecs::PerceptionState perception;
                perception.lastKnownX = position->x;
                perception.lastKnownY = position->y;
                registry.emplace<ecs::PerceptionState>(entity, perception);
            }
            if (!registry.all_of<ecs::LodState>(entity)) {
                registry.emplace<ecs::LodState>(entity);
            }
            ++characters;
        }
    }
    ZL_INFO("save", "运行时组件补建：玩家 {}，僵尸 {}", players, characters);
}

bool WorldDemoState::CaptureSaveChunks(std::vector<save::ChunkDiff>* out,
                                       std::string* error) {
    if (out == nullptr || m_ctx.chunkManager == nullptr) {
        if (error != nullptr) {
            *error = "chunk 快照输出为空";
        }
        return false;
    }
    bool ok = true;
    m_ctx.chunkManager->ForEachLoadedChunk(
        [&](const world::Chunk& chunk) {
            if (!ok) {
                return;
            }
            save::ChunkDiff diff;
            if (!save::CaptureChunkDiff(chunk, &diff, error)) {
                ok = false;
                return;
            }
            out->push_back(std::move(diff));
        });
    return ok;
}

void WorldDemoState::RequestQuickSave(bool withThumbnail) {
    if (m_networkMode) {
        SetNotice("multiplayer: server owns the world", kNoticeInfoColor, 2.0f);
        return;
    }
    if (m_savePending || m_saveRequestPending) {
        SetNotice("存档正在进行中", kNoticeInfoColor, 1.5f);
        return;
    }
    if (m_ctx.saveManager == nullptr || m_ctx.saveService == nullptr ||
        m_ctx.saveComponents == nullptr || m_ctx.ecs == nullptr) {
        SetNotice("存档服务未就绪", kNoticeFailColor, 2.5f);
        return;
    }

    SyncCraftingJobToEcs();
    save::EntitySnapshot entities;
    std::string error;
    if (!save::CaptureEntities(*m_ctx.ecs, *m_ctx.saveComponents, &entities, {},
                               &error)) {
        SetNotice("保存快照失败：" + error, kNoticeFailColor, 3.5f);
        ZL_ERROR("save", "保存快照失败：{}", error);
        return;
    }
    std::vector<save::ChunkDiff> chunks;
    if (!CaptureSaveChunks(&chunks, &error)) {
        SetNotice("保存 chunk 失败：" + error, kNoticeFailColor, 3.5f);
        ZL_ERROR("save", "保存 chunk 快照失败：{}", error);
        return;
    }

    m_pendingSave = save::SaveRequest{};
    m_pendingSave.id = m_saveId;
    m_pendingSave.name = "快速存档";
    m_pendingSave.mapId =
        m_ctx.chunkManager != nullptr ? m_ctx.chunkManager->Map().name : std::string();
    m_pendingSave.gameVersion =
        m_ctx.config->Get<std::string>("save.gameVersion", save::kDefaultGameVersion);
    m_pendingSave.entities = std::move(entities);
    m_pendingSave.chunks = std::move(chunks);

    if (m_player != entt::null && m_ctx.ecs->Alive(m_player)) {
        const std::uint64_t playerId = m_ctx.ecs->IdOf(m_player).id;
        if (playerId != 0) {
            m_pendingSave.players.push_back(
                save::SavePlayer{playerId, "Player", true});
        }
    }
    m_saveRequestPending = true;

    if (!withThumbnail) {
        SubmitPendingSave();
        return;
    }

    std::string rootError;
    const bool rootReady = m_ctx.saveManager->EnsureRoot(&rootError);
    const std::filesystem::path shotPath =
        m_ctx.saveManager->Root() / ".screenshot-readback";
    if (!rootReady || m_ctx.renderer == nullptr ||
        !m_ctx.renderer->RequestScreenshot(shotPath.string())) {
        ZL_WARN("save", "缩略图请求失败，改为无缩略图保存：{}",
                rootReady ? "renderer 不可用" : rootError);
        SubmitPendingSave();
        return;
    }
    m_screenshotPending = true;
    m_screenshotTimer = 0.0;
    SetNotice("正在生成存档缩略图…", kNoticeInfoColor, 2.0f);
}

void WorldDemoState::SubmitPendingSave() {
    if (!m_saveRequestPending) {
        return;
    }
    m_screenshotPending = false;
    if (m_ctx.saveService == nullptr) {
        m_saveRequestPending = false;
        m_pendingSave = save::SaveRequest{};
        SetNotice("存档服务未就绪", kNoticeFailColor, 2.5f);
        return;
    }

    const std::string saveId = m_pendingSave.id;
    std::string error;
    if (!m_ctx.saveService->Request(std::move(m_pendingSave), &error)) {
        m_saveRequestPending = false;
        m_pendingSave = save::SaveRequest{};
        SetNotice("提交后台存档失败：" + error, kNoticeFailColor, 3.5f);
        return;
    }
    m_pendingSave = save::SaveRequest{};
    m_saveRequestPending = false;
    m_savePending = true;
    if (m_ctx.events != nullptr) {
        m_ctx.events->Emit(core::kOnSaveBegin);
    }
    SetNotice("正在后台保存…", kNoticeInfoColor, 2.0f);
    ZL_INFO("save", "已提交后台存档请求（{}）",
            saveId.empty() ? "新建" : saveId);
}

void WorldDemoState::PollSaveScreenshot(double frameDt) {
    if (!m_screenshotPending) {
        return;
    }
    m_screenshotTimer += frameDt;
    render::ScreenshotImage image;
    if (m_ctx.renderer != nullptr && m_ctx.renderer->PollScreenshot(&image)) {
        if (image.Valid() && image.width <= 16384 && image.height <= 16384) {
            m_pendingSave.thumbnail.width = static_cast<int>(image.width);
            m_pendingSave.thumbnail.height = static_cast<int>(image.height);
            m_pendingSave.thumbnail.rgba = std::move(image.rgba);
        } else {
            ZL_WARN("save", "缩略图回读数据非法，改为无缩略图保存");
        }
        m_screenshotPending = false;
        SubmitPendingSave();
        return;
    }
    if (m_screenshotTimer >= 2.0) {
        ZL_WARN("save", "缩略图回读超时，改为无缩略图保存");
        m_screenshotPending = false;
        SubmitPendingSave();
    }
}

void WorldDemoState::HandleSaveFinished(bool ok, bool created,
                                        const std::string& id,
                                        const std::string& error) {
    m_savePending = false;
    if (ok) {
        m_saveId = id;
        m_ctx.activeSaveId = id;
        m_ctx.activeSaveUseBackup = false;
        SetNotice(created ? "存档已创建" : "存档已保存", kNoticeOkColor, 2.5f);
        ZL_INFO("save", "{}：{}", created ? "存档已创建" : "存档已保存", id);
    } else {
        SetNotice("保存失败：" + error, kNoticeFailColor, 4.0f);
        ZL_ERROR("save", "保存失败：{}", error);
    }
    if (m_ctx.events != nullptr) {
        m_ctx.events->Emit(core::kOnSaveEnd);
    }
}

void WorldDemoState::ConsumeSaveWarning() {
    if (m_ctx.saveWarning.empty()) {
        return;
    }
    const std::string warning = std::move(m_ctx.saveWarning);
    m_ctx.saveWarning.clear();
    SetNotice("存档警告：" + warning, kNoticeFailColor, 5.0f);
    ZL_WARN("save", "{}", warning);
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

bool WorldDemoState::IsSolidSquare(int wx, int wy) const {
    return IsSquareBlocked(*m_ctx.world, wx, wy, 0);
}

void WorldDemoState::SyncNetworkReplicas(float alpha) {
    if (!m_networkMode || m_ctx.networkClient == nullptr || m_ctx.ecs == nullptr) {
        return;
    }
    const auto& actors = m_ctx.networkClient->Replication().Actors();
    entt::registry& registry = m_ctx.ecs->Registry();
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(actors.size());

    for (const auto& [stableId, actor] : actors) {
        if (stableId == 0 || !actor.hasPosition) {
            continue;
        }
        seen.insert(stableId);
        ecs::Entity entity = entt::null;
        const auto mapped = m_networkEntities.find(stableId);
        if (mapped != m_networkEntities.end() && m_ctx.ecs->Alive(mapped->second)) {
            entity = mapped->second;
        } else {
            if (mapped != m_networkEntities.end()) {
                m_networkEntities.erase(mapped);
            }
            entity = m_ctx.ecs->CreateWithStableId(stableId);
            if (entity == entt::null) {
                // 仅当本地已有同 ID 实体（非网络状态残留）时复用；否则跳过该副本。
                entity = m_ctx.ecs->FindById(stableId);
                if (entity == entt::null) {
                    ZL_WARN("net", "无法创建网络副本实体 stableId={}（ID 冲突）", stableId);
                    continue;
                }
            }
            m_networkEntities.emplace(stableId, entity);
        }

        ecs::Position position = actor.position;
        (void)m_ctx.networkClient->Replication().SamplePosition(stableId, alpha, &position);
        if (auto* current = registry.try_get<ecs::Position>(entity); current != nullptr) {
            *current = position;
        } else {
            registry.emplace<ecs::Position>(entity, position);
        }
        if (auto* velocity = registry.try_get<ecs::Velocity>(entity); velocity != nullptr) {
            *velocity = actor.velocity;
        } else {
            registry.emplace<ecs::Velocity>(entity, actor.velocity);
        }
        if (actor.hasHealth) {
            if (auto* health = registry.try_get<ecs::Health>(entity); health != nullptr) {
                *health = actor.health;
            } else {
                registry.emplace<ecs::Health>(entity, actor.health);
            }
        }
        if (actor.hasSprite) {
            if (auto* sprite = registry.try_get<ecs::SpriteRef>(entity); sprite != nullptr) {
                *sprite = actor.sprite;
            } else {
                registry.emplace<ecs::SpriteRef>(entity, actor.sprite);
            }
        }
        if (actor.hasFacing) {
            if (auto* facing = registry.try_get<ecs::Facing>(entity); facing != nullptr) {
                *facing = actor.facing;
            } else {
                registry.emplace<ecs::Facing>(entity, actor.facing);
            }
        }
        if (actor.hasAnim) {
            if (auto* anim = registry.try_get<ecs::AnimState>(entity); anim != nullptr) {
                *anim = actor.anim;
            } else {
                registry.emplace<ecs::AnimState>(entity, actor.anim);
            }
        }
        if (actor.hasItem) {
            if (auto* item = registry.try_get<ecs::ItemInstance>(entity);
                item != nullptr) {
                *item = actor.item;
            } else {
                registry.emplace<ecs::ItemInstance>(entity, actor.item);
            }
        }
        if (actor.hasEquipment) {
            registry.emplace_or_replace<ecs::Equipment>(entity, actor.equipment);
        }
        if (actor.hasAction) {
            registry.emplace_or_replace<ecs::ActionState>(entity, actor.action);
        }
    }

    for (auto it = m_networkEntities.begin(); it != m_networkEntities.end();) {
        if (seen.find(it->first) == seen.end()) {
            if (m_ctx.ecs->Alive(it->second)) {
                m_ctx.ecs->Destroy(it->second);
            }
            it = m_networkEntities.erase(it);
        } else {
            ++it;
        }
    }
    m_ctx.ecs->Flush();
    if (m_ctx.spatialIndex != nullptr) {
        m_ctx.spatialIndex->Update(*m_ctx.ecs);
    }

    const std::uint64_t localId = m_ctx.networkClient->PlayerStableId();
    const ecs::Entity nextPlayer =
        localId != 0 ? m_ctx.ecs->FindById(localId) : entt::null;
    if (nextPlayer != m_player) {
        m_player = nextPlayer;
        m_followSnapped = false;
        m_footstepAnchorValid = false;
        m_lastPlayerPathState = ecs::PathStatus::None;
        m_markerPathActive = false;
        m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
        m_moveCommandValid = false;
        m_lastNetworkStatusSequence = 0;
    }
    ApplyNetworkPlayerState();
}

void WorldDemoState::ApplyNetworkPlayerState() {
    if (!m_networkMode || m_ctx.networkClient == nullptr || m_ctx.ecs == nullptr ||
        !m_ctx.networkClient->HasPlayerState()) {
        return;
    }
    const net::PlayerState& state = m_ctx.networkClient->PlayerState();
    if (state.playerStableId != m_ctx.networkClient->PlayerStableId()) {
        return;
    }
    m_networkPlayerAlive = state.alive;
    m_networkHealth = state.health;
    m_networkHealthMax = state.healthMax;
    m_networkRespawnSeconds = state.respawnSeconds;

    if (state.statusSequence != m_lastNetworkStatusSequence) {
        m_lastNetworkStatusSequence = state.statusSequence;
        if (!state.status.empty()) {
            const std::uint32_t color =
                state.statusTone == 1 ? kNoticeOkColor
                : state.statusTone == 2 ? kNoticeFailColor
                                        : kNoticeInfoColor;
            SetNotice(state.status, color, 2.2f);
        }
    }
    if (m_player == entt::null) {
        return;
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    if (auto* health = registry.try_get<ecs::Health>(m_player); health != nullptr) {
        health->current = state.health;
        health->max = state.healthMax;
    } else {
        registry.emplace<ecs::Health>(
            m_player, ecs::Health{state.health, state.healthMax});
    }
    ecs::Container container;
    container.capacityKg = state.capacityKg;
    container.items.reserve(state.bag.size());
    for (const net::PlayerStateItem& item : state.bag) {
        container.items.push_back(
            ecs::ItemInstance{item.defId, item.count, item.condition});
    }
    registry.emplace_or_replace<ecs::Container>(m_player, std::move(container));

    ecs::Equipment equipment;
    for (std::size_t i = 0; i < state.equipment.size() &&
                            i < equipment.slots.size();
         ++i) {
        if (!state.equipment[i].present) {
            continue;
        }
        const net::PlayerStateItem& item = state.equipment[i].item;
        equipment.slots[i] =
            ecs::ItemInstance{item.defId, item.count, item.condition};
    }
    registry.emplace_or_replace<ecs::Equipment>(m_player, std::move(equipment));

    ecs::KnownRecipes known;
    known.recipeIds = state.knownRecipes;
    registry.emplace_or_replace<ecs::KnownRecipes>(m_player, std::move(known));

    ecs::CraftingJob job;
    job.active = state.craftingActive;
    job.recipeId = state.craftingRecipeId;
    job.elapsed = state.craftingElapsed;
    job.duration = state.craftingDuration;
    job.toolEfficiency = state.craftingToolEfficiency;
    job.batchTotal = state.batchTotal;
    job.batchCompleted = state.batchCompleted;
    job.queue.reserve(state.craftQueue.size());
    for (const net::PlayerStateCraftQueue& entry : state.craftQueue) {
        job.queue.push_back(ecs::CraftQueueItem{entry.recipeId, entry.count});
    }
    registry.emplace_or_replace<ecs::CraftingJob>(m_player, std::move(job));
}

void WorldDemoState::RefreshSocialState() {
    if (m_ctx.ui == nullptr || !m_networkMode || m_ctx.networkClient == nullptr) {
        return;
    }
    ui::SocialUiState state;
    state.visible = m_ctx.networkClient->IsConnected();
    if (state.visible && m_ctx.networkClient->HasSocialState()) {
        const net::SocialState& social = m_ctx.networkClient->SocialState();
        const std::uint64_t localId = m_ctx.networkClient->PlayerStableId();
        state.players.reserve(social.players.size());
        for (const net::SocialPlayer& player : social.players) {
            state.players.push_back(
                ui::SocialPlayerUi{player.stableId, player.name,
                                   player.stableId == localId});
        }
        state.chat.reserve(social.chat.size());
        for (const net::SocialChatLine& line : social.chat) {
            state.chat.push_back(ui::SocialChatUi{line.sender, line.text});
        }
        if (state.players.empty()) {
            state.status = "等待其他玩家加入";
            state.statusTone = "info";
        }
    } else if (state.visible) {
        state.status = "正在同步玩家列表...";
        state.statusTone = "info";
    }
    m_ctx.ui->SetSocialState(state);
}

void WorldDemoState::SendChat(const std::string& text, std::string& error) {
    if (!m_networkMode || m_ctx.networkClient == nullptr) {
        error = "当前不在联机会话中";
        return;
    }
    if (!m_ctx.networkClient->SendChat(text, &error)) {
        return;
    }
    error.clear();
}
void WorldDemoState::EnsurePlayerSpawned() {
    if (m_ctx.ecs == nullptr || m_ctx.simulation == nullptr) {
        return;
    }
    if (m_networkMode) {
        const std::uint64_t networkId =
            m_ctx.networkClient != nullptr ? m_ctx.networkClient->PlayerStableId() : 0;
        const ecs::Entity next =
            networkId != 0 ? m_ctx.ecs->FindById(networkId) : entt::null;
        if (next != m_player) {
            m_player = next;
            if (m_player != entt::null) {
                m_followSnapped = false;
                m_footstepAnchorValid = false;
                m_lastPlayerPathState = ecs::PathStatus::None;
                m_markerPathActive = false;
                m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
                m_moveCommandValid = false;
            }
        }
        return;
    }
    // M5-T1：玩家死亡（实体已回收）→ 清引用重新生成（尸体留在原地）
    if (m_player != entt::null && !m_ctx.ecs->Alive(m_player)) {
        ZL_INFO("game", "玩家实体已死亡，重新生成（旧实体 ID {}）",
                m_ctx.ecs->IdOf(m_player).id);
        m_player = entt::null;
        m_lastPlayerPathState = ecs::PathStatus::None;
        m_markerPathActive = false;
        m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
        m_moveCommandValid = false;
        if (m_ctx.ui != nullptr) {
            m_ctx.ui->CloseInventory();
        }
    }
    if (m_player != entt::null) {
        return;
    }
    // 目标点 = 村庄南侧开阔地；WorldSimulation 会等目标 chunk 加载完成后再找可走格。
    m_player = m_ctx.simulation->EnsureLocalPlayer(70.0f, 24.0f, 24);
    if (m_player == entt::null) {
        return;
    }
    m_footstepAnchorValid = false;
    m_footstepDistance = 0.0f;
    m_lastPlayerPathState = ecs::PathStatus::None;
    m_markerPathActive = false;
    m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
    m_moveCommandValid = false;
}
void WorldDemoState::SetNotice(std::string text, uint32_t color, float seconds) {
    m_noticeText = std::move(text);
    m_noticeTone = color == kNoticeOkColor ? "ok"
                   : color == kNoticeFailColor ? "fail"
                                               : "info";
    m_noticeTimer = seconds;
    m_noticeDuration = seconds;
}

void WorldDemoState::RefreshHud() {
    if (m_ctx.ui == nullptr || m_ctx.gameTime == nullptr) {
        return;
    }
    ui::HudState hud;
    const bool networkState =
        m_networkMode && m_ctx.networkClient != nullptr &&
        m_ctx.networkClient->HasPlayerState();
    hud.active = (m_ctx.ecs != nullptr && m_player != entt::null) || networkState;
    const auto& now = m_ctx.gameTime->Now();
    const int day =
        1 + static_cast<int>(m_ctx.gameTime->TotalGameMinutes() / (24.0 * 60.0));
    char timeText[16];
    std::snprintf(timeText, sizeof(timeText), "%02d:%02d", now.hour, now.minute);
    hud.day = "第 " + std::to_string(day) + " 天";
    hud.time = timeText;
    hud.speed = core::GameTime::SpeedName(m_ctx.gameTime->GetSpeed());
    hud.selectedSlot = std::clamp(m_hotbarSelected, 0, 4);
    for (ui::HudSlot& slot : hud.slots) {
        slot.label = "空";
    }

    if (m_ctx.ecs != nullptr && m_player != entt::null) {
        entt::registry& registry = m_ctx.ecs->Registry();
        if (const auto* health = registry.try_get<ecs::Health>(m_player);
            health != nullptr) {
            hud.playerValid = true;
            hud.health = health->current;
            hud.healthMax = health->max;
        }
        if (const auto* needs = registry.try_get<ecs::Needs>(m_player); needs != nullptr) {
            hud.stamina = (1.0f - std::clamp(needs->fatigue, 0.0f, 1.0f)) * 100.0f;
            hud.staminaMax = 100.0f;
        } else {
            hud.stamina = 100.0f;
            hud.staminaMax = 100.0f;
        }
        if (const auto* container = registry.try_get<ecs::Container>(m_player);
            container != nullptr) {
            const std::size_t count = std::min<std::size_t>(container->items.size(), 5);
            for (std::size_t i = 0; i < count; ++i) {
                const ecs::ItemInstance& entry = container->items[i];
                const items::ItemDef* def = m_ctx.items != nullptr
                                                ? m_ctx.items->Find(entry.defId)
                                                : nullptr;
                hud.slots[i].label = def != nullptr ? def->name : entry.defId;
                hud.slots[i].count = entry.count;
            }
        }
    }

    if (networkState && m_player == entt::null) {
        hud.playerValid = true;
        hud.health = m_networkHealth;
        hud.healthMax = m_networkHealthMax;
        hud.stamina = 100.0f;
        hud.staminaMax = 100.0f;
    }
    if (networkState && !m_networkPlayerAlive && m_noticeTimer <= 0.0f) {
        hud.notice = "等待复活";
        hud.noticeTone = "fail";
        hud.noticeRemaining = std::max(0.1f, m_networkRespawnSeconds);
        hud.noticeDuration = std::max(0.1f, m_networkRespawnSeconds);
    }

    if (m_noticeTimer > 0.0f && !m_noticeText.empty()) {
        hud.notice = m_noticeText;
        hud.noticeTone = m_noticeTone;
        hud.noticeRemaining = m_noticeTimer;
        hud.noticeDuration = std::max(m_noticeDuration, 0.1f);
    }
    m_ctx.hudState = hud;
    m_ctx.ui->SetHudState(hud);
}

void WorldDemoState::RefreshInventory() {
    if (m_ctx.ui == nullptr) {
        return;
    }
    ui::InventoryState state;
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        m_ctx.ui->SetInventoryState(state);
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* container = registry.try_get<ecs::Container>(m_player);
    const auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (container == nullptr) {
        m_ctx.ui->SetInventoryState(state);
        return;
    }

    state.playerValid = true;
    state.capacity = container->capacityKg;
    state.weight = items::TotalWeight(*container, *m_ctx.items);
    state.bagStacks = static_cast<int>(container->items.size());

    const int repairKits =
        m_repairKitDefId.empty() || m_repairKitCount <= 0
            ? 0
            : items::CountOf(*container, m_repairKitDefId);
    const int bagCount = std::min<int>(static_cast<int>(container->items.size()),
                                       ui::kInventoryBagSlots);
    for (int i = 0; i < bagCount; ++i) {
        const ecs::ItemInstance& entry = container->items[static_cast<std::size_t>(i)];
        const items::ItemDef* def = m_ctx.items->Find(entry.defId);
        ui::InventorySlot& slot = state.bag[static_cast<std::size_t>(i)];
        slot.filled = true;
        slot.label = def != nullptr ? def->name : entry.defId;
        slot.count = entry.count;
        slot.usable = def != nullptr &&
                      (def->use.Any() || !def->unlocksRecipes.empty());
        slot.equippable = def != nullptr &&
                          (def->type == items::ItemType::Weapon ||
                           def->type == items::ItemType::Clothing);
        slot.condition = entry.condition;
        slot.damaged = def != nullptr && def->type == items::ItemType::Weapon &&
                       def->maxStack == 1 && entry.condition > 0.0f &&
                       entry.condition < 1.0f;
        slot.repairAvailable = slot.damaged && repairKits >= m_repairKitCount;
    }

    if (equipment != nullptr) {
        for (int i = 0; i < ui::kInventoryEquipmentSlots; ++i) {
            const auto& entry =
                equipment->slots[static_cast<std::size_t>(i)];
            if (!entry.has_value()) {
                continue;
            }
            const items::ItemDef* def = m_ctx.items->Find(entry->defId);
            ui::InventorySlot& slot = state.equipment[static_cast<std::size_t>(i)];
            slot.filled = true;
            slot.label = def != nullptr ? def->name : entry->defId;
            slot.count = entry->count;
            slot.condition = entry->condition;
        }
    }

    if (m_ctx.recipes != nullptr) {
        state.recipes.reserve(m_ctx.recipes->Size());
        for (const items::RecipeDef& def : m_ctx.recipes->All()) {
            ui::InventoryRecipe recipe;
            recipe.id = def.id;
            recipe.name = def.name;
            recipe.outputDefId = def.outputDefId;
            recipe.outputCount = def.outputCount;
            const items::ItemDef* output = m_ctx.items->Find(def.outputDefId);
            recipe.outputLabel = output != nullptr ? output->name : def.outputDefId;
            std::string reason;
            const bool inventoryOk =
                items::CanCraft(*container, *m_ctx.items, def, &reason);
            recipe.station = def.station;
            recipe.stationLabel = StationLabel(def.station);
            recipe.stationNearby =
                def.station.empty() ||
                (position != nullptr &&
                 StationNearby(def.station, position->x, position->y, position->z));
            recipe.known = RecipeKnown(def.id);
            recipe.canCraft = recipe.known && inventoryOk && recipe.stationNearby;
            recipe.ingredients.reserve(def.ingredients.size());
            for (const items::RecipeIngredient& ingredient : def.ingredients) {
                const items::ItemDef* ingredientDef =
                    m_ctx.items->Find(ingredient.defId);
                recipe.ingredients.push_back(ui::InventoryRecipeIngredient{
                    ingredientDef != nullptr ? ingredientDef->name : ingredient.defId,
                    ingredient.count, items::CountOf(*container, ingredient.defId)});
            }
            recipe.tools.reserve(def.tools.size());
            for (const items::RecipeIngredient& tool : def.tools) {
                const items::ItemDef* toolDef = m_ctx.items->Find(tool.defId);
                recipe.tools.push_back(ui::InventoryRecipeIngredient{
                    toolDef != nullptr ? toolDef->name : tool.defId, tool.count,
                    items::CountUsableTools(*container, tool.defId)});
            }
            const float efficiency = items::ToolEfficiency(*container, def);
            recipe.estimatedSeconds = items::EffectiveCraftSeconds(def, efficiency);
            state.recipes.push_back(std::move(recipe));
        }
    }

    const ecs::CraftingJob* networkJob =
        m_networkMode ? registry.try_get<ecs::CraftingJob>(m_player) : nullptr;
    if (networkJob != nullptr && networkJob->active && m_ctx.recipes != nullptr) {
        const items::RecipeDef* def =
            m_ctx.recipes->Find(networkJob->recipeId);
        if (def != nullptr) {
            const items::ItemDef* output = m_ctx.items->Find(def->outputDefId);
            state.crafting.active = true;
            state.crafting.recipeId = def->id;
            state.crafting.outputLabel =
                output != nullptr ? output->name : def->outputDefId;
            state.crafting.outputCount = def->outputCount;
            state.crafting.elapsed = networkJob->elapsed;
            state.crafting.duration = networkJob->duration;
            state.crafting.progress =
                networkJob->duration > 0.0f
                    ? std::clamp(networkJob->elapsed / networkJob->duration,
                                 0.0f, 1.0f)
                    : 0.0f;
            state.crafting.toolEfficiency = networkJob->toolEfficiency;
            state.crafting.batchCount = std::max(networkJob->batchTotal, 1);
            state.crafting.completedCount =
                std::clamp(networkJob->batchCompleted, 0,
                           state.crafting.batchCount);
        }
    } else if (!m_networkMode && m_crafting.active && m_ctx.recipes != nullptr) {
        const items::RecipeDef* def = m_ctx.recipes->Find(m_crafting.recipeId);
        if (def != nullptr) {
            const items::ItemDef* output = m_ctx.items->Find(def->outputDefId);
            state.crafting.active = true;
            state.crafting.recipeId = def->id;
            state.crafting.outputLabel =
                output != nullptr ? output->name : def->outputDefId;
            state.crafting.outputCount = def->outputCount;
            state.crafting.elapsed = m_crafting.elapsed;
            state.crafting.duration = m_crafting.duration;
            state.crafting.progress = m_crafting.Progress();
            state.crafting.toolEfficiency = m_crafting.toolEfficiency;
            state.crafting.batchCount = std::max(m_craftBatchTotal, 1);
            state.crafting.completedCount =
                std::clamp(m_craftBatchCompleted, 0, state.crafting.batchCount);
        }
    }
    if (m_ctx.recipes != nullptr) {
        const auto appendQueue = [&](const auto& queue) {
            state.crafting.queue.reserve(queue.size());
            for (const ecs::CraftQueueItem& queued : queue) {
                const items::RecipeDef* def =
                    m_ctx.recipes->Find(queued.recipeId);
                if (def == nullptr) {
                    continue;
                }
                const items::ItemDef* output =
                    m_ctx.items->Find(def->outputDefId);
                state.crafting.queue.push_back(ui::InventoryQueuedCraft{
                    def->id, output != nullptr ? output->name : def->outputDefId,
                    def->outputCount, queued.count});
            }
        };
        if (networkJob != nullptr) {
            appendQueue(networkJob->queue);
        } else if (!m_networkMode) {
            appendQueue(m_craftQueue);
        }
    }

    if (position != nullptr) {
        const std::vector<game::GroundItemInfo> ground =
            game::GroundItemsInRadius(*m_ctx.ecs, position->x, position->y, position->z,
                                      m_inventoryRange);
        state.lootStacks = static_cast<int>(ground.size());
        const int lootCount =
            std::min<int>(static_cast<int>(ground.size()), ui::kInventoryLootSlots);
        for (int i = 0; i < lootCount; ++i) {
            const game::GroundItemInfo& info = ground[static_cast<std::size_t>(i)];
            const items::ItemDef* def = m_ctx.items->Find(info.item.defId);
            ui::InventorySlot& slot = state.loot[static_cast<std::size_t>(i)];
            slot.filled = true;
            slot.label = def != nullptr ? def->name : info.item.defId;
            slot.count = info.item.count;
            slot.entityId = m_ctx.ecs->IdOf(info.entity).id;
            slot.condition = info.item.condition;
        }
    }

    if (m_noticeTimer > 0.0f && !m_noticeText.empty()) {
        state.status = m_noticeText;
        state.statusTone = m_noticeTone;
    }

    m_ctx.ui->SetInventoryState(state);
}

void WorldDemoState::ReturnOrDropItem(const ecs::ItemInstance& item) {
    if (!item.Valid() || m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (container != nullptr) {
        const int added = items::AddItem(*container, *m_ctx.items, item);
        if (added >= item.count) {
            return;
        }
        ecs::ItemInstance remainder = item;
        remainder.count -= added;
        if (position != nullptr) {
            game::SpawnGroundItem(*m_ctx.ecs, *m_ctx.items, remainder, position->x,
                                  position->y, position->z);
        }
        return;
    }
    if (position != nullptr) {
        game::SpawnGroundItem(*m_ctx.ecs, *m_ctx.items, item, position->x, position->y,
                              position->z);
    }
}

void WorldDemoState::EquipBagItem(int index, ecs::EquipSlot slot) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    if (container == nullptr || equipment == nullptr ||
        static_cast<std::size_t>(index) >= container->items.size()) {
        return;
    }
    const ecs::ItemInstance entry = container->items[static_cast<std::size_t>(index)];
    const items::ItemDef* def = m_ctx.items->Find(entry.defId);
    if (def == nullptr || !items::SlotAccepts(slot, def->type)) {
        SetNotice("无法装备该物品", kNoticeFailColor, 1.5f);
        return;
    }

    ecs::ItemInstance one = entry;
    one.count = 1;
    ecs::ItemInstance removed;
    if (!items::TakeAt(*container, static_cast<std::size_t>(index), 1, &removed)) {
        return;
    }
    ecs::ItemInstance swapped;
    if (!items::Equip(*equipment, *m_ctx.items, slot, one, &swapped)) {
        ReturnOrDropItem(removed);
        SetNotice("无法装备该物品", kNoticeFailColor, 1.5f);
        return;
    }
    if (swapped.Valid()) {
        ReturnOrDropItem(swapped);
    }
    SetNotice("装备了 " + def->name, kNoticeOkColor, 1.5f);
    ZL_INFO("items", "装备（背包槽 {}）：{}", index, def->name);
}

void WorldDemoState::EquipLootItem(std::uint64_t stableId, ecs::EquipSlot slot) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
    if (entity == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* instance = registry.try_get<ecs::ItemInstance>(entity);
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    if (instance == nullptr || equipment == nullptr) {
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(instance->defId);
    if (def == nullptr || !items::SlotAccepts(slot, def->type)) {
        SetNotice("无法装备该物品", kNoticeFailColor, 1.5f);
        return;
    }
    ecs::ItemInstance one = *instance;
    one.count = 1;
    ecs::ItemInstance swapped;
    if (!items::Equip(*equipment, *m_ctx.items, slot, one, &swapped)) {
        return;
    }
    const std::uint64_t entityId = m_ctx.ecs->IdOf(entity).id;
    instance->count -= 1;
    if (instance->count <= 0) {
        m_ctx.ecs->Destroy(entity);
    }
    if (m_ctx.events != nullptr) {
        m_ctx.events->EmitQueued(core::kOnItemPickup,
                                 core::ItemEvent{entityId, one.defId, 1});
    }
    if (swapped.Valid()) {
        ReturnOrDropItem(swapped);
    }
    SetNotice("装备了 " + def->name, kNoticeOkColor, 1.5f);
    ZL_INFO("items", "装备（地面）：{}", def->name);
}

void WorldDemoState::UnequipEquipment(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0 || index >= ui::kInventoryEquipmentSlots) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    if (equipment == nullptr) {
        return;
    }
    ecs::ItemInstance removed;
    if (!items::Unequip(*equipment, static_cast<ecs::EquipSlot>(index), &removed)) {
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(removed.defId);
    ReturnOrDropItem(removed);
    SetNotice("卸下了 " + (def != nullptr ? def->name : removed.defId), kNoticeInfoColor,
              1.5f);
    ZL_INFO("items", "卸下：{}", def != nullptr ? def->name : removed.defId);
}

void WorldDemoState::UseBagItem(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr ||
        static_cast<std::size_t>(index) >= container->items.size()) {
        return;
    }
    const items::ItemDef* def =
        m_ctx.items->Find(container->items[static_cast<std::size_t>(index)].defId);
    if (def != nullptr && !def->unlocksRecipes.empty()) {
        UseBlueprintItem(index, *def);
        return;
    }
    auto* health = registry.try_get<ecs::Health>(m_player);
    if (health == nullptr) {
        return;
    }
    const items::UseResult use =
        items::UseAt(*container, *m_ctx.items, static_cast<std::size_t>(index));
    if (!use.applied) {
        SetNotice("该物品不能使用", kNoticeFailColor, 1.5f);
        return;
    }
    auto* needs = registry.try_get<ecs::Needs>(m_player);
    const float healed = items::ApplyUseEffect(
        use.effect, health->max, &health->current,
        needs != nullptr ? &needs->hunger : nullptr,
        needs != nullptr ? &needs->thirst : nullptr,
        needs != nullptr ? &needs->fatigue : nullptr);
    const std::string name = def != nullptr ? def->name : use.defId;
    if (use.effect.heal > 0.0f) {
        SetNotice("使用了 " + name + "（+" + std::to_string(static_cast<int>(healed)) +
                      " HP）",
                  kNoticeOkColor, 1.8f);
    } else {
        SetNotice("使用了 " + name, kNoticeOkColor, 1.8f);
    }
    ZL_INFO("items", "使用（背包槽 {}）：{}（回血 {:.0f}，剩余 {}）", index, name, healed,
            use.remaining);
}

bool WorldDemoState::UseBlueprintItem(int index, const items::ItemDef& def) {
    if (m_ctx.ecs == nullptr || m_ctx.recipes == nullptr || m_player == entt::null ||
        index < 0) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr ||
        static_cast<std::size_t>(index) >= container->items.size()) {
        return false;
    }
    auto* known = registry.try_get<ecs::KnownRecipes>(m_player);
    if (known == nullptr) {
        known = &registry.emplace<ecs::KnownRecipes>(m_player);
    }

    std::vector<std::string> learned;
    for (const std::string& recipeId : def.unlocksRecipes) {
        if (m_ctx.recipes->Find(recipeId) == nullptr || RecipeKnown(recipeId)) {
            continue;
        }
        learned.push_back(recipeId);
    }
    if (learned.empty()) {
        SetNotice("该配方已学会，无需再次使用", kNoticeInfoColor, 1.8f);
        return true;
    }

    ecs::ItemInstance removed;
    if (!items::TakeAt(*container, static_cast<std::size_t>(index), 1, &removed)) {
        return false;
    }
    std::string labels;
    for (const std::string& recipeId : learned) {
        known->recipeIds.push_back(recipeId);
        const items::RecipeDef* recipe = m_ctx.recipes->Find(recipeId);
        if (!labels.empty()) {
            labels += "、";
        }
        labels += recipe != nullptr ? recipe->name : recipeId;
    }
    std::sort(known->recipeIds.begin(), known->recipeIds.end());
    known->recipeIds.erase(std::unique(known->recipeIds.begin(), known->recipeIds.end()),
                           known->recipeIds.end());
    SetNotice("学会了配方：" + labels, kNoticeOkColor, 2.8f);
    ZL_INFO("items", "使用蓝图/杂志 '{}'，学会配方：{}", def.name, labels);
    RefreshInventory();
    return true;
}

void WorldDemoState::RepairBagItem(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr) {
        return;
    }
    const items::RepairResult result =
        items::Repair(*container, *m_ctx.items, static_cast<std::size_t>(index),
                      m_repairKitDefId, m_repairKitCount, m_repairAmount);
    if (!result.ok) {
        SetNotice(result.error, kNoticeFailColor, 2.0f);
        ZL_WARN("items", "维修失败（背包槽 {}）：{}", index, result.error);
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(result.defId);
    const items::ItemDef* material = m_ctx.items->Find(result.materialDefId);
    const std::string name = def != nullptr ? def->name : result.defId;
    const std::string materialName =
        material != nullptr ? material->name : result.materialDefId;
    const int beforePct =
        static_cast<int>(std::lround(result.before * 100.0f));
    const int afterPct =
        static_cast<int>(std::lround(result.after * 100.0f));
    SetNotice("维修成功：" + name + " " + std::to_string(beforePct) + "% → " +
                  std::to_string(afterPct) + "%（消耗 " + materialName + " ×" +
                  std::to_string(result.materialCount) + "）",
              kNoticeOkColor, 2.5f);
    ZL_INFO("items", "维修成功（背包槽 {}）：{} {}% → {}%，消耗 {} ×{}", index,
            name, beforePct, afterPct, materialName, result.materialCount);
}

std::string WorldDemoState::StationLabel(std::string_view station) const {
    if (station.empty()) {
        return {};
    }
    const auto found = m_stationLabels.find(std::string(station));
    return found != m_stationLabels.end() ? found->second : std::string(station);
}

bool WorldDemoState::StationNearby(std::string_view station, float x, float y,
                                   int z) const {
    if (station.empty()) {
        return true;
    }
    if (m_ctx.world == nullptr || m_stationRange <= 0.0f) {
        return false;
    }
    return game::HasStationNearby(*m_ctx.world, x, y, z, station, m_stationRange);
}

bool WorldDemoState::RecipeKnown(std::string_view recipeId) const {
    if (!m_blueprintRecipes.contains(std::string(recipeId))) {
        return true;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return false;
    }
    const auto* known =
        m_ctx.ecs->Registry().try_get<ecs::KnownRecipes>(m_player);
    if (known == nullptr) {
        return false;
    }
    return std::find(known->recipeIds.begin(), known->recipeIds.end(), recipeId) !=
           known->recipeIds.end();
}

void WorldDemoState::DropBagItem(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (container == nullptr || position == nullptr ||
        static_cast<std::size_t>(index) >= container->items.size()) {
        return;
    }
    ecs::ItemInstance stack = container->items[static_cast<std::size_t>(index)];
    ecs::ItemInstance removed;
    if (!items::TakeAt(*container, static_cast<std::size_t>(index), stack.count,
                       &removed)) {
        return;
    }
    const ecs::Entity ground = game::SpawnGroundItem(
        *m_ctx.ecs, *m_ctx.items, removed, position->x, position->y, position->z);
    const items::ItemDef* def = m_ctx.items->Find(removed.defId);
    if (m_ctx.events != nullptr && ground != entt::null) {
        m_ctx.events->EmitQueued(
            core::kOnItemDrop,
            core::ItemEvent{m_ctx.ecs->IdOf(ground).id, removed.defId, removed.count});
    }
    SetNotice("丢弃了 " + (def != nullptr ? def->name : removed.defId) + " ×" +
                  std::to_string(removed.count),
              kNoticeInfoColor, 1.5f);
    ZL_INFO("items", "丢弃（背包槽 {}）：{} x{}", index,
            def != nullptr ? def->name : removed.defId, removed.count);
}

void WorldDemoState::DropEquipmentItem(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0 || index >= ui::kInventoryEquipmentSlots) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (equipment == nullptr || position == nullptr) {
        return;
    }
    ecs::ItemInstance removed;
    if (!items::Unequip(*equipment, static_cast<ecs::EquipSlot>(index), &removed)) {
        return;
    }
    const ecs::Entity ground = game::SpawnGroundItem(
        *m_ctx.ecs, *m_ctx.items, removed, position->x, position->y, position->z);
    if (m_ctx.events != nullptr && ground != entt::null) {
        m_ctx.events->EmitQueued(
            core::kOnItemDrop,
            core::ItemEvent{m_ctx.ecs->IdOf(ground).id, removed.defId, removed.count});
    }
    const items::ItemDef* def = m_ctx.items->Find(removed.defId);
    SetNotice("丢弃了 " + (def != nullptr ? def->name : removed.defId), kNoticeInfoColor,
              1.5f);
    ZL_INFO("items", "丢弃装备槽 {}：{}", index,
            def != nullptr ? def->name : removed.defId);
}

void WorldDemoState::PickupLootItem(std::uint64_t stableId) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
    if (entity == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr) {
        return;
    }
    const game::PickupResult result =
        game::PickupGroundItem(*m_ctx.ecs, *m_ctx.items, *container, entity);
    if (!result.found) {
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(result.defId);
    const std::string name = def != nullptr ? def->name : result.defId;
    if (result.picked > 0) {
        if (result.groundRemaining > 0) {
            SetNotice("拾取了 " + name + " ×" + std::to_string(result.picked) + "（地上剩 " +
                          std::to_string(result.groundRemaining) + "）",
                      kNoticeOkColor, 2.0f);
        } else {
            SetNotice("拾取了 " + name + " ×" + std::to_string(result.picked),
                      kNoticeOkColor, 2.0f);
        }
        if (m_ctx.events != nullptr) {
            m_ctx.events->EmitQueued(
                core::kOnItemPickup,
                core::ItemEvent{result.entityId, result.defId, result.picked});
        }
        ZL_INFO("items", "拾取（地面）：{} x{}（剩余 {}）", name, result.picked,
                result.groundRemaining);
    } else {
        SetNotice("背包已满", kNoticeFailColor, 1.5f);
    }
}

net::Command WorldDemoState::MakePlayerCommand(net::CommandType type) const {
    net::Command command;
    command.type = type;
    if (m_ctx.ecs != nullptr && m_player != entt::null && m_ctx.ecs->Alive(m_player)) {
        command.playerStableId = m_ctx.ecs->IdOf(m_player).id;
    }
    if (m_ctx.loop != nullptr) {
        command.tick = m_ctx.loop->TotalTicks();
    }
    return command;
}

bool WorldDemoState::SubmitPlayerCommand(const net::Command& command) {
    if (m_ctx.ecs == nullptr || m_player == entt::null || !m_ctx.ecs->Alive(m_player) ||
        command.playerStableId == 0) {
        return false;
    }
    std::string error;
    if (m_networkMode) {
        if (m_ctx.networkClient == nullptr ||
            !m_ctx.networkClient->SendCommand(command, &error)) {
            if (!error.empty()) {
                ZL_DEBUG("net", "玩家命令 {} 未发送：{}", net::CommandTypeName(command.type),
                         error);
            }
            return false;
        }
        return true;
    }
    if (m_ctx.commands == nullptr) {
        return false;
    }
    if (!m_ctx.commands->Submit(command, &error)) {
        if (!error.empty()) {
            ZL_WARN("net", "玩家命令 {} 未提交：{}", net::CommandTypeName(command.type), error);
        }
        return false;
    }
    return true;
}

void WorldDemoState::SubmitMoveCommand(float screenX, float screenY) {
    if (m_ctx.ecs == nullptr || m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
        return;
    }
    if (!std::isfinite(screenX) || !std::isfinite(screenY)) {
        return;
    }
    if (m_moveCommandValid && std::fabs(screenX - m_lastMoveCommand.x) < 0.0001f &&
        std::fabs(screenY - m_lastMoveCommand.y) < 0.0001f) {
        return;
    }
    net::Command command = MakePlayerCommand(net::CommandType::Move);
    command.payload.x = screenX;
    command.payload.y = screenY;
    if (SubmitPlayerCommand(command)) {
        m_lastMoveCommand = glm::vec2(screenX, screenY);
        m_moveCommandValid = true;
    }
}

void WorldDemoState::SubmitInventoryAction(const ui::InventoryAction& action) {
    if (m_ctx.ecs == nullptr || m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
        return;
    }
    net::Command command = MakePlayerCommand(net::CommandType::InventoryAction);
    const auto count = static_cast<std::uint16_t>(std::clamp(action.count, 1, 99));
    command.payload.flags =
        static_cast<std::uint16_t>(action.type) | static_cast<std::uint16_t>(count << 8);
    command.payload.area = static_cast<std::uint8_t>(action.from);
    command.payload.targetArea = static_cast<std::uint8_t>(action.to);
    command.payload.index = action.fromIndex;
    command.payload.targetIndex = action.toIndex;
    command.payload.entityId = action.fromEntityId;
    SubmitPlayerCommand(command);
}

bool WorldDemoState::ExecutePlayerCommand(const net::Command& command, std::string& error) {
    if (m_ctx.ecs == nullptr) {
        error = "ECS 未初始化";
        return false;
    }
    if (m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
        error = "玩家尚未生成";
        return false;
    }
    const std::uint64_t playerId = m_ctx.ecs->IdOf(m_player).id;
    if (command.playerStableId != playerId) {
        error = "playerStableId 不匹配（命令 " + std::to_string(command.playerStableId) +
                "，本地 " + std::to_string(playerId) + "）";
        return false;
    }
    if (!std::isfinite(command.payload.x) || !std::isfinite(command.payload.y)) {
        error = "命令 payload 浮点非法";
        return false;
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    switch (command.type) {
        case net::CommandType::Move: {
            auto* move = registry.try_get<ecs::MoveInput>(m_player);
            if (move == nullptr) {
                move = &registry.emplace<ecs::MoveInput>(m_player);
            }
            move->x = command.payload.x;
            move->y = command.payload.y;
            return true;
        }
        case net::CommandType::MoveTo: {
            render::GridPick pick;
            pick.hit = true;
            pick.x = static_cast<int>(std::lround(command.payload.x));
            pick.y = static_cast<int>(std::lround(command.payload.y));
            HandleClickToMove(pick);
            return true;
        }
        case net::CommandType::Attack:
            PlayerAttack(command.payload.entityId);
            return true;
        case net::CommandType::PickupNearest:
            PickupNearestItem();
            return true;
        case net::CommandType::PickupEntity:
            PickupLootItem(command.payload.entityId);
            return true;
        case net::CommandType::UseBag:
            if (command.payload.index < 0) {
                UseConsumableItem();
            } else {
                UseBagItem(command.payload.index);
            }
            return true;
        case net::CommandType::EquipBag:
            if (command.payload.index < 0) {
                ToggleEquipWeapon();
            } else {
                const int slot = command.payload.targetIndex >= 0 &&
                                         command.payload.targetIndex <
                                             ui::kInventoryEquipmentSlots
                                     ? command.payload.targetIndex
                                     : static_cast<int>(ecs::EquipSlot::PrimaryHand);
                EquipBagItem(command.payload.index, static_cast<ecs::EquipSlot>(slot));
            }
            return true;
        case net::CommandType::Unequip:
            UnequipEquipment(command.payload.index);
            return true;
        case net::CommandType::DropBag:
            if (command.payload.index < 0) {
                DropFirstItem();
            } else {
                DropBagItem(command.payload.index);
            }
            return true;
        case net::CommandType::DropEquipment:
            DropEquipmentItem(command.payload.index);
            return true;
        case net::CommandType::InventoryAction: {
            const auto actionType =
                static_cast<ui::InventoryActionType>(command.payload.flags & 0xFFu);
            const int count = static_cast<int>(command.payload.flags >> 8);
            if (count > 99 ||
                command.payload.area > static_cast<std::uint8_t>(ui::InventoryArea::Loot) ||
                command.payload.targetArea >
                    static_cast<std::uint8_t>(ui::InventoryArea::Loot) ||
                static_cast<std::uint8_t>(actionType) >
                    static_cast<std::uint8_t>(ui::InventoryActionType::QueueSetCount)) {
                error = "背包命令字段非法";
                return false;
            }
            ui::InventoryAction action;
            action.type = actionType;
            action.from = static_cast<ui::InventoryArea>(command.payload.area);
            action.to = static_cast<ui::InventoryArea>(command.payload.targetArea);
            action.fromIndex = command.payload.index;
            action.toIndex = command.payload.targetIndex;
            action.fromEntityId = command.payload.entityId;
            action.count = count > 0 ? count : 1;
            HandleInventoryAction(action);
            return true;
        }
        case net::CommandType::Gunshot:
            EmitGunshot();
            return true;
        case net::CommandType::SelectHotbar:
            if (command.payload.index < 0 || command.payload.index >= 5) {
                error = "快捷栏索引越界";
                return false;
            }
            m_hotbarSelected = command.payload.index;
            ZL_DEBUG("ui", "快捷栏选择：槽 {}", m_hotbarSelected + 1);
            return true;
    }
    error = "未知命令类型";
    return false;
}

bool WorldDemoState::StartCraftBatch(const items::RecipeDef& recipe, int requested,
                                     std::string& error, int& actual) {
    actual = 0;
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        error = "玩家或注册表不可用";
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (!RecipeKnown(recipe.id)) {
        error = "尚未学会该配方：阅读配方杂志";
        return false;
    }
    if (!recipe.station.empty() &&
        (position == nullptr ||
         !StationNearby(recipe.station, position->x, position->y, position->z))) {
        error = "已离开" + StationLabel(recipe.station);
        return false;
    }
    if (container == nullptr) {
        error = "背包不可用";
        return false;
    }
    std::string reason;
    if (!items::CanCraft(*container, *m_ctx.items, recipe, &reason)) {
        error = reason.empty() ? "材料或背包空间不足" : reason;
        return false;
    }
    const int possible =
        items::MaxCraftCount(*container, *m_ctx.items, recipe,
                             std::clamp(requested, 1, 99));
    if (possible <= 0) {
        error = "材料或背包空间不足";
        return false;
    }
    const float efficiency = items::ToolEfficiency(*container, recipe);
    const float seconds = items::EffectiveCraftSeconds(recipe, efficiency);
    m_craftBatchTotal = possible;
    m_craftBatchCompleted = 0;
    m_crafting.Start(recipe.id, seconds, efficiency);
    actual = possible;
    return true;
}

std::string WorldDemoState::QueuedCraftLabel(std::string_view recipeId) const {
    if (m_ctx.recipes == nullptr) {
        return std::string(recipeId);
    }
    const items::RecipeDef* recipe = m_ctx.recipes->Find(recipeId);
    if (recipe == nullptr) {
        return std::string(recipeId);
    }
    const items::ItemDef* output =
        m_ctx.items != nullptr ? m_ctx.items->Find(recipe->outputDefId) : nullptr;
    const std::string label =
        output != nullptr ? output->name : recipe->outputDefId;
    return label + " ×" + std::to_string(recipe->outputCount);
}

bool WorldDemoState::StartNextQueuedCraft(std::string_view prefix) {
    if (m_craftQueue.empty()) {
        return false;
    }
    ecs::CraftQueueItem entry = std::move(m_craftQueue.front());
    m_craftQueue.erase(m_craftQueue.begin());
    const auto fail = [&](const std::string& reason) {
        const int remaining = static_cast<int>(m_craftQueue.size());
        m_craftQueue.clear();
        std::string notice(prefix);
        notice += "队列中断：" + reason;
        if (remaining > 0) {
            notice += "（剩余 " + std::to_string(remaining) + " 项已清空）";
        }
        SetNotice(notice, kNoticeFailColor, 3.0f);
        ZL_INFO("items", "制作队列中断：{}（剩余 {} 项已清空）", reason, remaining);
        return false;
    };
    if (m_ctx.recipes == nullptr) {
        return fail("配方未加载");
    }
    const items::RecipeDef* recipe =
        m_ctx.recipes->Find(entry.recipeId);
    if (recipe == nullptr) {
        return fail("配方不存在");
    }
    std::string error;
    int actual = 0;
    if (!StartCraftBatch(*recipe, entry.count, error, actual)) {
        return fail(error);
    }
    const items::ItemDef* output = m_ctx.items->Find(recipe->outputDefId);
    const std::string label =
        output != nullptr ? output->name : recipe->outputDefId;
    std::string notice(prefix);
    notice += "开始队列下一项：" + label + " ×" +
              std::to_string(recipe->outputCount) + "（" +
              std::to_string(actual) + " 件";
    if (actual < entry.count) {
        notice += "；请求 " + std::to_string(entry.count) +
                  " 件，已按材料/空间截断";
    }
    notice += "）";
    SetNotice(notice, kNoticeInfoColor, 2.5f);
    ZL_INFO("items",
            "开始队列下一项：{} ×{}（配方 {}，请求 {} 件，实际 {} 件，剩余 {} 项）",
            label, recipe->outputCount, recipe->id, entry.count, actual,
            static_cast<int>(m_craftQueue.size()));
    return true;
}

void WorldDemoState::SyncCraftingJobToEcs() {
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* job = registry.try_get<ecs::CraftingJob>(m_player);
    if (job == nullptr) {
        job = &registry.emplace<ecs::CraftingJob>(m_player);
    }
    job->active = m_crafting.active;
    job->recipeId = m_crafting.active ? m_crafting.recipeId : std::string();
    job->elapsed = m_crafting.active ? m_crafting.elapsed : 0.0f;
    job->duration = m_crafting.active ? m_crafting.duration : 0.0f;
    job->toolEfficiency = m_crafting.active ? m_crafting.toolEfficiency : 1.0f;
    job->batchTotal = m_crafting.active ? m_craftBatchTotal : 0;
    job->batchCompleted = m_crafting.active ? m_craftBatchCompleted : 0;
    job->queue = m_craftQueue;
}

void WorldDemoState::RestoreCraftingJobFromEcs() {
    m_crafting.Cancel();
    m_craftBatchTotal = 0;
    m_craftBatchCompleted = 0;
    m_craftQueue.clear();
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        return;
    }
    const auto* job =
        m_ctx.ecs->Registry().try_get<ecs::CraftingJob>(m_player);
    if (job == nullptr || (!job->active && job->queue.empty())) {
        return;
    }
    const auto appendWarning = [this](std::string warning) {
        if (m_ctx.saveWarning.empty()) {
            m_ctx.saveWarning = std::move(warning);
        } else {
            m_ctx.saveWarning += "；" + warning;
        }
    };
    const auto cancelRestore = [&](const std::string& reason) {
        m_craftQueue.clear();
        appendWarning("存档制作任务未恢复：" + reason);
        ZL_WARN("items", "读档制作任务未恢复：{}", reason);
    };
    if (!job->active) {
        cancelRestore("存档队列缺少活跃批次");
        return;
    }
    if (m_ctx.recipes == nullptr) {
        cancelRestore("配方未加载");
        return;
    }
    const items::RecipeDef* recipe = m_ctx.recipes->Find(job->recipeId);
    if (recipe == nullptr) {
        cancelRestore("配方不存在：" + job->recipeId);
        return;
    }
    if (!RecipeKnown(recipe->id)) {
        cancelRestore("尚未学会配方：" + recipe->id);
        return;
    }
    if (job->batchTotal < 1 || job->batchTotal > ecs::kMaxCraftBatchCount ||
        job->batchCompleted < 0 || job->batchCompleted >= job->batchTotal ||
        !(job->duration > 0.0f) || job->elapsed < 0.0f ||
        job->elapsed > job->duration + 1e-4f || !(job->toolEfficiency > 0.0f) ||
        job->toolEfficiency > 1.0f) {
        cancelRestore("批次进度非法");
        return;
    }

    int dropped = 0;
    for (const ecs::CraftQueueItem& entry : job->queue) {
        if (entry.count < 1 || entry.count > ecs::kMaxCraftBatchCount ||
            m_ctx.recipes->Find(entry.recipeId) == nullptr) {
            ++dropped;
            continue;
        }
        m_craftQueue.push_back(entry);
    }
    if (dropped > 0) {
        appendWarning("存档队列有 " + std::to_string(dropped) +
                      " 项配方不存在，已跳过");
    }

    m_crafting.active = true;
    m_crafting.recipeId = job->recipeId;
    m_crafting.elapsed = job->elapsed;
    m_crafting.duration = job->duration;
    m_crafting.toolEfficiency = job->toolEfficiency;
    m_craftBatchTotal = job->batchTotal;
    m_craftBatchCompleted = job->batchCompleted;
    const std::string label = QueuedCraftLabel(recipe->id);
    SetNotice("已恢复制作：" + label + "（" +
                  std::to_string(m_craftBatchCompleted) + "/" +
                  std::to_string(m_craftBatchTotal) + "，队列 " +
                  std::to_string(m_craftQueue.size()) + " 项）",
              kNoticeInfoColor, 2.5f);
    ZL_INFO("items",
            "读档恢复制作：{}（配方 {}，进度 {}/{}, 已完成 {}/{}, 队列 {} 项）",
            label, recipe->id, m_crafting.elapsed, m_crafting.duration,
            m_craftBatchCompleted, m_craftBatchTotal,
            static_cast<int>(m_craftQueue.size()));
}

void WorldDemoState::HandleInventoryAction(const ui::InventoryAction& action) {
    if (m_ctx.ui == nullptr || m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (container == nullptr || equipment == nullptr) {
        return;
    }

    switch (action.type) {
        case ui::InventoryActionType::Transfer:
            if (action.from == ui::InventoryArea::Backpack &&
                action.to == ui::InventoryArea::Backpack) {
                if (action.fromIndex >= 0 && action.toIndex >= 0) {
                    items::MoveStack(*container, *m_ctx.items,
                                     static_cast<std::size_t>(action.fromIndex),
                                     static_cast<std::size_t>(action.toIndex));
                }
            } else if (action.from == ui::InventoryArea::Backpack &&
                       action.to == ui::InventoryArea::Equipment) {
                if (action.toIndex >= 0 && action.toIndex < ui::kInventoryEquipmentSlots) {
                    EquipBagItem(action.fromIndex,
                                 static_cast<ecs::EquipSlot>(action.toIndex));
                }
            } else if (action.from == ui::InventoryArea::Backpack &&
                       action.to == ui::InventoryArea::Loot) {
                DropBagItem(action.fromIndex);
            } else if (action.from == ui::InventoryArea::Equipment &&
                       action.to == ui::InventoryArea::Backpack) {
                UnequipEquipment(action.fromIndex);
            } else if (action.from == ui::InventoryArea::Equipment &&
                       action.to == ui::InventoryArea::Loot) {
                DropEquipmentItem(action.fromIndex);
            } else if (action.from == ui::InventoryArea::Loot &&
                       action.to == ui::InventoryArea::Backpack) {
                PickupLootItem(action.fromEntityId);
            } else if (action.from == ui::InventoryArea::Loot &&
                       action.to == ui::InventoryArea::Equipment) {
                if (action.toIndex >= 0 && action.toIndex < ui::kInventoryEquipmentSlots) {
                    EquipLootItem(action.fromEntityId,
                                  static_cast<ecs::EquipSlot>(action.toIndex));
                }
            }
            break;
        case ui::InventoryActionType::Use:
            UseBagItem(action.fromIndex);
            break;
        case ui::InventoryActionType::Equip: {
            if (action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= container->items.size()) {
                break;
            }
            const ecs::ItemInstance& entry =
                container->items[static_cast<std::size_t>(action.fromIndex)];
            const items::ItemDef* def = m_ctx.items->Find(entry.defId);
            if (def == nullptr) {
                break;
            }
            if (def->type == items::ItemType::Weapon) {
                EquipBagItem(action.fromIndex, ecs::EquipSlot::PrimaryHand);
            } else if (def->type == items::ItemType::Clothing) {
                EquipBagItem(action.fromIndex, ecs::EquipSlot::Body);
            } else {
                SetNotice("该物品不能装备", kNoticeFailColor, 1.5f);
            }
            break;
        }
        case ui::InventoryActionType::Unequip:
            UnequipEquipment(action.fromIndex);
            break;
        case ui::InventoryActionType::Drop:
            if (action.from == ui::InventoryArea::Backpack) {
                DropBagItem(action.fromIndex);
            } else if (action.from == ui::InventoryArea::Equipment) {
                DropEquipmentItem(action.fromIndex);
            }
            break;
        case ui::InventoryActionType::Repair:
            if (action.from != ui::InventoryArea::Backpack) {
                SetNotice("只能维修背包中的工具", kNoticeFailColor, 1.8f);
                break;
            }
            RepairBagItem(action.fromIndex);
            break;
        case ui::InventoryActionType::Pickup:
            PickupLootItem(action.fromEntityId);
            break;
        case ui::InventoryActionType::Craft: {
            if (m_ctx.recipes == nullptr) {
                SetNotice("制作不可用：配方未加载", kNoticeFailColor, 2.5f);
                break;
            }
            const items::RecipeDef* recipe =
                action.fromIndex >= 0
                    ? m_ctx.recipes->At(static_cast<std::size_t>(action.fromIndex))
                    : nullptr;
            if (recipe == nullptr) {
                SetNotice("配方不存在", kNoticeFailColor, 2.0f);
                break;
            }
            if (!RecipeKnown(recipe->id)) {
                SetNotice("尚未学会该配方：阅读配方杂志", kNoticeFailColor, 2.5f);
                ZL_INFO("items", "制作被拒绝（配方 {}）：尚未学会", recipe->id);
                break;
            }
            if (!recipe->station.empty() &&
                (position == nullptr ||
                 !StationNearby(recipe->station, position->x, position->y,
                                position->z))) {
                SetNotice("需要附近" + StationLabel(recipe->station),
                          kNoticeFailColor, 2.5f);
                ZL_INFO("items", "制作被拒绝（配方 {}）：需要附近{}", recipe->id,
                        StationLabel(recipe->station));
                break;
            }
            const int requested = std::clamp(action.count, 1, 99);
            const items::ItemDef* output = m_ctx.items->Find(recipe->outputDefId);
            const std::string label =
                output != nullptr ? output->name : recipe->outputDefId;
            if (m_crafting.active || !m_craftQueue.empty()) {
                if (static_cast<int>(m_craftQueue.size()) >= ecs::kMaxCraftQueueEntries) {
                    SetNotice("制作队列已满（最多 " +
                                  std::to_string(ecs::kMaxCraftQueueEntries) + " 项）",
                              kNoticeFailColor, 2.5f);
                    break;
                }
                m_craftQueue.push_back(ecs::CraftQueueItem{recipe->id, requested});
                SetNotice("已加入制作队列（第 " +
                              std::to_string(m_craftQueue.size()) + " 项）：" +
                              label + " ×" +
                              std::to_string(recipe->outputCount) + "（请求 " +
                              std::to_string(requested) + " 件）",
                          kNoticeInfoColor, 2.5f);
                ZL_INFO("items",
                        "加入制作队列：{} ×{}（配方 {}，请求 {} 件，队列 {} 项）",
                        label, recipe->outputCount, recipe->id, requested,
                        static_cast<int>(m_craftQueue.size()));
                if (!m_crafting.active) {
                    StartNextQueuedCraft({});
                }
                break;
            }
            std::string error;
            int actual = 0;
            if (!StartCraftBatch(*recipe, requested, error, actual)) {
                SetNotice(error, kNoticeFailColor, 2.5f);
                ZL_INFO("items", "制作被拒绝（配方 {}）：{}", recipe->id, error);
                break;
            }
            if (requested > 1) {
                std::string notice =
                    (actual > 1 ? "开始批量制作：" : "开始制作：") + label + " ×" +
                    std::to_string(recipe->outputCount) + "（共 " +
                    std::to_string(actual) + " 件";
                if (actual < requested) {
                    notice += "；请求 " + std::to_string(requested) +
                              " 件，已按材料/空间截断";
                }
                notice += "）";
                SetNotice(notice, kNoticeInfoColor, 2.5f);
            } else {
                SetNotice("开始制作：" + label + " ×" +
                              std::to_string(recipe->outputCount),
                          kNoticeInfoColor, 1.5f);
            }
            ZL_INFO("items",
                    "开始制作：{} ×{}（配方 {}，请求 {} 件，实际 {} 件，耗时 {:.2f}s，"
                    "工具效率 {:.0f}%）",
                    label, recipe->outputCount, recipe->id, requested, actual,
                    m_crafting.duration, m_crafting.toolEfficiency * 100.0f);
            break;
        }
        case ui::InventoryActionType::CancelCraft: {
            if (!m_crafting.active && m_craftQueue.empty()) {
                SetNotice("没有正在制作的项目", kNoticeFailColor, 1.5f);
                break;
            }
            const std::string recipeId = m_crafting.recipeId;
            const int completed = m_craftBatchCompleted;
            const int total = m_craftBatchTotal;
            const int queued = static_cast<int>(m_craftQueue.size());
            m_crafting.Cancel();
            m_craftBatchTotal = 0;
            m_craftBatchCompleted = 0;
            m_craftQueue.clear();
            if (queued > 0) {
                if (completed > 0) {
                    SetNotice("已取消制作队列（当前已完成 " +
                                  std::to_string(completed) + "/" +
                                  std::to_string(total) + "，队列 " +
                                  std::to_string(queued) + " 项已清空）",
                              kNoticeInfoColor, 2.8f);
                } else {
                    SetNotice("已取消制作队列（队列 " + std::to_string(queued) +
                                  " 项已清空，材料未消耗）",
                              kNoticeInfoColor, 2.5f);
                }
            } else if (completed > 0) {
                SetNotice("已取消批量制作（已完成 " + std::to_string(completed) +
                              "/" + std::to_string(total) + "，当前材料未消耗）",
                          kNoticeInfoColor, 2.5f);
            } else {
                SetNotice("已取消制作（材料未消耗）", kNoticeInfoColor, 2.0f);
            }
            ZL_INFO("items",
                    "取消制作（配方 {}，已完成 {}/{}，清空队列 {} 项）", recipeId,
                    completed, total, queued);
            break;
        }
        case ui::InventoryActionType::QueueMoveUp:
        case ui::InventoryActionType::QueueMoveDown: {
            if (action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= m_craftQueue.size()) {
                SetNotice("队列项不存在", kNoticeFailColor, 1.5f);
                break;
            }
            const bool moveUp = action.type == ui::InventoryActionType::QueueMoveUp;
            const int delta = moveUp ? -1 : 1;
            const int target = action.fromIndex + delta;
            if (target < 0) {
                SetNotice("队列项已在队首", kNoticeFailColor, 1.5f);
                break;
            }
            if (target >= static_cast<int>(m_craftQueue.size())) {
                SetNotice("队列项已在队尾", kNoticeFailColor, 1.5f);
                break;
            }
            const std::string label =
                QueuedCraftLabel(m_craftQueue[static_cast<std::size_t>(action.fromIndex)]
                                     .recipeId);
            std::swap(m_craftQueue[static_cast<std::size_t>(action.fromIndex)],
                      m_craftQueue[static_cast<std::size_t>(target)]);
            const std::string direction = moveUp ? "上移" : "下移";
            SetNotice("已" + direction + "队列第 " +
                          std::to_string(action.fromIndex + 1) + " 项：" + label +
                          "（现为第 " + std::to_string(target + 1) + " 项）",
                      kNoticeInfoColor, 2.0f);
            ZL_INFO("items", "制作队列{}：{} 第 {} 项 -> 第 {} 项（队列 {} 项）",
                    direction, label, action.fromIndex + 1, target + 1,
                    static_cast<int>(m_craftQueue.size()));
            break;
        }
        case ui::InventoryActionType::QueueRemove: {
            if (action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= m_craftQueue.size()) {
                SetNotice("队列项不存在", kNoticeFailColor, 1.5f);
                break;
            }
            const ecs::CraftQueueItem removed =
                m_craftQueue[static_cast<std::size_t>(action.fromIndex)];
            const std::string label = QueuedCraftLabel(removed.recipeId);
            m_craftQueue.erase(m_craftQueue.begin() + action.fromIndex);
            SetNotice("已移除队列第 " + std::to_string(action.fromIndex + 1) +
                          " 项：" + label + "（请求 " +
                          std::to_string(removed.count) + " 件）",
                      kNoticeInfoColor, 2.0f);
            ZL_INFO("items", "移除制作队列第 {} 项：{}（请求 {} 件，剩余 {} 项）",
                    action.fromIndex + 1, label, removed.count,
                    static_cast<int>(m_craftQueue.size()));
            break;
        }
        case ui::InventoryActionType::QueueSetCount: {
            if (action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= m_craftQueue.size()) {
                SetNotice("队列项不存在", kNoticeFailColor, 1.5f);
                break;
            }
            ecs::CraftQueueItem& entry =
                m_craftQueue[static_cast<std::size_t>(action.fromIndex)];
            const int count = std::clamp(action.count, 1, 99);
            if (entry.count == count) {
                break;
            }
            const int previous = entry.count;
            entry.count = count;
            const std::string label = QueuedCraftLabel(entry.recipeId);
            SetNotice("队列第 " + std::to_string(action.fromIndex + 1) +
                          " 项数量：" + std::to_string(previous) + " → " +
                          std::to_string(count) + " 件（" + label + "）",
                      kNoticeInfoColor, 2.0f);
            ZL_INFO("items",
                    "调整制作队列第 {} 项数量：{} 请求 {} -> {} 件（队列 {} 项）",
                    action.fromIndex + 1, label, previous, count,
                    static_cast<int>(m_craftQueue.size()));
            break;
        }
    }
    RefreshInventory();
}

void WorldDemoState::UpdateCrafting(float tickDt) {
    if (!m_crafting.active || !m_crafting.Advance(tickDt)) {
        return;
    }
    const std::string recipeId = m_crafting.recipeId;
    m_crafting.Cancel();
    const auto finishFailure = [this](const std::string& reason) {
        const int completed = m_craftBatchCompleted;
        const int total = m_craftBatchTotal;
        const int queued = static_cast<int>(m_craftQueue.size());
        m_craftBatchTotal = 0;
        m_craftBatchCompleted = 0;
        m_craftQueue.clear();
        const std::string queueSuffix =
            queued > 0 ? "；队列 " + std::to_string(queued) + " 项已清空"
                       : std::string();
        if (completed > 0) {
            SetNotice("批量制作中断：已完成 " + std::to_string(completed) + "/" +
                          std::to_string(total) + "（" + reason + "）" +
                          queueSuffix,
                      kNoticeFailColor, 3.0f);
            ZL_INFO("items",
                    "批量制作中断（已完成 {}/{}，清空队列 {} 项）：{}", completed,
                    total, queued, reason);
        } else {
            SetNotice("制作失败：" + reason + queueSuffix, kNoticeFailColor, 2.5f);
            ZL_INFO("items", "制作失败（清空队列 {} 项）：{}", queued, reason);
        }
    };
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_ctx.recipes == nullptr ||
        m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
        const int queued = static_cast<int>(m_craftQueue.size());
        m_craftBatchTotal = 0;
        m_craftBatchCompleted = 0;
        m_craftQueue.clear();
        ZL_WARN("items", "制作中断：玩家或注册表不可用（配方 {}，清空队列 {} 项）",
                recipeId, queued);
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    const items::RecipeDef* recipe = m_ctx.recipes->Find(recipeId);
    if (container == nullptr || recipe == nullptr) {
        finishFailure("配方或背包不可用");
        return;
    }
    if (!RecipeKnown(recipe->id)) {
        finishFailure("尚未学会配方");
        return;
    }
    if (!recipe->station.empty() &&
        (position == nullptr ||
         !StationNearby(recipe->station, position->x, position->y, position->z))) {
        finishFailure("已离开" + StationLabel(recipe->station));
        return;
    }
    const items::CraftResult result = items::Craft(*container, *m_ctx.items, *recipe);
    if (!result.ok) {
        finishFailure(result.error);
        return;
    }

    const int total = std::max(m_craftBatchTotal, 1);
    const int completed = ++m_craftBatchCompleted;
    const items::ItemDef* output = m_ctx.items->Find(result.outputDefId);
    const std::string label = output != nullptr ? output->name : result.outputDefId;
    std::string brokenTools;
    for (const std::string& defId : result.brokenToolDefIds) {
        const items::ItemDef* def = m_ctx.items->Find(defId);
        if (!brokenTools.empty()) {
            brokenTools += "、";
        }
        brokenTools += def != nullptr ? def->name : defId;
    }
    const std::string brokenSuffix =
        brokenTools.empty() ? std::string() : "；" + brokenTools + "已损坏";
    if (completed < total) {
        std::string reason;
        if (!RecipeKnown(recipe->id)) {
            reason = "尚未学会配方";
        } else if (!recipe->station.empty() &&
                   (position == nullptr ||
                    !StationNearby(recipe->station, position->x, position->y,
                                   position->z))) {
            reason = "已离开" + StationLabel(recipe->station);
        } else if (!items::CanCraft(*container, *m_ctx.items, *recipe, &reason)) {
            // reason 已由 CanCraft 填充
        }
        if (!reason.empty()) {
            finishFailure(reason);
            return;
        }
        const float efficiency = items::ToolEfficiency(*container, *recipe);
        const float seconds = items::EffectiveCraftSeconds(*recipe, efficiency);
        m_crafting.Start(recipe->id, seconds, efficiency);
        SetNotice("制作中：" + label + "（" + std::to_string(completed + 1) + "/" +
                      std::to_string(total) + "）" + brokenSuffix,
                  kNoticeInfoColor, 1.5f);
        ZL_INFO("items", "批量制作继续：{}（已完成 {}/{}，耗时 {:.2f}s，效率 {:.0f}%）",
                label, completed, total, seconds, efficiency * 100.0f);
        return;
    }

    m_craftBatchTotal = 0;
    m_craftBatchCompleted = 0;
    const std::string completion =
        total > 1
            ? "批量制作完成：" + label + " ×" +
                  std::to_string(result.outputCount * total) + "（共 " +
                  std::to_string(total) + " 件）" + brokenSuffix
            : "制作成功：" + label + " ×" +
                  std::to_string(result.outputCount) + brokenSuffix;
    if (brokenTools.empty()) {
        ZL_INFO("items", "制作成功：{} ×{}（配方 {}，批次 {}/{}）", label,
                result.outputCount, recipe->id, completed, total);
    } else {
        ZL_INFO("items", "制作成功：{} ×{}（配方 {}，批次 {}/{}）；工具损坏：{}",
                label, result.outputCount, recipe->id, completed, total, brokenTools);
    }
    if (!m_craftQueue.empty()) {
        StartNextQueuedCraft(completion + "；");
        return;
    }
    SetNotice(completion, kNoticeOkColor, total > 1 ? 3.0f : 2.5f);
}

void WorldDemoState::HandleClickToMove(const render::GridPick& pick) {
    if (m_ctx.ecs == nullptr || m_player == entt::null || !pick.hit) {
        return;  // 玩家未生成 / 拾取无效
    }
    if (m_networkMode) {
        net::Command command = MakePlayerCommand(net::CommandType::MoveTo);
        command.payload.x = static_cast<float>(pick.x);
        command.payload.y = static_cast<float>(pick.y);
        if (SubmitPlayerCommand(command)) {
            m_markerX = pick.x;
            m_markerY = pick.y;
            m_markerTimer = 3.0f;
            m_markerColor = kMarkerActiveColor;
            SetNotice("pathfinding on server...", kNoticeInfoColor, 1.2f);
        }
        return;
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
            if (m_ctx.events != nullptr) {  // M7-T4：OnPlayerMove（点击移动到达路点；排队派发）
                const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
                const float px = position != nullptr ? position->x : 0.0f;
                const float py = position != nullptr ? position->y : 0.0f;
                const int pz = position != nullptr ? position->z : 0;
                m_ctx.events->EmitQueued(
                    core::kOnPlayerMove,
                    core::PlayerEvent{m_ctx.ecs->IdOf(m_player).id, px, py, pz});
            }
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
    if (m_ctx.simulation != nullptr) {
        m_ctx.simulation->SpawnDemoPopulation();
    }
}
// M5-T2 临时"感知"桩已删除：M5-T3 起由 ai::PerceptionSystem（视锥 + 视线 + 听觉）接管。

void WorldDemoState::KillNearestDemoEntity() {
    if (m_networkMode || m_ctx.ecs == nullptr) {
        return;  // 网络客户端不能直接修改权威世界
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
    // M5-T4/M11-T4：只发 OnSound；感知系统与音频系统同时消费（枪声可闻 + 引怪）。
    if (m_ctx.audio != nullptr) {
        m_ctx.audio->EmitWorldSound(shot, audio::kGunshot);
    } else {
        shot.clip = audio::kGunshot;
        m_ctx.events->EmitQueued(core::kOnSound, shot);
    }
    SetNotice("gunshot! r=" + std::to_string(static_cast<int>(m_gunshotRadius)) +
                  " (walls attenuate)",
              kNoticeInfoColor, 2.0f);
    ZL_INFO("ai", "枪声（F7）：声源 ({:.1f}, {:.1f}) 基础半径 {:.0f} 格（隔墙逐格衰减）", shot.x,
            shot.y, shot.radius);
}

void WorldDemoState::PlayerAttack(std::uint64_t targetStableId) {
    if (m_ctx.ecs == nullptr || m_ctx.combat == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* attack = registry.try_get<ecs::AttackIntent>(m_player);
    const auto* playerPos = registry.try_get<ecs::Position>(m_player);
    if (attack == nullptr || playerPos == nullptr) {
        return;
    }

    // M12-T1：命令可携带目标 StableId；缺省仍自动瞄准最近僵尸。
    constexpr float kAutoAimRange = 3.0f;
    ecs::Entity best = entt::null;
    float bestDistSq = kAutoAimRange * kAutoAimRange;
    if (targetStableId != 0) {
        best = m_ctx.ecs->FindById(targetStableId);
        const auto* targetPos = best != entt::null
                                    ? registry.try_get<ecs::Position>(best)
                                    : nullptr;
        const auto* health = best != entt::null ? registry.try_get<ecs::Health>(best)
                                                : nullptr;
        if (best == entt::null || best == m_player ||
            !registry.all_of<ecs::AiState>(best) || targetPos == nullptr ||
            health == nullptr || health->current <= 0.0f) {
            SetNotice("attack: target unavailable", kNoticeFailColor, 1.5f);
            return;
        }
        const float dx = targetPos->x - playerPos->x;
        const float dy = targetPos->y - playerPos->y;
        bestDistSq = dx * dx + dy * dy;
        if (bestDistSq > kAutoAimRange * kAutoAimRange) {
            SetNotice("attack: target out of range", kNoticeFailColor, 1.5f);
            return;
        }
    } else {
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
    }
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

void WorldDemoState::EnsureDemoItemsScattered() {
    if (m_networkMode || m_demoItemsScattered || m_ctx.ecs == nullptr ||
        m_ctx.items == nullptr) {
        return;
    }
    // 与玩家生成相同的门控：目标 chunk 就绪后再散落（走位图/格子数据可靠）
    const world::ChunkCoord targetChunk = world::ChunkOf(70, 24);
    const world::Chunk* target = FindChunk(targetChunk.cx, targetChunk.cy);
    if (target == nullptr || target->State() != world::ChunkState::Loaded) {
        return;  // 下帧再试
    }
    if (m_ctx.world == nullptr) {
        return;
    }
    const int spawned = game::SpawnDemoGroundItems(
        *m_ctx.ecs, *m_ctx.items, *m_ctx.world, 70.0f, 24.0f);
    m_demoItemsScattered = true;
    ZL_INFO("items", "地面物品散落：{} 处（村庄附近；F 拾取最近）", spawned);
}

void WorldDemoState::PickupNearestItem() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* pos = registry.try_get<ecs::Position>(m_player);
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (pos == nullptr || container == nullptr) {
        return;
    }
    const game::PickupResult result = game::PickupNearestItem(
        *m_ctx.ecs, *m_ctx.items, *container, pos->x, pos->y, pos->z, m_pickupRadius);
    if (!result.found) {
        SetNotice("Nothing to pick up", kNoticeInfoColor, 1.2f);
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(result.defId);
    const std::string name = def != nullptr ? def->name : result.defId;
    if (result.picked > 0) {
        if (result.groundRemaining > 0) {
            SetNotice("拾取了 " + name + " ×" + std::to_string(result.picked) + "（地上剩 " +
                          std::to_string(result.groundRemaining) + "）",
                      kNoticeOkColor, 2.0f);
        } else {
            SetNotice("拾取了 " + name + " ×" + std::to_string(result.picked),
                      kNoticeOkColor, 2.0f);
        }
        ZL_INFO("items", "拾取：{} x{}（地面剩 {} 件；背包 {} 件）", name, result.picked,
                result.groundRemaining, items::CountOf(*container, result.defId));
        if (m_ctx.events != nullptr) {  // M7-T4：OnItemPickup（排队派发）
            m_ctx.events->EmitQueued(
                core::kOnItemPickup,
                core::ItemEvent{result.entityId, result.defId, result.picked});
        }
    } else {
        SetNotice("Bag full (" + result.defId + ")", kNoticeFailColor, 1.5f);
        ZL_INFO("items", "拾取失败：{}（背包已满）", name);
    }
}

void WorldDemoState::UseConsumableItem() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    auto* health = registry.try_get<ecs::Health>(m_player);
    if (container == nullptr || health == nullptr) {
        return;
    }

    const items::UseResult use = items::UseFirstConsumable(*container, *m_ctx.items);
    if (!use.applied) {
        SetNotice("No usable item", kNoticeInfoColor, 1.2f);
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(use.defId);
    const std::string name = def != nullptr ? def->name : use.defId;

    // 效果落地（纯函数：HP 钳到上限，返回实际回血；需求钳到 [0,1]）
    auto* needs = registry.try_get<ecs::Needs>(m_player);
    const float healed = items::ApplyUseEffect(
        use.effect, health->max, &health->current,
        needs != nullptr ? &needs->hunger : nullptr,
        needs != nullptr ? &needs->thirst : nullptr,
        needs != nullptr ? &needs->fatigue : nullptr);
    if (use.effect.heal > 0.0f) {
        SetNotice("Used " + use.defId + " (+" + std::to_string(static_cast<int>(healed)) +
                      "HP)",
                  kNoticeOkColor, 1.5f);
    } else {
        SetNotice("Used " + use.defId, kNoticeOkColor, 1.5f);
    }
    ZL_INFO("items", "使用：{}（回血 {:.0f}，HP {:.0f}/{:.0f}；剩余 {} 件）", name, healed,
            health->current, health->max, use.remaining);
}

void WorldDemoState::ToggleEquipWeapon() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    auto* container = registry.try_get<ecs::Container>(m_player);
    auto* pos = registry.try_get<ecs::Position>(m_player);
    if (equipment == nullptr || container == nullptr || pos == nullptr) {
        return;
    }
    constexpr std::size_t kPrimary = static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand);
    const items::ItemRegistry& reg = *m_ctx.items;

    // 已装备 → 卸下（回背包；放不下则落地面）
    if (equipment->slots[kPrimary].has_value()) {
        ecs::ItemInstance out;
        items::Unequip(*equipment, ecs::EquipSlot::PrimaryHand, &out);
        const items::ItemDef* def = reg.Find(out.defId);
        const std::string name = def != nullptr ? def->name : out.defId;
        if (items::AddItem(*container, reg, out) > 0) {
            SetNotice("Unequipped " + out.defId, kNoticeInfoColor, 1.5f);
            ZL_INFO("items", "卸下：{}（回背包）", name);
        } else {
            game::SpawnGroundItem(*m_ctx.ecs, reg, out, pos->x, pos->y, pos->z);
            SetNotice("Unequipped " + out.defId + " (dropped)", kNoticeInfoColor, 1.5f);
            ZL_INFO("items", "卸下：{}（背包放不下 → 落地面）", name);
        }
        return;
    }

    // 空手 → 背包第一件武器上手；被换下的武器回背包（放不下落地面）
    for (std::size_t i = 0; i < container->items.size(); ++i) {
        const ecs::ItemInstance entry = container->items[i];  // 拷贝（RemoveItem 会失效引用）
        const items::ItemDef* def = reg.Find(entry.defId);
        if (def == nullptr || def->type != items::ItemType::Weapon) {
            continue;
        }
        ecs::ItemInstance swapped;
        if (!items::Equip(*equipment, reg, ecs::EquipSlot::PrimaryHand, entry, &swapped)) {
            continue;
        }
        items::RemoveItem(*container, entry.defId, 1);
        if (swapped.Valid()) {
            if (items::AddItem(*container, reg, swapped) <= 0) {
                game::SpawnGroundItem(*m_ctx.ecs, reg, swapped, pos->x, pos->y, pos->z);
            }
        }
        SetNotice("Equipped " + entry.defId, kNoticeOkColor, 1.5f);
        ZL_INFO("items", "装备：{} → 主手（背包剩 {} 件）", def->name,
                items::CountOf(*container, entry.defId));
        return;
    }
    SetNotice("No weapon in bag", kNoticeFailColor, 1.2f);
}

void WorldDemoState::DropFirstItem() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    auto* pos = registry.try_get<ecs::Position>(m_player);
    if (container == nullptr || pos == nullptr) {
        return;
    }
    if (container->items.empty()) {
        SetNotice("Bag empty", kNoticeInfoColor, 1.2f);
        return;
    }
    ecs::ItemInstance stack = container->items.front();
    const int removed = items::RemoveItem(*container, stack.defId, stack.count);
    if (removed <= 0) {
        return;
    }
    stack.count = removed;
    const ecs::Entity ground =
        game::SpawnGroundItem(*m_ctx.ecs, *m_ctx.items, stack, pos->x, pos->y, pos->z);
    if (m_ctx.events != nullptr && ground != entt::null) {  // M7-T4：OnItemDrop（排队派发）
        m_ctx.events->EmitQueued(core::kOnItemDrop,
                                 core::ItemEvent{m_ctx.ecs->IdOf(ground).id, stack.defId, removed});
    }
    const items::ItemDef* def = m_ctx.items->Find(stack.defId);
    SetNotice("Dropped " + stack.defId + " x" + std::to_string(removed), kNoticeInfoColor,
              1.5f);
    ZL_INFO("items", "丢弃：{} x{}（地面物品 {} 件）", def != nullptr ? def->name : stack.defId,
            removed, game::CountGroundItems(*m_ctx.ecs));
}

void WorldDemoState::AppendActorPresentation() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_ctx.assets == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const std::vector<AssetProvider::Entry>* pixelFrames =
        m_ctx.assets->FramesByHash(ecs::HashName("ui.pixel"));
    if (pixelFrames == nullptr || pixelFrames->empty() ||
        (*pixelFrames)[0].atlas == nullptr || (*pixelFrames)[0].frame == nullptr) {
        return;
    }
    const AssetProvider::Entry& pixel = (*pixelFrames)[0];

    auto pushIcon = [&](const ecs::Position& pos, std::uint64_t stableId,
                        std::string_view icon, int subLayer, float offsetX,
                        float offsetY, float scale, std::uint32_t tint) {
        const items::ItemDef* def = m_ctx.items->Find(icon);
        if (def == nullptr) {
            return;
        }
        const auto* frames = m_ctx.assets->FramesByHash(ecs::HashName(def->icon));
        if (frames == nullptr || frames->empty() || frames->front().atlas == nullptr ||
            frames->front().frame == nullptr) {
            return;
        }
        const AssetProvider::Entry& entry = frames->front();
        render::DrawItem item;
        RenderCollectSystem::FillEntityDrawItem(
            pos.x, pos.y, pos.z, tint, stableId, *entry.frame,
            entry.atlas->Texture(), subLayer, &item);
        const float centerX = item.rect.x + item.rect.w * 0.5f;
        const float centerY = item.rect.y + item.rect.h * 0.5f;
        item.rect.w *= scale;
        item.rect.h *= scale;
        item.rect.x = centerX + offsetX - item.rect.w * 0.5f;
        item.rect.y = centerY + offsetY - item.rect.h * 0.5f;
        m_items.push_back(item);
    };

    auto pushPixel = [&](const ecs::Position& pos, std::uint64_t stableId,
                         float offsetX, float offsetY, float width, float height,
                         std::uint32_t tint, int subLayer) {
        render::DrawItem item;
        RenderCollectSystem::FillEntityDrawItem(
            pos.x, pos.y, pos.z, tint, stableId, *pixel.frame,
            pixel.atlas->Texture(), subLayer, &item);
        const float centerX = item.rect.x + item.rect.w * 0.5f;
        const float centerY = item.rect.y + item.rect.h * 0.5f;
        item.rect.w = width;
        item.rect.h = height;
        item.rect.x = centerX + offsetX - width * 0.5f;
        item.rect.y = centerY + offsetY - height * 0.5f;
        m_items.push_back(item);
    };

    registry.view<ecs::Position, ecs::SpriteRef>().each(
        [&](const ecs::Entity entity, const ecs::Position& pos,
            const ecs::SpriteRef&) {
            const std::uint64_t stableId = m_ctx.ecs->IdOf(entity).id;
            if (const auto* equipment = registry.try_get<ecs::Equipment>(entity);
                equipment != nullptr) {
                const auto& primary =
                    equipment->slots[static_cast<std::size_t>(
                        ecs::EquipSlot::PrimaryHand)];
                if (primary.has_value()) {
                    pushIcon(pos, stableId, primary->defId, 1, 16.0f, -36.0f,
                             1.0f, 0xFFFFFFFFu);
                }
                const auto& body =
                    equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::Body)];
                if (body.has_value()) {
                    pushIcon(pos, stableId, body->defId, 0, 0.0f, -18.0f, 0.72f,
                             0xD8FFFFFFu);
                }
            }

            const auto* action = registry.try_get<ecs::ActionState>(entity);
            const auto* facing = registry.try_get<ecs::Facing>(entity);
            if (action == nullptr || facing == nullptr) {
                return;
            }
            if (action->action == ecs::ActorAction::Attacking) {
                float offsetX = 24.0f;
                float offsetY = -34.0f;
                float width = 34.0f;
                float height = 8.0f;
                switch (facing->dir) {
                    case ecs::Direction::Down:
                        offsetX = 20.0f;
                        offsetY = -26.0f;
                        break;
                    case ecs::Direction::Up:
                        offsetX = -20.0f;
                        offsetY = -46.0f;
                        break;
                    case ecs::Direction::Left:
                        offsetX = -26.0f;
                        offsetY = -34.0f;
                        break;
                    case ecs::Direction::Right:
                        offsetX = 26.0f;
                        offsetY = -34.0f;
                        width = 8.0f;
                        height = 34.0f;
                        break;
                }
                if (facing->dir == ecs::Direction::Up ||
                    facing->dir == ecs::Direction::Down) {
                    width = 8.0f;
                    height = 34.0f;
                }
                pushPixel(pos, stableId, offsetX, offsetY, width, height,
                          0xFFE8A0FFu, 3);
                pushPixel(pos, stableId, offsetX * 0.7f, offsetY - 8.0f,
                          width * 0.6f, height * 0.6f, 0xFFFFFFFFu, 3);
            } else if (action->action == ecs::ActorAction::Crafting) {
                pushPixel(pos, stableId, -7.0f, -30.0f, 4.0f, 4.0f,
                          0xFFFFD060u, 3);
                pushPixel(pos, stableId, 5.0f, -37.0f, 3.0f, 3.0f,
                          0xFFFFF0A0u, 3);
                pushPixel(pos, stableId, 0.0f, -23.0f, 3.0f, 3.0f,
                          0xFFFFB040u, 3);
            }
        });
}

void WorldDemoState::DespawnGroundItems() {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    registry.view<ecs::ItemInstance>().each(
        [&](const ecs::Entity e, const ecs::ItemInstance&) {
            if (m_ctx.ecs->Alive(e)) {
                m_ctx.ecs->Destroy(e);
            }
        });
    m_ctx.ecs->Flush();
}

void WorldDemoState::OpenPauseMenu() {
    if (m_ctx.ui == nullptr || m_ctx.ui->IsMenuOpen()) {
        return;
    }
    m_ctx.ui->CloseInventory();
    m_speedBeforeMenu = m_ctx.gameTime->GetSpeed();
    if (m_speedBeforeMenu == core::GameTime::Speed::Paused) {
        m_speedBeforeMenu = core::GameTime::Speed::X1;
    }
    m_menuPaused = true;
    m_ctx.gameTime->SetSpeed(core::GameTime::Speed::Paused);
    m_ctx.ui->ShowPauseMenu();
    ZL_INFO("ui", "暂停菜单打开（ESC 继续）");
}

void WorldDemoState::ResumeFromMenu() {
    if (m_ctx.ui == nullptr) {
        return;
    }
    m_ctx.ui->HideMenu();
    if (!m_menuPaused) {
        return;
    }
    m_menuPaused = false;
    m_ctx.gameTime->SetSpeed(m_speedBeforeMenu);
    ZL_INFO("ui", "继续游戏：倍速 {}", core::GameTime::SpeedName(m_speedBeforeMenu));
}

void WorldDemoState::HandleMenuAction(ui::MenuActionType action) {
    switch (action) {
        case ui::MenuActionType::Continue:
        case ui::MenuActionType::Resume:
            ResumeFromMenu();
            break;
        case ui::MenuActionType::ReturnToMainMenu:
            if (m_networkMode) {
                if (m_ctx.leaveMultiplayer) {
                    m_ctx.leaveMultiplayer();
                }
                if (m_ctx.states != nullptr) {
                    m_ctx.states->SetState(std::make_unique<MainMenuState>(m_ctx));
                }
                ZL_INFO("ui", "离开联机会话，返回主菜单");
                break;
            }
            if (m_ctx.ui != nullptr) {
                m_ctx.ui->ShowMainMenu(true);
                ZL_INFO("ui", "返回主菜单（当前世界保留，可继续）");
            }
            break;
        case ui::MenuActionType::Multiplayer:
            break;
        case ui::MenuActionType::NewGame:
            if (m_ctx.gameTime != nullptr) {
                m_ctx.gameTime->SetSpeed(core::GameTime::Speed::X1);
            }
            if (m_ctx.states != nullptr) {
                m_ctx.states->SetState(std::make_unique<WorldDemoState>(m_ctx));
            }
            break;
        case ui::MenuActionType::Quit:
            if (m_ctx.window != nullptr) {
                m_ctx.window->SetShouldClose(true);
            }
            break;
    }
}

void WorldDemoState::OnExit() {
    // 退出前自动存档：先采集快照/提交后台，再销毁实体；GameApp 会在关闭时等待 zl-save
    m_ctx.executePlayerCommand = {};
    m_ctx.onSaveFinished = {};
    m_ctx.quickSave = {};
    if (m_autoSaveOnExit && m_player != entt::null && m_ctx.ecs != nullptr &&
        m_ctx.ecs->Alive(m_player) && m_ctx.saveService != nullptr) {
        if (m_screenshotPending) {
            m_screenshotPending = false;
            SubmitPendingSave();
        } else if (m_saveRequestPending) {
            SubmitPendingSave();
        } else if (!m_savePending) {
            RequestQuickSave(false);
        }
    }
    m_ctx.activeSaveId.clear();
    m_ctx.hudState = {};

    if (m_ctx.ui != nullptr) {
        m_ctx.ui->SetHudState(m_ctx.hudState);
        m_ctx.ui->CloseInventory();
        m_ctx.ui->SetInventoryActionHandler({});
        m_ctx.ui->SetSocialChatHandler({});
        m_ctx.ui->SetSocialState({});
        m_ctx.ui->HideMenu();
        m_ctx.ui->SetMenuActionHandler({});
    }
    if (m_menuPaused && m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetSpeed(core::GameTime::Speed::X1);
    }
    m_menuPaused = false;
    // 先清演示实体（含物理体/地面物品），再汇报统计
    DespawnGroundItems();
    DespawnDemoEntities();
    if (m_ctx.ecs != nullptr) {
        entt::registry& registry = m_ctx.ecs->Registry();
        std::vector<ecs::Entity> remaining;
        registry.view<ecs::StableId>().each(
            [&](const ecs::Entity entity, const ecs::StableId&) {
                if (m_ctx.ecs->Alive(entity)) {
                    remaining.push_back(entity);
                }
            });
        for (const ecs::Entity entity : remaining) {
            if (m_ctx.physics != nullptr) {
                if (const auto* rigid = registry.try_get<ecs::RigidBody>(entity);
                    rigid != nullptr && rigid->body != nullptr) {
                    m_ctx.physics->DestroyBody(rigid->body);
                }
            }
            m_ctx.ecs->Destroy(entity);
        }
        m_ctx.ecs->Flush();
    }
    m_networkEntities.clear();
    m_player = entt::null;
    const auto& stats = m_ctx.chunkManager->GetStats();
    ZL_INFO("render", "退出 WorldDemo：本状态共 {} tick；chunk 累计 加载 {} / 卸载 {} / 取消 {}",
            m_ticksInState, stats.totalLoaded, stats.totalUnloaded, stats.totalCancelled);
}

void WorldDemoState::UpdateAudio(double frameDt) {
    (void)frameDt;
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        m_footstepAnchorValid = false;
        return;
    }
    const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        m_footstepAnchorValid = false;
        return;
    }
    const glm::vec2 current{position->x, position->y};
    if (!m_footstepAnchorValid) {
        m_footstepAnchor = current;
        m_footstepAnchorValid = true;
        m_footstepDistance = 0.0f;
        return;
    }
    const glm::vec2 delta = current - m_footstepAnchor;
    const float moved = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    m_footstepAnchor = current;
    if (moved > 2.0f) {  // 传送/读档落位：不补一大串脚步
        m_footstepDistance = 0.0f;
        return;
    }

    const bool paused = m_menuPaused ||
                        (m_ctx.ui != nullptr && m_ctx.ui->IsMenuOpen()) ||
                        (m_ctx.gameTime != nullptr && m_ctx.gameTime->SpeedScale() <= 0.0);
    if (paused || moved <= 0.0001f) {
        return;
    }
    m_footstepDistance += moved;
    if (m_footstepDistance < m_footstepStride) {
        return;
    }
    m_footstepDistance = std::fmod(m_footstepDistance, m_footstepStride);

    std::string_view floorSprite = "tile.floor.grass";
    if (m_ctx.world != nullptr) {
        const int wx = static_cast<int>(std::floor(current.x));
        const int wy = static_cast<int>(std::floor(current.y));
        if (const world::GridSquare* square = m_ctx.world->GetSquare(wx, wy, 0);
            square != nullptr) {
            if (const world::SquareObject* floorObject = square->Floor();
                floorObject != nullptr && !floorObject->sprite.empty()) {
                floorSprite = floorObject->sprite;
            }
        }
    }

    core::SoundEvent step;
    step.x = current.x;
    step.y = current.y;
    step.radius = 2.5f;
    step.type = core::SoundType::Footstep;
    step.clip = std::string(audio::FootstepClipForFloor(floorSprite));
    if (m_ctx.audio != nullptr) {
        m_ctx.audio->EmitWorldSound(std::move(step));
    } else if (m_ctx.events != nullptr) {
        m_ctx.events->EmitQueued(core::kOnSound, std::move(step));
    }
}

void WorldDemoState::OnTick(double tickDt) {
    if (!m_networkMode) {
        UpdateCrafting(static_cast<float>(tickDt));
    } else if (m_ctx.networkClient != nullptr && m_ctx.networkClient->HasPlayer() &&
               m_moveCommandValid) {
        // 本地轻量预测：服务器快照到达后在 EntityReplicationClient 内校正/吸附。
        const glm::vec2 screen(m_lastMoveCommand.x, m_lastMoveCommand.y);
        if (glm::length(screen) > 0.0001f) {
            const glm::vec2 grid =
                glm::normalize(characters::InputSystem::ScreenDirToGridDir(
                    glm::normalize(screen)));
            (void)m_ctx.networkClient->AdvanceLocalPrediction(
                static_cast<float>(tickDt), grid.x, grid.y);
        }
    }
    if (m_networkMode) {
        RefreshSocialState();
    }
    ++m_ticksInState;
    // 单机：感知/AI/位移由固定链驱动；网络：服务器权威推进，客户端只应用副本。
}

void WorldDemoState::OnFrame(double frameDt, double alpha) {
    (void)alpha;
    using Clock = std::chrono::steady_clock;
    const auto frameStart = Clock::now();
    PollSaveScreenshot(frameDt);
    ConsumeSaveWarning();
    if (m_networkMode) {
        SyncNetworkReplicas(static_cast<float>(alpha));
        if (m_ctx.networkClient != nullptr && !m_ctx.networkClient->IsConnected() &&
            m_noticeTimer <= 0.0f) {
            const std::string reason = m_ctx.networkClient->LastError();
            SetNotice(reason.empty() ? "multiplayer: disconnected, reconnecting..."
                                     : "multiplayer: " + reason,
                      kNoticeFailColor, 2.0f);
        }
    }
    HandleFrameInput(frameDt);
    if (m_ctx.ui == nullptr || !m_ctx.ui->IsMenuOpen()) {
        UpdatePathFeedback(frameDt);  // M4-T7：点击移动反馈（提示文字/目标标记计时与状态跃迁）
        UpdateCruise(frameDt);
        UpdateCameraFollow(frameDt);  // M4-T5：跟随在巡航之后（巡航接管时不生效）
    }

    // 流式加载：相机中心（世界像素 → 世界格坐标）
    const glm::vec2 camGrid = render::FromScreen(m_ctx.camera->Center(), 0);
    m_ctx.chunkManager->Update(camGrid.x, camGrid.y);
    m_ctx.chunkManager->ProcessResults();

    // M4-T4：目标 chunk 加载完成后生成玩家（村庄中心附近找可走格）
    EnsurePlayerSpawned();
    // M6-T3：玩家就绪后散落地面物品（网络客户端不持有权威世界物品）
    if (!m_networkMode) {
        EnsureDemoItemsScattered();
    }
    // M11-T3：脚步/地面材质 → OnSound（音频 + AI 听觉）
    UpdateAudio(frameDt);
    const auto afterStreaming = Clock::now();

    CollectWorld();
    const auto afterWorldCollect = Clock::now();
    // M4-T2：ECS 实体 → 同列表的"高度精灵"通道；之后统一全局画家序排序
    if (m_ctx.ecs != nullptr && m_ctx.spatialIndex != nullptr) {
        RenderCollectSystem::Collect(*m_ctx.ecs, *m_ctx.spatialIndex, *m_ctx.assets, m_view,
                                     m_items);
    }
    const auto afterEntityCollect = Clock::now();
    // M6-T5：手持武器（装备槽 → 玩家旁图标；画在同格角色之上）
    AppendActorPresentation();
    std::sort(m_items.begin(), m_items.end(), render::DrawItemLess);
    const auto afterSort = Clock::now();
    DrawWorld();
    const auto afterWorldDraw = Clock::now();
    if (m_ctx.ui != nullptr && m_ctx.ui->IsMenuOpen()) {
        // 菜单是全屏模态：Debug 视图在 UI 之后合成，必须跳过世界调试叠层（含路径目标标记）。
        bgfx::touch(static_cast<bgfx::ViewId>(render::ViewId::Debug));
    } else {
        DrawDebugOverlay();
        DrawPhysicsDebug();
    }
    DrawDebugText();
    const auto afterDebugDraw = Clock::now();
    const auto elapsedMs = [](Clock::time_point begin, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - begin).count();
    };

    m_statsTimer += static_cast<float>(frameDt);
    if (m_statsTimer >= kStatsLogInterval) {
        m_statsTimer = 0.0f;
        const render::Renderer::Stats stats = m_ctx.renderer->GetStats();
        const auto& chunkStats = m_ctx.chunkManager->GetStats();
        ai::PathfindingService::Stats pathStats{};
        if (m_ctx.pathfinding != nullptr) {
            pathStats = m_ctx.pathfinding->GetStats();
        }
        audio::SoundManager::Stats audioStats{};
        if (m_ctx.audio != nullptr) {
            audioStats = m_ctx.audio->GetStats();
        }
        ai::PerceptionSystem::Stats perceptionStats{};
        if (m_ctx.perception != nullptr) {
            perceptionStats = m_ctx.perception->GetStats();
        }
        ZL_INFO("render",
                "世界统计: FPS={:.0f} | 实例={} | 四边形={} | 批次={} | drawcall={} | zoom={:.2f} | "
                "chunk 驻留={} 加载中={} | 相机格=({:.0f},{:.0f}) | 累计加载={} 卸载={} 取消={} 失败={} "
                "挂接={:.2f}ms/{}次 | "
                "ECS={} 索引={} | 物理: 静态={} 动态={} step={:.2f}ms | 寻路: 查询={} 完成={} 平均={:.2f}ms | "
                "感知: 见={} 警={} 听={} | "
                "音频: 活动={} 播放={} 脚步={} 枪声={} 低吼={} 环境={}",
                stats.fps, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
                m_ctx.sprites->DrawCallsThisFrame(), stats.drawCalls, m_ctx.camera->Zoom(),
                chunkStats.loadedChunks, chunkStats.loadingChunks, camGrid.x, camGrid.y,
                chunkStats.totalLoaded, chunkStats.totalUnloaded, chunkStats.totalCancelled,
                chunkStats.totalFailed, chunkStats.lastAttachMs,
                static_cast<unsigned long long>(chunkStats.attachBudgetHits),
                m_ctx.ecs != nullptr ? m_ctx.ecs->AliveCount() : 0u,
                m_ctx.spatialIndex != nullptr ? m_ctx.spatialIndex->EntityCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->StaticBodyCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->DynamicBodyCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->AvgStepMs() : 0.0,
                static_cast<unsigned long long>(pathStats.submitted),
                static_cast<unsigned long long>(pathStats.completed), pathStats.avgMs,
                perceptionStats.seeing, perceptionStats.alerted,
                static_cast<unsigned long long>(perceptionStats.heard),
                audioStats.activeVoices,
                static_cast<unsigned long long>(audioStats.eventsPlayed),
                static_cast<unsigned long long>(audioStats.footsteps),
                static_cast<unsigned long long>(audioStats.gunshots),
                static_cast<unsigned long long>(audioStats.growls),
                static_cast<unsigned long long>(audioStats.ambienceStarts));
        if (m_ctx.ecs != nullptr) {
            std::ostringstream timings;
            timings << std::fixed << std::setprecision(2);
            bool first = true;
            for (const auto& entry : m_ctx.ecs->Systems().Entries()) {
                if (!entry.enabled) {
                    continue;
                }
                if (!first) {
                    timings << ' ';
                }
                first = false;
                timings << entry.name << '=' << entry.avgMs;
            }
            ZL_INFO("perf", "系统耗时(ms): {}", timings.str());
        }
        ZL_INFO("perf",
                "帧内耗时(ms): 流式={:.2f} 世界收集={:.2f} 实体收集={:.2f} 排序={:.2f} "
                "世界绘制={:.2f} 调试={:.2f} OnFrame={:.2f} Frame={:.2f} 条目={}",
                elapsedMs(frameStart, afterStreaming),
                elapsedMs(afterStreaming, afterWorldCollect),
                elapsedMs(afterWorldCollect, afterEntityCollect),
                elapsedMs(afterEntityCollect, afterSort),
                elapsedMs(afterSort, afterWorldDraw),
                elapsedMs(afterWorldDraw, afterDebugDraw),
                elapsedMs(frameStart, afterDebugDraw),
                m_ctx.loop != nullptr ? m_ctx.loop->LastFrameMs() : 0.0, m_items.size());
        if (m_ctx.commands != nullptr) {
            const net::CommandPipeline::Stats commandStats = m_ctx.commands->GetStats();
            const std::uint64_t serverTick =
                m_ctx.commands->HasSnapshot() ? m_ctx.commands->LastSnapshot().serverTick : 0;
            const std::uint64_t nowTick =
                m_ctx.loop != nullptr ? m_ctx.loop->TotalTicks() : 0;
            ZL_INFO("net",
                    "命令: 模式={} | 提交={} 执行={} 拒绝={} | 包 发/收={}/{} | "
                    "队列={} 快照={} | 环回 tick 延迟={}",
                    net::TransportModeName(commandStats.mode),
                    static_cast<unsigned long long>(commandStats.commandsSubmitted),
                    static_cast<unsigned long long>(commandStats.commandsExecuted),
                    static_cast<unsigned long long>(commandStats.commandsRejected),
                    static_cast<unsigned long long>(commandStats.packetsSent),
                    static_cast<unsigned long long>(commandStats.packetsReceived),
                    commandStats.queuedCommands,
                    static_cast<unsigned long long>(commandStats.snapshotsReceived),
                    serverTick > 0 && nowTick >= serverTick ? nowTick - serverTick : 0);
        }
        if (m_ctx.networkClient != nullptr) {
            const NetworkClient::Stats netStats = m_ctx.networkClient->GetStats();
            ZL_INFO("net",
                    "客户端: 连接={} 玩家={} | 分配={} 快照={} 状态={} | 命令包发/拒={}/{} | "
                    "会话 发/收={}/{} 数据 发/收={}/{} | 实体副本={}",
                    m_ctx.networkClient->IsConnected() ? "yes" : "no",
                    static_cast<unsigned long long>(m_ctx.networkClient->PlayerStableId()),
                    static_cast<unsigned long long>(netStats.assignments),
                    static_cast<unsigned long long>(netStats.snapshotsReceived),
                    static_cast<unsigned long long>(netStats.playerStatesReceived),
                    static_cast<unsigned long long>(netStats.commandPacketsSent),
                    static_cast<unsigned long long>(netStats.commandPacketsRejected),
                    static_cast<unsigned long long>(netStats.session.messagesSent),
                    static_cast<unsigned long long>(netStats.session.messagesReceived),
                    static_cast<unsigned long long>(netStats.session.dataSent),
                    static_cast<unsigned long long>(netStats.session.dataReceived),
                    m_ctx.networkClient->Replication().Actors().size());
        }
    }

    // 压测采样（巡航期间每 5s 一条 perf 行）
    m_perfTimer += static_cast<float>(frameDt);
    if (m_perfTimer >= kPerfLogInterval) {
        m_perfTimer = 0.0f;
        if (m_cruiseEnabled && !m_cruiseFinished) {
            LogPerfSample("cruise");
        }
    }
    RefreshHud();
    RefreshInventory();
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

    if (m_ctx.ui != nullptr && m_ctx.ui->IsMenuOpen()) {
        SubmitMoveCommand(0.0f, 0.0f);
        return;
    }

    // M12-T1：WASD 也先变成命令（Move：屏幕方向）；InputSystem 在固定链里转成 MoveIntent。
    glm::vec2 moveScreen(0.0f, 0.0f);
    if (input.IsActionDown("MoveUp")) {
        moveScreen.y -= 1.0f;
    }
    if (input.IsActionDown("MoveDown")) {
        moveScreen.y += 1.0f;
    }
    if (input.IsActionDown("MoveLeft")) {
        moveScreen.x -= 1.0f;
    }
    if (input.IsActionDown("MoveRight")) {
        moveScreen.x += 1.0f;
    }
    SubmitMoveCommand(moveScreen.x, moveScreen.y);

    for (int i = 0; i < 5; ++i) {
        if (input.IsActionPressed(kHotbarActions[i]) && m_hotbarSelected != i) {
            net::Command command = MakePlayerCommand(net::CommandType::SelectHotbar);
            command.payload.index = i;
            SubmitPlayerCommand(command);
        }
    }

    if (input.IsActionPressed("Quit")) {
        ZL_INFO("render", "ESC 打开暂停菜单");
        events.Emit(EventProbe::kInputActionEvent, std::string("Quit"));
        OpenPauseMenu();
        return;
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
        SubmitPlayerCommand(MakePlayerCommand(net::CommandType::Attack));
    }
    if (input.IsActionPressed("Pickup")) {
        SubmitPlayerCommand(MakePlayerCommand(net::CommandType::PickupNearest));
    }
    if (input.IsActionPressed("Use")) {
        net::Command command = MakePlayerCommand(net::CommandType::UseBag);
        command.payload.index = -1;
        SubmitPlayerCommand(command);
    }
    if (input.IsActionPressed("Equip")) {
        net::Command command = MakePlayerCommand(net::CommandType::EquipBag);
        command.payload.index = -1;
        SubmitPlayerCommand(command);
    }
    if (input.IsActionPressed("Drop")) {
        net::Command command = MakePlayerCommand(net::CommandType::DropBag);
        command.payload.index = -1;
        SubmitPlayerCommand(command);
    }
    if (input.IsActionPressed("QuickSave")) {
        RequestQuickSave(true);  // M10-T5：F12 快照 → 截图 → 后台原子写盘
    }
    if (input.IsActionPressed("DemoGunshot")) {
        SubmitPlayerCommand(MakePlayerCommand(net::CommandType::Gunshot));
    }
    if (input.IsActionPressed("DemoProjectile")) {
        SubmitPlayerCommand(MakePlayerCommand(net::CommandType::FireProjectile));
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

    // M4-T7：左键点击移动（命令 MoveTo → 服务器校验 → PathRequest；巡航期间不接管）
    if (input.IsMouseButtonPressed(0) && !(m_cruiseEnabled && !m_cruiseFinished) &&
        m_player != entt::null) {
        net::Command command = MakePlayerCommand(net::CommandType::MoveTo);
        command.payload.x = static_cast<float>(pick.x);
        command.payload.y = static_cast<float>(pick.y);
        SubmitPlayerCommand(command);
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
    if (m_cachedSprite.empty() || object.sprite != m_cachedSprite) {
        m_cachedSprite = object.sprite;
        m_cachedFrames = m_ctx.assets->Frames(object.sprite);
    }
    const std::vector<AssetProvider::Entry>* frames = m_cachedFrames;
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
    m_cachedSprite = {};
    m_cachedFrames = nullptr;
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
            const std::uint8_t zMask = chunk->NonEmptyZMask();

            // 逐 z 层收集（z=0 地面起步；z≥1 楼上，空层快速跳过）
            for (int z = 0; z < core::kMaxZ; ++z) {
                if ((zMask & static_cast<std::uint8_t>(1u << z)) == 0) {
                    continue;
                }
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
    if (m_ctx.ui != nullptr &&
        (m_ctx.ui->IsInventoryOpen() || m_ctx.ui->IsMenuOpen())) {
        bgfx::dbgTextClear();
        return;
    }
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
                        "WASD/LMB move | SPACE attack | F pickup | E use | R equip | G drop | I bag | F6 demo-kill | F7 gunshot | V projectile | F4 cam | F9 grid | F1 pause | F2 speed | F5 reload | ESC quit");
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
    // M9-T4：点击移动/拾取/战斗提示改由 RmlUi HUD 浮字显示（支持中文）。
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
    // M6-T2/T3：背包（件数/重量/容量）+ 地面物品数（睡眠化：远处物品仍在场但不进渲染收集）
    if (m_ctx.ecs != nullptr && m_player != entt::null && m_ctx.items != nullptr) {
        const auto* container = m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
        if (container != nullptr) {
            int itemCount = 0;
            for (const ecs::ItemInstance& entry : container->items) {
                itemCount += entry.count;
            }
            bgfx::dbgTextPrintf(0, 15, 0x0f, "bag: %d item(s) %.1f/%.1fkg | ground=%d",
                                itemCount, items::TotalWeight(*container, *m_ctx.items),
                                container->capacityKg, game::CountGroundItems(*m_ctx.ecs));
        }
        // M6-T5：装备槽（装备/动作由 AppendActorPresentation 落世界；此处 HUD 展示）
        const auto* equipment = m_ctx.ecs->Registry().try_get<ecs::Equipment>(m_player);
        if (equipment != nullptr) {
            const auto& primary =
                equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
            const auto& body = equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::Body)];
            bgfx::dbgTextPrintf(0, 16, 0x0f, "equip: primary=%s body=%s",
                                primary.has_value() ? primary->defId.c_str() : "-",
                                body.has_value() ? body->defId.c_str() : "-");
        }
    }
}

}  // namespace zl::game
