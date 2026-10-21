#include "server/ServerHost.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "core/Config.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "core/GameTime.h"
#include "core/Log.h"
#include "core/SoundEvent.h"
#include "core/TaskScheduler.h"
#include "ecs/components/CommonComponents.h"
#include "game/EntityReplication.h"
#include "game/GroundItems.h"
#include "game/PlayerInventoryService.h"
#include "game/WorldQueries.h"
#include "game/WorldSimulation.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "net/Command.h"
#include "net/CommandCodec.h"
#include "net/KcpSession.h"
#include "net/PlayerAssignment.h"
#include "net/PlayerIdentity.h"
#include "net/PlayerState.h"
#include "net/Replication.h"
#include "save/ComponentRegistry.h"
#include "save/SaveManager.h"
#include "save/SaveService.h"
#include "server/PlayerProfile.h"
#include "ui/InventoryUi.h"
#include "world/ChunkManager.h"

namespace zl::server {
namespace {

std::uint32_t ReadU32Le(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

#if defined(_WIN32)
std::atomic<ServerHost*> g_activeHost{nullptr};

BOOL WINAPI ConsoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        if (ServerHost* host = g_activeHost.load()) {
            host->RequestStop();
        }
        return TRUE;
    }
    return FALSE;
}
#endif

}  // namespace

ServerHost::ServerHost() = default;

ServerHost::~ServerHost() {
    Shutdown();
}

int ServerHost::Run(int argc, char** argv) {
    core::InitLogging();
    ServerHost host;
    const std::string configPath = core::Config::FindConfigPath(argc, argv, "config/game.json");

#if defined(_WIN32)
    g_activeHost.store(&host);
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#endif

    Options options;
    options.configPath = configPath;
    options.argc = argc;
    options.argv = argv;
    options.network = true;  // 独立服务器默认监听 UDP；测试可显式关闭。
    options.enablePlayerProfiles = true;  // M13-T8：正式服务器默认持久化玩家档案。
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--server.network=false") {
            options.network = false;
        }
    }
    std::string error;
    int result = 1;
    if (!host.Init(options, &error)) {
        ZL_CRITICAL("server", "服务器启动失败：{}", error);
    } else {
        const double tickRate = host.Config().Get<double>("server.tickRate",
                                                          static_cast<double>(
                                                              host.m_options.tickRate));
        const double dt = 1.0 / std::max(1.0, tickRate);
        auto next = std::chrono::steady_clock::now();
        ZL_INFO("server", "无窗口服务器启动：{} Hz，maxTicks={}", tickRate,
                host.m_options.maxTicks);
        while (host.Running() &&
               (host.m_options.maxTicks <= 0 ||
                host.TickCount() <
                    static_cast<uint64_t>(host.m_options.maxTicks))) {
            host.Tick(static_cast<float>(dt));
            next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(dt));
            const auto now = std::chrono::steady_clock::now();
            if (now - next > std::chrono::milliseconds(500)) {
                next = now;  // 落后太多时重建节拍，避免补帧风暴
            }
            std::this_thread::sleep_until(next);
        }
        host.Shutdown();
        result = 0;
    }

#if defined(_WIN32)
    g_activeHost.store(nullptr);
    SetConsoleCtrlHandler(ConsoleHandler, FALSE);
#endif
    ZL_INFO("server", "服务器退出（ticks={}）", host.TickCount());
    core::ShutdownLogging();
    return result;
}

bool ServerHost::Init(const Options& options, std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        ZL_ERROR("server", "{}", message);
        Shutdown();
        return false;
    };
    if (m_initialized) {
        return true;
    }
    m_options = options;
    m_networkStats = {};
    m_socialSequence = 0;
    m_lastSocialTick = 0;
    m_socialDirty = true;
    m_chatLog.clear();

    m_config = std::make_unique<core::Config>();
    if (!m_config->LoadFile(m_options.configPath)) {
        return fail("配置文件加载失败：" + m_options.configPath);
    }
    if (m_options.argc > 0 && m_options.argv != nullptr) {
        m_config->ApplyCommandLine(m_options.argc, m_options.argv);
    }
    core::SetLogLevel(m_config->Get<std::string>("log.level", "debug"));
    if (!m_options.network) {
        m_options.network = m_config->Get<bool>("server.network", false);
    }
    if (m_options.network) {
        // 网络服务器不生成“本地玩家”；每个会话拥有自己的权威玩家实体。
        m_options.spawnLocalPlayer = false;
    }
    m_attackDamage =
        static_cast<float>(m_config->Get<double>("combat.playerDamage", 40.0));
    m_attackRange = static_cast<float>(m_config->Get<double>("combat.playerRange", 1.7));
    m_attackKnockback =
        static_cast<float>(m_config->Get<double>("combat.playerKnockback", 3.0));
    m_attackStagger =
        static_cast<float>(m_config->Get<double>("combat.playerStaggerSeconds", 0.35));
    m_gunshotRadius = static_cast<float>(
        m_config->Get<double>("sounds.gunshotRadius", 30.0));
    m_projectileDamage = static_cast<float>(
        std::max(0.1, m_config->Get<double>("net.projectileDamage", 24.0)));
    m_projectileSpeed = static_cast<float>(
        std::max(1.0, m_config->Get<double>("net.projectileSpeed", 18.0)));
    m_projectileLifetime = static_cast<float>(
        std::max(0.1, m_config->Get<double>("net.projectileLifetimeSeconds", 2.5)));
    m_maxProjectileDistance = static_cast<float>(
        std::max(1.0, m_config->Get<double>("net.maxProjectileDistance", 24.0)));
    m_stateSendIntervalTicks = static_cast<std::uint32_t>(
        std::clamp(m_config->Get<int>("net.playerStateIntervalTicks", 6), 1, 60));
    m_socialStateIntervalTicks = static_cast<std::uint32_t>(
        std::clamp(m_config->Get<int>("net.socialStateIntervalTicks", 30), 1, 600));

    // M13-T6：服务器侧命令策略；所有限制都在权威侧执行，客户端参数不可信。
    m_maxCommandsPerTick = static_cast<std::uint32_t>(
        std::max(1, m_config->Get<int>("net.maxCommandsPerTick", 8)));
    m_maxMoveToDistance = static_cast<float>(
        std::max(1.0, m_config->Get<double>("net.maxMoveToDistance", 64.0)));
    m_maxAttackDistance = static_cast<float>(
        std::max(0.5, m_config->Get<double>("net.maxAttackDistance", 3.0)));

    if (m_options.loadSaveId.empty()) {
        m_options.loadSaveId = m_config->Get<std::string>("server.saveId", "");
    }
    if (m_options.maxTicks <= 0) {
        m_options.maxTicks = m_config->Get<int>("server.ticks", 0);
    }
    if (!(m_options.tickRate > 0.0f)) {
        m_options.tickRate = static_cast<float>(
            m_config->Get<double>("server.tickRate", 60.0));
    }
    const double attackCooldownSeconds =
        std::max(0.0, m_config->Get<double>("net.attackCooldownSeconds", 0.35));
    m_attackCooldownTicks = static_cast<std::uint64_t>(std::llround(
        attackCooldownSeconds * static_cast<double>(m_options.tickRate)));
    const double projectileCooldownSeconds =
        std::max(0.0, m_config->Get<double>("net.projectileCooldownSeconds", 0.5));
    m_projectileCooldownTicks = static_cast<std::uint64_t>(std::llround(
        projectileCooldownSeconds * static_cast<double>(m_options.tickRate)));
    const double chatCooldownSeconds =
        std::max(0.0, m_config->Get<double>("net.chatCooldownSeconds", 1.0));
    m_chatCooldownTicks = static_cast<std::uint64_t>(std::llround(
        chatCooldownSeconds * static_cast<double>(m_options.tickRate)));

    // M13-T8：复活延迟/档案自动保存间隔。Options 显式值优先于配置（测试用）。
    const double respawnDelaySeconds =
        m_options.respawnDelaySeconds > 0.0f
            ? static_cast<double>(m_options.respawnDelaySeconds)
            : std::max(0.5, m_config->Get<double>("net.respawnDelaySeconds", 5.0));
    m_respawnDelayTicks = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(std::llround(
               respawnDelaySeconds * static_cast<double>(m_options.tickRate))));
    if (m_options.profileSaveIntervalSeconds > 0.0f) {
        m_profileSaveIntervalSeconds =
            static_cast<double>(m_options.profileSaveIntervalSeconds);
    } else {
        m_profileSaveIntervalSeconds = std::max(
            0.0, m_config->Get<double>("net.playerProfileSaveSeconds", 30.0));
    }
    if (m_options.enablePlayerProfiles) {
        if (m_options.playerProfilesFile.empty()) {
            m_options.playerProfilesFile = m_config->Get<std::string>(
                "net.playerProfilesFile", "saves/multiplayer_players.bin");
        }
        m_profilesEnabled = true;
    }

    m_events = std::make_unique<core::EventBus>();
    m_gameTime = std::make_unique<core::GameTime>();
    const double dayLengthMinutes =
        m_config->Get<double>("game.dayLengthMinutes", 60.0);
    const double gameMinutesPerRealSecond = 1440.0 / (dayLengthMinutes * 60.0);
    core::DateTime startDate;
    startDate.year = m_config->Get<int>("game.startYear", 2000);
    startDate.month = m_config->Get<int>("game.startMonth", 7);
    startDate.day = m_config->Get<int>("game.startDay", 9);
    startDate.hour = std::clamp(m_config->Get<int>("game.startHour", 0), 0, 23);
    m_gameTime->Configure(gameMinutesPerRealSecond, startDate);

    if (!LoadContent(error)) {
        return false;
    }

    const int threadCount = m_config->Get<int>("task.threads", 0);
    m_tasks = std::make_unique<core::TaskScheduler>(
        static_cast<unsigned>(std::max(threadCount, 0)));

    game::WorldSimulation::Options simulationOptions;
    simulationOptions.config = m_config.get();
    simulationOptions.events = m_events.get();
    simulationOptions.tasks = m_tasks.get();
    simulationOptions.saveManager = m_saveManager.get();
    simulationOptions.saveComponents = m_saveComponents.get();
    simulationOptions.activeSaveId = &m_activeSaveId;
    simulationOptions.activeSaveUseBackup = &m_activeSaveUseBackup;
    simulationOptions.saveWarning = &m_saveWarning;
    simulationOptions.focus = glm::vec2(m_options.playerTargetX, m_options.playerTargetY);
    m_simulation = std::make_unique<game::WorldSimulation>(simulationOptions);
    std::string simulationError;
    if (!m_simulation->Init(&simulationError)) {
        return fail("模拟初始化失败：" + simulationError);
    }

    game::InventoryServiceOptions inventoryOptions;
    inventoryOptions.pickupRadius = static_cast<float>(
        m_config->Get<double>("inventory.pickupRadius", 1.6));
    inventoryOptions.inventoryRange = static_cast<float>(
        m_config->Get<double>("inventory.lootRange", 3.0));
    inventoryOptions.stationRange = static_cast<float>(
        m_config->Get<double>("crafting.stationRange", 2.5));
    inventoryOptions.repairKitDefId =
        m_config->Get<std::string>("inventory.repairKit", "repair_kit");
    inventoryOptions.repairKitCount =
        std::max(1, m_config->Get<int>("inventory.repairKitCount", 1));
    inventoryOptions.repairAmount = std::clamp(
        static_cast<float>(m_config->Get<double>("inventory.repairAmount", 0.5)),
        0.01f, 1.0f);
    inventoryOptions.stationLabels.emplace("workbench", "工作台");
    inventoryOptions.stationLabels.emplace("campfire", "篝火");
    const auto& root = m_config->Raw();
    const auto crafting = root.find("crafting");
    if (crafting != root.end() && crafting->is_object()) {
        const auto labels = crafting->find("stationLabels");
        if (labels != crafting->end() && labels->is_object()) {
            for (const auto& [id, label] : labels->items()) {
                if (id.empty() || !label.is_string() ||
                    label.get<std::string>().empty()) {
                    continue;
                }
                inventoryOptions.stationLabels[id] = label.get<std::string>();
            }
        }
    }
    m_inventory = std::make_unique<game::PlayerInventoryService>(
        m_simulation->Ecs(), *m_items, *m_recipes, &m_simulation->World(),
        m_events.get(), std::move(inventoryOptions));

    if (!m_options.loadSaveId.empty()) {
        if (!m_simulation->LoadSave(m_options.loadSaveId, &simulationError)) {
            return fail(simulationError);
        }
        m_demoLootSpawned = true;  // 存档世界已包含自己的物品，不再叠加演示散落
    } else if (m_options.spawnDemoPopulation) {
        m_simulation->SpawnDemoPopulation();
    }

    if (m_profilesEnabled && !InitPlayerProfiles(error)) {
        return false;
    }

    if (m_options.network) {
        if (!InitNetwork(error)) {
            return false;
        }
    }

    m_running = true;
    m_initialized = true;
    ZL_INFO("server", "服务器内容就绪：地图 chunks={}，物品 {}，配方 {}，系统 {}{}",
            m_simulation->Chunks().Map().chunksX * m_simulation->Chunks().Map().chunksY,
            m_items->All().size(), m_recipes->All().size(),
            m_simulation->Ecs().Systems().Count(),
            m_networkEnabled ? "，网络 UDP 监听 " + std::to_string(NetworkPort()) : "");
    return true;
}

bool ServerHost::InitNetwork(std::string* error) {
    m_replicationRegistry = std::make_unique<net::ReplicationRegistry>();
    std::string registryError;
    if (!game::RegisterEntityReplicationComponents(*m_replicationRegistry,
                                                   &registryError)) {
        if (error != nullptr) {
            *error = "复制组件注册失败：" + registryError;
        }
        return false;
    }

    if (!m_options.bindHostOverride) {
        m_options.bindHost =
            m_config->Get<std::string>("net.bindHost", m_options.bindHost);
    }
    if (!m_options.portOverride && m_options.port == 7777) {
        m_options.port = static_cast<std::uint16_t>(
            std::clamp(m_config->Get<int>("net.port", 7777), 0, 65535));
    }
    m_options.maxSessions = static_cast<std::size_t>(
        std::max(1, m_config->Get<int>("net.maxSessions",
                                       static_cast<int>(m_options.maxSessions))));
    if (m_options.contentHash == 0) {
        m_options.contentHash = static_cast<std::uint64_t>(
            m_config->Get<std::int64_t>("net.contentHash", 0));
    }

    net::KcpSessionServerOptions options;
    options.contentHash = m_options.contentHash;
    options.maxSessions = m_options.maxSessions;
    m_network = std::make_unique<net::KcpSessionServer>(options);
    std::string listenError;
    if (!m_network->Listen(m_options.bindHost, m_options.port, &listenError)) {
        if (error != nullptr) {
            *error = "服务器网络监听失败：" + listenError;
        }
        m_network.reset();
        return false;
    }
    m_networkEnabled = true;
    ZL_INFO("net", "服务器 UDP 会话监听：{}:{}（maxSessions={}，contentHash={:#x}）",
            m_options.bindHost, m_network->Port(), m_options.maxSessions,
            m_options.contentHash);
    return true;
}

bool ServerHost::LoadContent(std::string* error) {
    m_items = std::make_unique<items::ItemRegistry>();
    const std::string itemsFile =
        m_config->Get<std::string>("items.file", "assets/items.json");
    if (!m_items->LoadFile(itemsFile)) {
        ZL_WARN("items", "物品注册表为空，物品相关功能不可用（文件: {}）", itemsFile);
    }

    m_recipes = std::make_unique<items::RecipeRegistry>();
    const std::string recipesFile =
        m_config->Get<std::string>("recipes.file", "assets/recipes.json");
    if (!m_recipes->LoadFile(recipesFile, *m_items)) {
        ZL_WARN("items", "配方注册表为空，制作功能不可用（文件: {}）", recipesFile);
    }

    save::SaveManager::Options saveOptions;
    saveOptions.root = m_config->Get<std::string>("save.root", "saves");
    saveOptions.gameVersion =
        m_config->Get<std::string>("save.gameVersion", save::kDefaultGameVersion);
    m_saveManager = std::make_unique<save::SaveManager>(std::move(saveOptions));
    m_saveComponents = std::make_unique<save::ComponentRegistry>();
    std::string componentError;
    if (!save::RegisterBuiltinComponents(*m_saveComponents, &componentError)) {
        if (error != nullptr) {
            *error = "存档组件注册失败：" + componentError;
        }
        return false;
    }
    m_saveService = std::make_unique<save::SaveService>(*m_saveManager);
    return true;
}

bool ServerHost::InitPlayerProfiles(std::string* error) {
    PlayerProfileStore::Options options;
    options.path = m_options.playerProfilesFile;
    m_playerProfiles = std::make_unique<PlayerProfileStore>(std::move(options));
    std::string loadError;
    if (!m_playerProfiles->Load(&loadError)) {
        if (error != nullptr) {
            *error = "玩家档案加载失败：" + loadError;
        }
        m_playerProfiles.reset();
        return false;
    }
    ZL_INFO("server", "玩家档案已加载：{} 个（{}）", m_playerProfiles->Size(),
            m_playerProfiles->Path().string());
    return true;
}

void ServerHost::Shutdown() {
    if (!m_initialized && m_simulation == nullptr) {
        return;
    }
    m_running = false;
    if (m_profilesEnabled && m_playerProfiles != nullptr && m_simulation != nullptr) {
        SavePlayerProfiles("shutdown");
    }
    m_sessions.clear();
    m_chatLog.clear();
    m_socialDirty = true;
    m_network.reset();
    m_networkEnabled = false;
    m_replicationRegistry.reset();
    m_inventory.reset();
    m_simulation.reset();
    m_playerProfiles.reset();
    m_profilesEnabled = false;
    if (m_saveService != nullptr) {
        m_saveService->WaitIdle();
        m_saveService.reset();
    }
    m_saveManager.reset();
    m_saveComponents.reset();
    m_recipes.reset();
    m_items.reset();
    m_tasks.reset();
    m_gameTime.reset();
    m_events.reset();
    m_config.reset();
    m_initialized = false;
    ZL_INFO("server", "服务器资源已释放");
}

void ServerHost::Tick(float dt) {
    if (!m_initialized || m_simulation == nullptr) {
        return;
    }
    if (!(dt > 0.0f) || !std::isfinite(dt)) {
        dt = 1.0f / 60.0f;
    }

    ecs::Entity player = m_simulation->Player();
    if (player != entt::null) {
        const auto& position = m_simulation->Ecs().Registry().get<ecs::Position>(player);
        m_simulation->SetFocus(position.x, position.y);
    } else {
        m_simulation->SetFocus(m_options.playerTargetX, m_options.playerTargetY);
    }

    m_simulation->Stream();
    m_tasks->ProcessResults();

    if (m_networkEnabled) {
        for (auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            session.commandsThisTick = 0;
        }
        PollNetwork();
        for (auto& [sessionId, session] : m_sessions) {
            if (!EnsureSessionPlayer(sessionId, session)) {
                break;  // chunk 尚未就绪；下一 tick 继续
            }
        }
    }

    if (m_options.spawnLocalPlayer && m_simulation->Player() == entt::null) {
        if (m_simulation->EnsureLocalPlayer(m_options.playerTargetX,
                                            m_options.playerTargetY) != entt::null &&
            m_options.spawnDemoPopulation) {
            SpawnDemoLoot();
        }
    }

    m_gameTime->Tick();
    m_events->EmitQueued(core::kOnTick, static_cast<int64_t>(m_ticks));
    m_simulation->Tick(dt);
    TickCrafting(dt);
    UpdateProjectiles(dt);
    UpdateTransientEffects(dt);

    UpdatePlayerLifecycle();
    m_simulation->Flush();
    m_events->FlushQueued();

    if (m_saveService != nullptr) {
        m_saveService->PollResults();
    }
    ++m_ticks;
    if (m_networkEnabled) {
        BroadcastSnapshots();
        BroadcastPlayerStates();
        BroadcastSocialStates();
    }
    if (m_profilesEnabled && m_profileSaveIntervalSeconds > 0.0) {
        m_profileSaveTimer += static_cast<double>(dt);
        if (m_profileSaveTimer >= m_profileSaveIntervalSeconds) {
            SavePlayerProfiles("interval");
        }
    }
    m_logTimer += static_cast<double>(dt);
    if (m_logTimer >= 5.0) {
        m_logTimer = 0.0;
        LogStatus();
    }
}

void ServerHost::RunForTicks(int count, float dt) {
    for (int i = 0; i < count && m_running; ++i) {
        Tick(dt);
    }
}

void ServerHost::RequestStop() {
    m_running = false;
}

bool ServerHost::Running() const {
    return m_running;
}

uint64_t ServerHost::TickCount() const {
    return m_ticks;
}

game::WorldSimulation& ServerHost::Simulation() {
    return *m_simulation;
}

const game::WorldSimulation& ServerHost::Simulation() const {
    return *m_simulation;
}

const core::Config& ServerHost::Config() const {
    return *m_config;
}

void ServerHost::PollNetwork() {
    if (m_network == nullptr) {
        return;
    }
    m_network->Poll();
    net::KcpSessionServerEvent event;
    while (m_network->PollEvent(&event)) {
        switch (event.type) {
            case net::KcpSessionServerEventType::Connected: {
                std::string identity = event.playerIdentity;
                if (identity.empty()) {
                    identity = "session-" + std::to_string(event.sessionId);
                }
                if (!net::IsValidPlayerIdentity(identity)) {
                    ZL_WARN("net", "拒绝非法玩家身份：session={}", event.sessionId);
                    if (m_network != nullptr) {
                        (void)m_network->Disconnect(
                            event.sessionId, net::SessionDisconnectReason::BadRequest,
                            "玩家身份格式非法");
                    }
                    break;
                }
                auto existingSession = m_sessions.end();
                for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
                    if (it->first != event.sessionId && it->second.identity == identity) {
                        existingSession = it;
                        break;
                    }
                }
                if (existingSession != m_sessions.end()) {
                    const std::uint64_t oldSessionId = existingSession->first;
                    CaptureSessionProfile(existingSession->second,
                                          existingSession->second.dead);
                    if (m_simulation != nullptr &&
                        existingSession->second.playerStableId != 0) {
                        const ecs::Entity oldPlayer = m_simulation->Ecs().FindById(
                            existingSession->second.playerStableId);
                        if (oldPlayer != entt::null) {
                            m_simulation->DespawnEntity(oldPlayer);
                        }
                    }
                    if (m_network != nullptr) {
                        (void)m_network->Disconnect(
                            oldSessionId, net::SessionDisconnectReason::ClientLeft,
                            "同身份新连接接管会话");
                    }
                    m_sessions.erase(existingSession);
                    SavePlayerProfiles("reconnect-replace");
                    ZL_INFO("net", "同身份新连接接管：oldSession={}，newSession={}，身份={}",
                            oldSessionId, event.sessionId, identity);
                }
                const auto [it, inserted] = m_sessions.try_emplace(event.sessionId);
                if (!inserted) {
                    ZL_WARN("net", "忽略重复 Connected 事件：session={}", event.sessionId);
                    break;
                }
                it->second.identity = identity;
                it->second.playerName =
                    event.playerName.empty() ? "Player" : event.playerName;
                if (!net::IsValidChatText(it->second.playerName)) {
                    it->second.playerName = "Player";
                }
                m_socialDirty = true;
                ZL_INFO("net", "客户端加入：session={}，身份={}，名称={}（当前 {} 人）",
                        event.sessionId, it->second.identity, it->second.playerName,
                        m_sessions.size());
                (void)EnsureSessionPlayer(event.sessionId, it->second);
                break;
            }
            case net::KcpSessionServerEventType::Data: {
                const auto it = m_sessions.find(event.sessionId);
                if (it == m_sessions.end()) {
                    ZL_WARN("net", "收到未登记会话的数据：session={}", event.sessionId);
                    break;
                }
                ++m_networkStats.dataPacketsReceived;
                if (event.data.size() > net::kMaxCommandPacketBytes) {
                    ++m_networkStats.oversizedPackets;
                    ++m_networkStats.malformedPackets;
                    ZL_WARN("net", "session={} 命令包超过 {} 字节上限（{}），已拒绝",
                            event.sessionId, net::kMaxCommandPacketBytes,
                            event.data.size());
                    break;
                }
                if (event.data.size() >= 4 &&
                    ReadU32Le(event.data.data()) == net::kChatMessageMagic) {
                    net::ChatMessage chat;
                    std::string chatError;
                    if (!net::DecodeChatMessage(event.data, &chat, &chatError)) {
                        ++m_networkStats.chatMessagesRejected;
                        ZL_WARN("net", "session={} 聊天消息无效：{}", event.sessionId,
                                chatError);
                        break;
                    }
                    ++m_networkStats.chatMessagesReceived;
                    HandleChatMessage(event.sessionId, it->second, chat.text);
                    break;
                }
                net::CommandPacket packet;
                std::string error;
                if (!net::DecodeCommandPacket(event.data, &packet, &error)) {
                    ++m_networkStats.malformedPackets;
                    ZL_WARN("net", "session={} 命令包解码失败：{}", event.sessionId, error);
                    break;
                }
                SessionPlayer& session = it->second;
                if (packet.sequence <= session.lastCommandSequence) {
                    ++m_networkStats.stalePackets;
                    ZL_WARN("net", "session={} 命令包序列未前进（{} <= {}）",
                            event.sessionId, packet.sequence,
                            session.lastCommandSequence);
                    break;
                }
                session.lastCommandSequence = packet.sequence;
                for (const net::Command& command : packet.commands) {
                    ++m_networkStats.commandsReceived;
                    if (session.commandsThisTick >= m_maxCommandsPerTick) {
                        ++m_networkStats.commandsRejected;
                        ++m_networkStats.rateLimitedCommands;
                        ZL_DEBUG("net", "session={} 命令 {} 超过每 tick 配额 {}",
                                 event.sessionId, net::CommandTypeName(command.type),
                                 m_maxCommandsPerTick);
                        continue;
                    }
                    ++session.commandsThisTick;
                    if (!ExecuteSessionCommand(event.sessionId, session, command, &error)) {
                        ++m_networkStats.commandsRejected;
                        ZL_DEBUG("net", "session={} 命令 {} 被拒绝：{}", event.sessionId,
                                 net::CommandTypeName(command.type), error);
                    } else {
                        ++m_networkStats.commandsAccepted;
                    }
                }
                break;
            }
            case net::KcpSessionServerEventType::Disconnected:
                RemoveSession(event.sessionId, static_cast<std::uint8_t>(event.reason),
                              event.message);
                break;
        }
    }
}

bool ServerHost::EnsureSessionPlayer(std::uint64_t sessionId, SessionPlayer& session) {
    static_cast<void>(sessionId);
    if (m_simulation == nullptr || m_replicationRegistry == nullptr) {
        return false;
    }
    if (session.dead) {
        if (m_ticks < session.respawnAtTick) {
            return true;
        }
        return RespawnSessionPlayer(session);
    }
    return SpawnOrRestoreSessionPlayer(sessionId, session);
}

PlayerProfile* ServerHost::FindSessionProfile(SessionPlayer& session) {
    if (!m_profilesEnabled || m_playerProfiles == nullptr || session.identity.empty()) {
        return nullptr;
    }
    return m_playerProfiles->Find(session.identity);
}

bool ServerHost::SpawnOrRestoreSessionPlayer(std::uint64_t sessionId,
                                             SessionPlayer& session) {
    PlayerProfile* profile = FindSessionProfile(session);
    if (profile == nullptr && m_profilesEnabled && m_playerProfiles != nullptr) {
        profile = &m_playerProfiles->Create(session.identity, session.playerName);
        if (profile->stableId == 0) {
            ZL_ERROR("net", "无法为身份 {} 分配 StableId", session.identity);
            return false;
        }
    }
    if (profile != nullptr) {
        if (profile->stableId == 0) {
            ZL_ERROR("net", "玩家档案缺少 StableId：身份={}", session.identity);
            return false;
        }
        session.playerStableId = profile->stableId;
        if (!profile->alive) {
            session.dead = true;
            session.needsFullSnapshot = true;
            session.stateDirty = true;
            if (session.respawnAtTick == 0) {
                const double seconds = std::max(
                    0.0, static_cast<double>(profile->respawnSeconds));
                const std::uint64_t ticks = static_cast<std::uint64_t>(std::llround(
                    seconds * static_cast<double>(m_options.tickRate)));
                session.respawnAtTick = m_ticks + std::max<std::uint64_t>(1, ticks);
            }
            SetSessionStatus(session, "你已死亡，等待复活", 2);
            m_socialDirty = true;
            return true;
        }
    }

    auto waitForSpawn = [&] {
        session.spawned = false;
        session.playerStableId = 0;
        session.assignmentSent = false;
        return false;
    };
    ecs::Entity player = entt::null;
    if (session.playerStableId != 0) {
        player = m_simulation->Ecs().FindById(session.playerStableId);
        if (player != entt::null) {
            session.spawned = true;
            return true;
        }
    }
    if (profile == nullptr && session.playerStableId != 0) {
        // 旧行为（未启用档案）：实体丢失后重新分配。
        session.playerStableId = 0;
        session.assignmentSent = false;
        session.producer.reset();
    }

    if (profile != nullptr && profile->hasSnapshot) {
        std::string restoreError;
        player = m_simulation->RestoreNetworkPlayer(profile->entity, false,
                                                    &restoreError);
        if (player == entt::null) {
            ZL_WARN("net", "session={} 恢复玩家失败，下一 tick 重试：{}", session.identity,
                    restoreError);
            return waitForSpawn();
        }
        ++m_networkStats.profileRestores;
    } else {
        const int slot = static_cast<int>((sessionId - 1) % 8);
        const float offsetX = static_cast<float>((slot % 3) - 1) * 1.5f;
        const float offsetY = static_cast<float>((slot / 3) - 1) * 1.5f;
        player = m_simulation->SpawnNetworkPlayer(
            m_options.playerTargetX + offsetX, m_options.playerTargetY + offsetY, 24,
            session.playerStableId);
        if (player == entt::null) {
            return waitForSpawn();  // chunk 尚未就绪
        }
    }

    session.playerStableId = m_simulation->Ecs().IdOf(player).id;
    session.spawned = true;
    m_socialDirty = true;
    if (session.playerStableId == 0) {
        ZL_ERROR("net", "session={} 玩家实体缺少 StableId", session.identity);
        return waitForSpawn();
    }
    if (profile != nullptr) {
        profile->stableId = session.playerStableId;
        profile->alive = true;
        profile->respawnSeconds = 0.0f;
        profile->state.playerStableId = session.playerStableId;
    } else {
        // 旧路径：没有档案时仍按现分配生成。
    }

    game::EntityReplicationProducer::Options producerOptions;
    producerOptions.aoiRadius = static_cast<float>(
        m_config->Get<double>("net.aoiRadius", 48.0));
    producerOptions.maxEntities = static_cast<std::size_t>(
        std::max(1, m_config->Get<int>("net.maxEntities", 256)));
    if (session.producer == nullptr) {
        session.producer = std::make_unique<game::EntityReplicationProducer>(
            m_simulation->Ecs(), *m_replicationRegistry, producerOptions);
    } else {
        session.producer->Reset();
    }
    session.assignmentSent = false;
    session.needsFullSnapshot = true;
    session.stateDirty = true;
    session.lastStateTick = 0;
    session.dead = false;
    session.respawnAtTick = 0;
    session.lastCommandSequence = 0;
    session.commandsThisTick = 0;
    session.lastAttackTick = 0;
    session.attackUsed = false;
    session.lastProjectileTick = 0;
    session.projectileUsed = false;
    if (profile != nullptr && !profile->hasSnapshot) {
        CaptureSessionProfile(session, false);
    }
    if (m_options.spawnDemoPopulation) {
        SpawnDemoLoot();
    }
    ZL_INFO("net", "身份={} 分配玩家 stableId={}（当前 {} 人）", session.identity,
            session.playerStableId, m_sessions.size());
    return true;
}

void ServerHost::BroadcastSnapshots() {
    if (!m_networkEnabled || m_network == nullptr || m_simulation == nullptr ||
        m_replicationRegistry == nullptr || m_ticks == 0) {
        return;
    }
    for (auto& [sessionId, session] : m_sessions) {
        if (session.playerStableId == 0) {
            continue;
        }

        if (!session.assignmentSent) {
            net::PlayerAssignment assignment;
            assignment.sessionId = sessionId;
            assignment.playerStableId = session.playerStableId;
            assignment.serverTick = m_ticks;
            std::string error;
            const std::vector<std::uint8_t> bytes =
                net::EncodePlayerAssignment(assignment, &error);
            if (bytes.empty()) {
                ZL_WARN("net", "session={} 玩家分配消息编码失败：{}", sessionId, error);
                continue;
            }
            if (!m_network->Send(sessionId, bytes, &error)) {
                ZL_WARN("net", "session={} 玩家分配消息发送失败：{}", sessionId, error);
                continue;
            }
            session.assignmentSent = true;
            ++m_networkStats.assignmentsSent;
        }

        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null || session.producer == nullptr) {
            continue;
        }
        const auto* position =
            m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
        if (position == nullptr) {
            continue;
        }

        net::ReplicationSnapshot snapshot;
        std::string error;
        if (session.needsFullSnapshot || !session.producer->HasBaseline()) {
            snapshot = session.producer->BuildFull(m_ticks, position->x, position->y,
                                                   &error);
        } else {
            if (!session.producer->BuildDelta(m_ticks, position->x, position->y,
                                              &snapshot, &error)) {
                ++m_networkStats.snapshotFailures;
                ZL_WARN("net", "session={} 复制增量构建失败，下一 tick 重发全量：{}",
                        sessionId, error);
                session.producer->Reset();
                session.needsFullSnapshot = true;
                continue;
            }
        }
        if (!error.empty()) {
            ++m_networkStats.snapshotFailures;
            ZL_WARN("net", "session={} 复制快照构建失败，下一 tick 重发全量：{}", sessionId,
                    error);
            session.producer->Reset();
            session.needsFullSnapshot = true;
            continue;
        }
        const std::vector<std::uint8_t> bytes =
            net::EncodeReplicationSnapshot(snapshot, *m_replicationRegistry, &error);
        if (bytes.empty()) {
            ++m_networkStats.snapshotFailures;
            ZL_WARN("net", "session={} 复制快照编码失败，下一 tick 重发全量：{}", sessionId,
                    error);
            session.producer->Reset();
            session.needsFullSnapshot = true;
            continue;
        }
        if (!m_network->Send(sessionId, bytes, &error)) {
            ++m_networkStats.snapshotFailures;
            ZL_WARN("net", "session={} 复制快照发送失败，下一 tick 重发全量：{}", sessionId,
                    error);
            session.producer->Reset();
            session.needsFullSnapshot = true;
            continue;
        }
        session.needsFullSnapshot = false;
        ++m_networkStats.snapshotsSent;
    }
}

void ServerHost::SetSessionStatus(SessionPlayer& session, std::string message,
                                  std::uint8_t tone) {
    session.status = std::move(message);
    session.statusTone = std::min<std::uint8_t>(tone, 2);
    ++session.statusSequence;
    session.stateDirty = true;
}

void ServerHost::FillPlayerState(const SessionPlayer& session,
                                 net::PlayerState* state) {
    if (state == nullptr) {
        return;
    }
    *state = {};
    state->playerStableId = session.playerStableId;
    state->serverTick = m_ticks;
    state->alive = !session.dead;
    if (m_profilesEnabled && m_playerProfiles != nullptr) {
        if (const PlayerProfile* profile =
                m_playerProfiles->Find(session.identity);
            profile != nullptr) {
            *state = profile->state;
            state->playerStableId = session.playerStableId;
            state->serverTick = m_ticks;
            state->alive = !session.dead;
        }
    }
    if (session.dead) {
        state->health = 0.0f;
        const std::uint64_t remainingTicks =
            session.respawnAtTick > m_ticks ? session.respawnAtTick - m_ticks : 0;
        state->respawnSeconds = static_cast<float>(remainingTicks) /
                                std::max(1.0f, m_options.tickRate);
    } else {
        state->respawnSeconds = 0.0f;
    }

    const ecs::Entity player = m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        state->statusSequence = session.statusSequence;
        state->statusTone = session.statusTone;
        state->status = session.status;
        return;
    }

    entt::registry& registry = m_simulation->Ecs().Registry();
    if (const auto* health = registry.try_get<ecs::Health>(player);
        health != nullptr) {
        state->health = health->current;
        state->healthMax = health->max;
    }
    if (const auto* container = registry.try_get<ecs::Container>(player);
        container != nullptr) {
        state->capacityKg = container->capacityKg;
        state->bag.clear();
        state->bag.reserve(container->items.size());
        for (const ecs::ItemInstance& item : container->items) {
            state->bag.push_back(
                net::PlayerStateItem{item.defId, item.count, item.condition});
        }
    }
    if (const auto* equipment = registry.try_get<ecs::Equipment>(player);
        equipment != nullptr) {
        state->equipment = {};
        for (std::size_t i = 0;
             i < equipment->slots.size() && i < state->equipment.size(); ++i) {
            if (!equipment->slots[i].has_value()) {
                continue;
            }
            const ecs::ItemInstance& item = *equipment->slots[i];
            state->equipment[i].present = true;
            state->equipment[i].item =
                net::PlayerStateItem{item.defId, item.count, item.condition};
        }
    }
    if (const auto* known = registry.try_get<ecs::KnownRecipes>(player);
        known != nullptr) {
        state->knownRecipes = known->recipeIds;
    }
    if (const auto* job = registry.try_get<ecs::CraftingJob>(player);
        job != nullptr) {
        state->craftingActive = job->active;
        state->craftingRecipeId = job->active ? job->recipeId : std::string();
        state->craftingElapsed = job->active ? job->elapsed : 0.0f;
        state->craftingDuration = job->active ? job->duration : 0.0f;
        state->craftingToolEfficiency = job->active ? job->toolEfficiency : 1.0f;
        state->batchTotal = job->active ? job->batchTotal : 0;
        state->batchCompleted = job->active ? job->batchCompleted : 0;
        state->craftQueue.clear();
        state->craftQueue.reserve(job->queue.size());
        for (const ecs::CraftQueueItem& entry : job->queue) {
            state->craftQueue.push_back(
                net::PlayerStateCraftQueue{entry.recipeId, entry.count});
        }
    }
    state->statusSequence = session.statusSequence;
    state->statusTone = session.statusTone;
    state->status = session.status;
}

void ServerHost::BroadcastPlayerStates() {
    if (!m_networkEnabled || m_network == nullptr || m_simulation == nullptr ||
        m_inventory == nullptr || m_ticks == 0) {
        return;
    }
    for (auto& [sessionId, session] : m_sessions) {
        if (session.playerStableId == 0 || !session.assignmentSent) {
            continue;
        }
        if (!session.stateDirty && !session.dead &&
            m_ticks < session.lastStateTick + m_stateSendIntervalTicks) {
            continue;
        }

        net::PlayerState state;
        FillPlayerState(session, &state);
        std::string error;
        const std::vector<std::uint8_t> bytes =
            net::EncodePlayerState(state, &error);
        if (bytes.empty()) {
            ++m_networkStats.playerStateFailures;
            ZL_WARN("net", "session={} 玩家状态编码失败：{}", sessionId, error);
            continue;
        }
        if (!m_network->Send(sessionId, bytes, &error)) {
            ++m_networkStats.playerStateFailures;
            ZL_WARN("net", "session={} 玩家状态发送失败：{}", sessionId, error);
            continue;
        }
        session.stateDirty = false;
        session.lastStateTick = m_ticks;
        ++m_networkStats.playerStatesSent;
    }
}

void ServerHost::HandleChatMessage(std::uint64_t sessionId, SessionPlayer& session,
                                   const std::string& text) {
    static_cast<void>(sessionId);
    const std::size_t begin = text.find_first_not_of(" \t");
    const std::size_t end = text.find_last_not_of(" \t");
    if (begin == std::string::npos || end == std::string::npos) {
        ++m_networkStats.chatMessagesRejected;
        return;
    }
    std::string trimmed = text.substr(begin, end - begin + 1);
    if (session.chatUsed &&
        m_ticks < session.lastChatTick + m_chatCooldownTicks) {
        ++m_networkStats.chatMessagesRejected;
        SetSessionStatus(session, "聊天过于频繁，请稍后再试", 2);
        return;
    }
    if (!net::IsValidChatText(trimmed)) {
        ++m_networkStats.chatMessagesRejected;
        return;
    }
    session.lastChatTick = m_ticks;
    session.chatUsed = true;
    m_chatLog.push_back(net::SocialChatLine{m_ticks, session.playerName, trimmed});
    while (m_chatLog.size() > net::kMaxSocialChatMessages) {
        m_chatLog.pop_front();
    }
    m_socialDirty = true;
    ZL_DEBUG("net", "session={} 聊天：{}: {}", sessionId, session.playerName, trimmed);
}

void ServerHost::FillSocialState(net::SocialState* state) const {
    if (state == nullptr) {
        return;
    }
    state->serverTick = m_ticks;
    state->sequence = m_socialSequence;
    state->players.clear();
    state->players.reserve(m_sessions.size());
    for (const auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0) {
            continue;
        }
        state->players.push_back(
            net::SocialPlayer{session.playerStableId, session.playerName});
    }
    std::sort(state->players.begin(), state->players.end(),
              [](const net::SocialPlayer& left, const net::SocialPlayer& right) {
                  return left.stableId < right.stableId;
              });
    state->chat.assign(m_chatLog.begin(), m_chatLog.end());
}

void ServerHost::BroadcastSocialStates() {
    if (!m_networkEnabled || m_network == nullptr || m_sessions.empty()) {
        return;
    }
    if (!m_socialDirty &&
        m_ticks < m_lastSocialTick + m_socialStateIntervalTicks) {
        return;
    }
    ++m_socialSequence;
    net::SocialState state;
    FillSocialState(&state);
    std::string error;
    const std::vector<std::uint8_t> bytes = net::EncodeSocialState(state, &error);
    if (bytes.empty()) {
        ZL_WARN("net", "社交状态编码失败：{}", error);
        return;
    }
    for (const auto& [sessionId, session] : m_sessions) {
        static_cast<void>(session);
        if (m_network->Send(sessionId, bytes, &error)) {
            ++m_networkStats.socialStatesSent;
        } else {
            ZL_WARN("net", "session={} 社交状态发送失败：{}", sessionId, error);
        }
    }
    m_socialDirty = false;
    m_lastSocialTick = m_ticks;
}
void ServerHost::SpawnDemoLoot() {
    if (m_demoLootSpawned || m_simulation == nullptr || m_items == nullptr) {
        return;
    }
    const int spawned = game::SpawnDemoGroundItems(
        m_simulation->Ecs(), *m_items, m_simulation->World(),
        m_options.playerTargetX, m_options.playerTargetY);
    m_demoLootSpawned = true;
    ZL_INFO("items", "服务器演示地面物品：{} 处（出生点附近）", spawned);
}

void ServerHost::TickCrafting(float dt) {
    if (m_inventory == nullptr || m_simulation == nullptr) {
        return;
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null) {
            continue;
        }
        game::InventoryResult result;
        if (!m_inventory->TickCrafting(player, dt, &result)) {
            continue;
        }
        session.stateDirty = true;
        if (result.ok && result.tone == game::InventoryTone::Ok) {
            SpawnCraftEffect(session.playerStableId);
        }
        if (!result.message.empty()) {
            SetSessionStatus(session, result.message,
                             static_cast<std::uint8_t>(result.tone));
        }
    }
}

bool ServerHost::FireProjectile(SessionPlayer& session, const net::Command& command) {
    if (m_simulation == nullptr) {
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const auto* position =
        player != entt::null ? registry.try_get<ecs::Position>(player) : nullptr;
    if (player == entt::null || position == nullptr) {
        return false;
    }

    float dirX = command.payload.x;
    float dirY = command.payload.y;
    if (command.payload.entityId != 0) {
        const ecs::Entity target =
            m_simulation->Ecs().FindById(command.payload.entityId);
        const auto* targetPosition =
            target != entt::null ? registry.try_get<ecs::Position>(target) : nullptr;
        const auto* health =
            target != entt::null ? registry.try_get<ecs::Health>(target) : nullptr;
        if (target == entt::null || !registry.all_of<ecs::AiState>(target) ||
            targetPosition == nullptr || health == nullptr ||
            health->current <= 0.0f) {
            return false;
        }
        dirX = targetPosition->x - position->x;
        dirY = targetPosition->y - position->y;
        const float distance = std::sqrt(dirX * dirX + dirY * dirY);
        if (distance > m_maxProjectileDistance) {
            ++m_networkStats.rangeRejected;
            return false;
        }
    }
    float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.0001f) {
        const auto* facing = registry.try_get<ecs::Facing>(player);
        const ecs::Direction dir =
            facing != nullptr ? facing->dir : ecs::Direction::Down;
        switch (dir) {
            case ecs::Direction::Down:
                dirX = 1.0f;
                dirY = 1.0f;
                break;
            case ecs::Direction::Left:
                dirX = -1.0f;
                dirY = 1.0f;
                break;
            case ecs::Direction::Right:
                dirX = 1.0f;
                dirY = -1.0f;
                break;
            case ecs::Direction::Up:
                dirX = -1.0f;
                dirY = -1.0f;
                break;
        }
        length = std::sqrt(dirX * dirX + dirY * dirY);
    }
    if (length < 0.0001f) {
        return false;
    }
    dirX /= length;
    dirY /= length;
    if (session.projectileUsed &&
        m_ticks < session.lastProjectileTick + m_projectileCooldownTicks) {
        ++m_networkStats.cooldownRejected;
        return false;
    }

    const ecs::Entity projectile = m_simulation->Ecs().Create();
    if (projectile == entt::null) {
        return false;
    }
    registry.emplace<ecs::Position>(
        projectile, ecs::Position{position->x + dirX * 0.35f,
                                  position->y + dirY * 0.35f, position->z});
    registry.emplace<ecs::Velocity>(
        projectile, ecs::Velocity{dirX * m_projectileSpeed, dirY * m_projectileSpeed});
    registry.emplace<ecs::SpriteRef>(
        projectile, ecs::SpriteRef{ecs::HashName("projectile.bullet"), 1,
                                   0xFFFFC040u});
    ecs::Projectile value;
    value.ownerStableId = session.playerStableId;
    value.lifetime = m_projectileLifetime;
    value.damage = m_projectileDamage;
    registry.emplace<ecs::Projectile>(projectile, value);

    session.projectileUsed = true;
    session.lastProjectileTick = m_ticks;
    auto& action = registry.get_or_emplace<ecs::ActionState>(player);
    action.action = ecs::ActorAction::Attacking;
    action.elapsed = 0.0f;
    action.duration = 0.28f;
    ++m_networkStats.projectilesFired;
    return true;
}

void ServerHost::SpawnImpactEffect(float x, float y, int z) {
    if (m_simulation == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity effect = m_simulation->Ecs().Create();
    if (effect == entt::null) {
        return;
    }
    registry.emplace<ecs::Position>(effect, ecs::Position{x, y, z});
    registry.emplace<ecs::SpriteRef>(
        effect, ecs::SpriteRef{ecs::HashName("effect.impact"), 2, 0xFF7040FFu});
    registry.emplace<ecs::TransientEffect>(effect, ecs::TransientEffect{0.35f});
    ++m_networkStats.effectsSpawned;
}

void ServerHost::SpawnCraftEffect(std::uint64_t playerStableId) {
    if (m_simulation == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity player = m_simulation->Ecs().FindById(playerStableId);
    const auto* position =
        player != entt::null ? registry.try_get<ecs::Position>(player) : nullptr;
    if (position == nullptr) {
        return;
    }
    SpawnImpactEffect(position->x, position->y, position->z);
}

void ServerHost::UpdateProjectiles(float dt) {
    if (m_simulation == nullptr || !(dt > 0.0f)) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    std::vector<ecs::Entity> destroy;
    struct Impact {
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
    };
    std::vector<Impact> impacts;

    registry.view<ecs::Projectile, ecs::Position, ecs::Velocity>().each(
        [&](const ecs::Entity projectile, ecs::Projectile& value,
            const ecs::Position& position, const ecs::Velocity& velocity) {
            value.lifetime -= dt;
            bool hit = false;
            Impact impact{position.x, position.y, position.z};

            const int wx = static_cast<int>(std::floor(position.x));
            const int wy = static_cast<int>(std::floor(position.y));
            if (value.lifetime <= 0.0f ||
                game::IsSquareBlocked(m_simulation->World(), wx, wy, position.z)) {
                hit = true;
            }

            ecs::Entity best = entt::null;
            float bestDistanceSq = 1.0e9f;
            if (!hit) {
                registry.view<ecs::Health, ecs::Position>().each(
                    [&](const ecs::Entity target, const ecs::Health& health,
                        const ecs::Position& targetPosition) {
                        if (target == projectile ||
                            (target ==
                                 m_simulation->Ecs().FindById(value.ownerStableId) &&
                             value.ownerStableId != 0) ||
                            health.current <= 0.0f ||
                            (!value.hitPlayers &&
                             registry.all_of<ecs::PlayerTag>(target))) {
                            return;
                        }
                        if (targetPosition.z != position.z) {
                            return;
                        }
                        const float dx = targetPosition.x - position.x;
                        const float dy = targetPosition.y - position.y;
                        const float distanceSq = dx * dx + dy * dy;
                        const float reach = value.radius + 0.35f;
                        if (distanceSq <= reach * reach &&
                            distanceSq < bestDistanceSq) {
                            bestDistanceSq = distanceSq;
                            best = target;
                        }
                    });
            }
            if (best != entt::null) {
                hit = true;
                const auto* targetPosition = registry.try_get<ecs::Position>(best);
                if (targetPosition != nullptr) {
                    impact = Impact{targetPosition->x, targetPosition->y,
                                    targetPosition->z};
                }
                if (m_simulation->Combat() != nullptr) {
                    const float length =
                        std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
                    ecs::DamageEvent event;
                    event.sourceId = value.ownerStableId;
                    event.type = ecs::DamageType::Blunt;
                    event.amount = value.damage;
                    event.dirX = length > 0.0001f ? velocity.x / length : 0.0f;
                    event.dirY = length > 0.0001f ? velocity.y / length : 0.0f;
                    event.knockback = value.knockback;
                    event.staggerSeconds = value.staggerSeconds;
                    if (m_simulation->Combat()->ApplyDamage(
                            m_simulation->Ecs(), best, event)) {
                        ++m_networkStats.projectilesHit;
                    }
                }
            }
            if (hit) {
                destroy.push_back(projectile);
                impacts.push_back(impact);
            }
        });

    for (const ecs::Entity projectile : destroy) {
        m_simulation->DespawnEntity(projectile);
    }
    for (const Impact& impact : impacts) {
        SpawnImpactEffect(impact.x, impact.y, impact.z);
    }
}

void ServerHost::UpdateTransientEffects(float dt) {
    if (m_simulation == nullptr || !(dt > 0.0f)) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    std::vector<ecs::Entity> destroy;
    registry.view<ecs::TransientEffect>().each(
        [&](const ecs::Entity effect, ecs::TransientEffect& value) {
            value.remaining -= dt;
            if (value.remaining <= 0.0f) {
                destroy.push_back(effect);
            }
        });
    for (const ecs::Entity effect : destroy) {
        m_simulation->DespawnEntity(effect);
    }
}

void ServerHost::CaptureSessionProfile(SessionPlayer& session, bool markDead) {
    if (!m_profilesEnabled || m_playerProfiles == nullptr ||
        session.playerStableId == 0) {
        return;
    }
    if (markDead) {
        session.dead = true;
    }
    PlayerProfile* profile = FindSessionProfile(session);
    if (profile == nullptr) {
        profile = &m_playerProfiles->Create(session.identity, session.playerName);
    }
    profile->name = session.playerName;
    profile->stableId = session.playerStableId;
    profile->alive = !session.dead;
    if (session.dead) {
        const std::uint64_t remainingTicks =
            session.respawnAtTick > m_ticks ? session.respawnAtTick - m_ticks : 0;
        profile->respawnSeconds = static_cast<float>(remainingTicks) /
                                  std::max(1.0f, m_options.tickRate);
        profile->state.health = 0.0f;
        profile->state.alive = false;
    } else {
        profile->respawnSeconds = 0.0f;
        profile->state.alive = true;
    }

    net::PlayerState state;
    FillPlayerState(session, &state);
    state.status.clear();
    state.statusSequence = 0;
    state.statusTone = 0;
    if (session.dead) {
        state.health = 0.0f;
    }
    profile->state = std::move(state);

    const ecs::Entity player = m_simulation->Ecs().FindById(session.playerStableId);
    if (player != entt::null) {
        save::SavedEntity snapshot;
        std::string captureError;
        if (save::CaptureEntity(m_simulation->Ecs(), player, *m_saveComponents,
                                &snapshot, &captureError)) {
            profile->entity = std::move(snapshot);
            profile->hasSnapshot = true;
        } else {
            ZL_WARN("server", "玩家档案快照失败：identity={}，原因={}",
                    session.identity, captureError);
        }
    }
}

void ServerHost::SavePlayerProfiles(const char* reason) {
    if (!m_profilesEnabled || m_playerProfiles == nullptr) {
        return;
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0) {
            continue;
        }
        CaptureSessionProfile(session, session.dead);
    }
    std::string saveError;
    if (!m_playerProfiles->Save(&saveError)) {
        ZL_WARN("server", "玩家档案保存失败（{}）：{}",
                reason != nullptr ? reason : "manual", saveError);
    } else {
        ++m_networkStats.profileSaves;
        ZL_DEBUG("server", "玩家档案已保存（{}，{} 个）",
                 reason != nullptr ? reason : "manual", m_playerProfiles->Size());
    }
    m_profileSaveTimer = 0.0;
}

void ServerHost::UpdatePlayerLifecycle() {
    if (!m_profilesEnabled || m_simulation == nullptr) {
        return;
    }
    bool changed = false;
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0 || session.dead || !session.spawned) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        bool died = false;
        if (player == entt::null) {
            died = true;
        } else if (const auto* health =
                       m_simulation->Ecs().Registry().try_get<ecs::Health>(player);
                   health != nullptr && health->current <= 0.0f) {
            died = true;
        }
        if (!died) {
            continue;
        }

        session.dead = true;
        session.respawnAtTick = m_ticks + m_respawnDelayTicks;
        session.stateDirty = true;
        ++m_networkStats.playerDeaths;
        CaptureSessionProfile(session, true);
        const double seconds = static_cast<double>(m_respawnDelayTicks) /
                               std::max(1.0f, m_options.tickRate);
        SetSessionStatus(session,
                         "你已死亡，" + std::to_string(static_cast<int>(
                                             std::ceil(seconds))) +
                             " 秒后复活",
                         2);
        changed = true;
        ZL_INFO("server", "玩家死亡：identity={}，stableId={}，{} 秒后复活",
                session.identity, session.playerStableId, seconds);
    }
    if (changed) {
        SavePlayerProfiles("death");
    }
}

bool ServerHost::RespawnSessionPlayer(SessionPlayer& session) {
    PlayerProfile* profile = FindSessionProfile(session);
    if (profile == nullptr || m_simulation == nullptr) {
        return false;
    }
    std::string respawnError;
    ecs::Entity player = entt::null;
    if (profile->hasSnapshot) {
        player = m_simulation->RespawnNetworkPlayer(
            profile->entity, m_options.playerTargetX, m_options.playerTargetY, 24, true,
            &respawnError);
    } else {
        player = m_simulation->SpawnNetworkPlayer(
            m_options.playerTargetX, m_options.playerTargetY, 24, profile->stableId);
    }
    if (player == entt::null) {
        ZL_WARN("server", "玩家复活暂不可用：identity={}，原因={}", session.identity,
                respawnError);
        return false;
    }

    session.playerStableId = m_simulation->Ecs().IdOf(player).id;
    session.spawned = true;
    session.dead = false;
    session.respawnAtTick = 0;
    session.needsFullSnapshot = true;
    session.stateDirty = true;
    session.lastStateTick = 0;
    session.attackUsed = false;
    session.projectileUsed = false;
    if (session.producer != nullptr) {
        session.producer->Reset();
    } else if (m_replicationRegistry != nullptr) {
        game::EntityReplicationProducer::Options producerOptions;
        producerOptions.aoiRadius = static_cast<float>(
            m_config->Get<double>("net.aoiRadius", 48.0));
        producerOptions.maxEntities = static_cast<std::size_t>(
            std::max(1, m_config->Get<int>("net.maxEntities", 256)));
        session.producer = std::make_unique<game::EntityReplicationProducer>(
            m_simulation->Ecs(), *m_replicationRegistry, producerOptions);
    }
    profile->alive = true;
    profile->respawnSeconds = 0.0f;
    ++m_networkStats.playerRespawns;
    SetSessionStatus(session, "已复活，生命已恢复", 1);
    CaptureSessionProfile(session, false);
    SavePlayerProfiles("respawn");
    ZL_INFO("server", "玩家复活：identity={}，stableId={}", session.identity,
            session.playerStableId);
    return true;
}

void ServerHost::EmitGunshot(std::uint64_t playerStableId) {
    if (m_events == nullptr || m_simulation == nullptr) {
        return;
    }
    const ecs::Entity player = m_simulation->Ecs().FindById(playerStableId);
    const auto* position =
        player != entt::null
            ? m_simulation->Ecs().Registry().try_get<ecs::Position>(player)
            : nullptr;
    if (position == nullptr) {
        return;
    }
    core::SoundEvent shot;
    shot.x = position->x;
    shot.y = position->y;
    shot.radius = m_gunshotRadius;
    shot.type = core::SoundType::Gunshot;
    m_events->EmitQueued(core::kOnSound, std::move(shot));
    ++m_networkStats.gunshots;
}

bool ServerHost::ExecuteSessionCommand(std::uint64_t sessionId, SessionPlayer& session,
                                       const net::Command& command,
                                       std::string* error) {
    static_cast<void>(sessionId);  // 仅 ZL_DEBUG 诊断日志使用
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    auto applyInventory = [&](const game::InventoryResult& result) {
        SetSessionStatus(session, result.message,
                         static_cast<std::uint8_t>(result.tone));
        return result.ok ? true : fail(result.message);
    };
    if (m_simulation == nullptr || session.playerStableId == 0) {
        return fail("会话玩家尚未生成");
    }
    if (session.dead) {
        return fail("玩家已死亡，等待复活");
    }
    if (m_inventory == nullptr) {
        return fail("服务器背包服务不可用");
    }
    if (command.playerStableId != session.playerStableId) {
        ++m_networkStats.ownershipRejected;
        return fail("命令 playerStableId 与会话不匹配");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return fail("权威玩家实体不存在");
    }
    if (!std::isfinite(command.payload.x) || !std::isfinite(command.payload.y)) {
        return fail("命令 payload 浮点非法");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* playerPos = registry.try_get<ecs::Position>(player);
    if (playerPos == nullptr) {
        return fail("玩家缺少位置");
    }

    switch (command.type) {
        case net::CommandType::Move: {
            float x = command.payload.x;
            float y = command.payload.y;
            const float length = std::sqrt(x * x + y * y);
            if (length > 1.0f) {
                x /= length;
                y /= length;
            }
            auto* move = registry.try_get<ecs::MoveInput>(player);
            if (move == nullptr) {
                move = &registry.emplace<ecs::MoveInput>(player);
            }
            move->x = x;
            move->y = y;
            return true;
        }
        case net::CommandType::MoveTo: {
            const float dx = command.payload.x - playerPos->x;
            const float dy = command.payload.y - playerPos->y;
            if (dx * dx + dy * dy >
                m_maxMoveToDistance * m_maxMoveToDistance) {
                ++m_networkStats.rangeRejected;
                return fail("MoveTo 目标超出服务器允许距离");
            }
            auto* request = registry.try_get<ecs::PathRequest>(player);
            if (request == nullptr) {
                request = &registry.emplace<ecs::PathRequest>(player);
            }
            request->goalX = static_cast<int>(std::lround(command.payload.x));
            request->goalY = static_cast<int>(std::lround(command.payload.y));
            request->newRequest = true;
            return true;
        }
        case net::CommandType::Attack: {
            auto* attack = registry.try_get<ecs::AttackIntent>(player);
            if (attack == nullptr) {
                return fail("玩家缺少攻击组件");
            }
            ecs::Entity best = entt::null;
            float bestDistSq = m_maxAttackDistance * m_maxAttackDistance;
            if (command.payload.entityId != 0) {
                best = m_simulation->Ecs().FindById(command.payload.entityId);
                const auto* targetPos =
                    best != entt::null ? registry.try_get<ecs::Position>(best) : nullptr;
                const auto* health =
                    best != entt::null ? registry.try_get<ecs::Health>(best) : nullptr;
                if (best == entt::null || !registry.all_of<ecs::AiState>(best) ||
                    targetPos == nullptr || health == nullptr || health->current <= 0.0f) {
                    return fail("攻击目标不可用");
                }
                const float dx = targetPos->x - playerPos->x;
                const float dy = targetPos->y - playerPos->y;
                bestDistSq = dx * dx + dy * dy;
                if (bestDistSq > m_maxAttackDistance * m_maxAttackDistance) {
                    ++m_networkStats.rangeRejected;
                    return fail("攻击目标超出范围");
                }
            } else {
                registry.view<ecs::AiState, ecs::Health, ecs::Position>().each(
                    [&](const ecs::Entity entity, const ecs::AiState&,
                        const ecs::Health& health, const ecs::Position& pos) {
                        if (!m_simulation->Ecs().Alive(entity) || health.current <= 0.0f) {
                            return;
                        }
                        const float dx = pos.x - playerPos->x;
                        const float dy = pos.y - playerPos->y;
                        const float distSq = dx * dx + dy * dy;
                        if (distSq < bestDistSq) {
                            bestDistSq = distSq;
                            best = entity;
                        }
                    });
            }
            if (best == entt::null) {
                return fail("攻击范围内没有目标");
            }
            const auto* targetPos = registry.try_get<ecs::Position>(best);
            if (targetPos == nullptr) {
                return fail("攻击目标缺少位置");
            }
            if (session.attackUsed &&
                m_ticks < session.lastAttackTick + m_attackCooldownTicks) {
                ++m_networkStats.cooldownRejected;
                return fail("攻击冷却中");
            }
            float dx = targetPos->x - playerPos->x;
            float dy = targetPos->y - playerPos->y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.0001f) {
                dx /= length;
                dy /= length;
            } else {
                dx = 1.0f;
                dy = 0.0f;
            }
            attack->targetId = m_simulation->Ecs().IdOf(best).id;
            attack->dirX = dx;
            attack->dirY = dy;
            attack->damage = m_attackDamage;
            attack->range = m_attackRange;
            attack->knockback = m_attackKnockback;
            attack->staggerSeconds = m_attackStagger;
            attack->arcDegrees = 100.0f;
            attack->type = ecs::DamageType::Blunt;
            attack->pending = true;
            session.attackUsed = true;
            session.lastAttackTick = m_ticks;
            ZL_DEBUG("net", "session={} 攻击目标 stableId={}（距离 {:.2f}）", sessionId,
                     attack->targetId, static_cast<double>(std::sqrt(bestDistSq)));
            return true;
        }
        case net::CommandType::PickupNearest:
            return applyInventory(m_inventory->PickupNearest(player));
        case net::CommandType::PickupEntity:
            return applyInventory(
                m_inventory->Pickup(player, command.payload.entityId));
        case net::CommandType::UseBag:
            return applyInventory(m_inventory->UseBag(player, command.payload.index));
        case net::CommandType::EquipBag: {
            const int slot = command.payload.index < 0
                                 ? -1
                                 : (command.payload.targetIndex >= 0 &&
                                            command.payload.targetIndex <
                                                static_cast<int>(
                                                    ecs::kEquipSlotCount)
                                        ? command.payload.targetIndex
                                        : static_cast<int>(
                                              ecs::EquipSlot::PrimaryHand));
            return applyInventory(
                m_inventory->EquipBag(player, command.payload.index, slot));
        }
        case net::CommandType::Unequip:
            return applyInventory(
                m_inventory->Unequip(player, command.payload.index));
        case net::CommandType::DropBag:
            return applyInventory(
                m_inventory->DropBag(player, command.payload.index));
        case net::CommandType::DropEquipment:
            return applyInventory(
                m_inventory->DropEquipment(player, command.payload.index));
        case net::CommandType::InventoryAction: {
            const auto actionType =
                static_cast<ui::InventoryActionType>(command.payload.flags & 0xFFu);
            const int count = static_cast<int>(command.payload.flags >> 8);
            if (count > 99 ||
                command.payload.area >
                    static_cast<std::uint8_t>(ui::InventoryArea::Loot) ||
                command.payload.targetArea >
                    static_cast<std::uint8_t>(ui::InventoryArea::Loot) ||
                static_cast<std::uint8_t>(actionType) >
                    static_cast<std::uint8_t>(ui::InventoryActionType::QueueSetCount)) {
                return fail("背包命令字段非法");
            }
            ui::InventoryAction action;
            action.type = actionType;
            action.from = static_cast<ui::InventoryArea>(command.payload.area);
            action.to =
                static_cast<ui::InventoryArea>(command.payload.targetArea);
            action.fromIndex = command.payload.index;
            action.toIndex = command.payload.targetIndex;
            action.fromEntityId = command.payload.entityId;
            action.count = count > 0 ? count : 1;
            return applyInventory(m_inventory->Dispatch(player, action));
        }
        case net::CommandType::Gunshot:
            EmitGunshot(session.playerStableId);
            SetSessionStatus(session, "开了一枪", 0);
            return true;
        case net::CommandType::FireProjectile:
            if (!FireProjectile(session, command)) {
                return fail("投射物发射失败（冷却/目标/方向无效）");
            }
            SetSessionStatus(session, "发射投射物", 0);
            return true;
        case net::CommandType::SelectHotbar:
            if (command.payload.index < 0 || command.payload.index >= 5) {
                return fail("快捷栏索引越界");
            }
            return true;  // 纯客户端 UI 状态；服务器无对应世界状态
        default:
            ++m_networkStats.unsupportedCommands;
            return fail("服务器暂未实现该命令");
    }
}

void ServerHost::RemoveSession(std::uint64_t sessionId, std::uint8_t reason,
                               const std::string& message) {
    const auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) {
        return;
    }
    if (m_simulation != nullptr && it->second.playerStableId != 0) {
        CaptureSessionProfile(it->second, it->second.dead);
        const ecs::Entity player =
            m_simulation->Ecs().FindById(it->second.playerStableId);
        if (player != entt::null) {
            m_simulation->DespawnEntity(player);
        }
    }
    SavePlayerProfiles("disconnect");
    m_socialDirty = true;
    ZL_INFO("net", "客户端离开：session={}，原因={}{}（剩余 {} 人）", sessionId,
            net::SessionDisconnectReasonName(
                static_cast<net::SessionDisconnectReason>(reason)),
            message.empty() ? "" : "：“" + message + "”", m_sessions.size() - 1);
    m_sessions.erase(it);
}

std::uint16_t ServerHost::NetworkPort() const {
    return m_network != nullptr ? m_network->Port() : 0;
}

void ServerHost::LogStatus() {
    if (m_simulation == nullptr) {
        return;
    }
    const auto& stats = m_simulation->Chunks().GetStats();
    const ecs::Entity player = m_simulation->Player();
    glm::vec2 playerPosition{0.0f, 0.0f};
    if (player != entt::null) {
        const auto& position = m_simulation->Ecs().Registry().get<ecs::Position>(player);
        playerPosition = glm::vec2(position.x, position.y);
    }
    ZL_INFO("server", "tick={} 实体={} chunk={} loading={} 玩家={:.1f},{:.1f} 会话={}", m_ticks,
            m_simulation->Ecs().AliveCount(), stats.loadedChunks, stats.loadingChunks,
            playerPosition.x, playerPosition.y,
            m_networkEnabled ? m_sessions.size() : 0);
    if (m_networkEnabled && m_network != nullptr) {
        const net::KcpSessionServerStats sessionStats = m_network->GetStats();
        ZL_INFO("net",
                "服务器网络: 收包={} 坏包={} 超大={} 过期={} | 命令 收/受/拒={}/{}/{} "
                "限频={} 冷却={} 越权={} 距离={} 未实现={} | 分配={} 快照={} 失败={} "
                "状态 发/失败={}/{} 社交={} 聊天 收/拒={}/{} 枪声={} 投射物 发/中={}/{} | 档案 恢复/保存/死亡/复活={}/{}/{}/{} | KCP 数据报 收/发={}/{} 拒绝={}",
                m_networkStats.dataPacketsReceived, m_networkStats.malformedPackets,
                m_networkStats.oversizedPackets, m_networkStats.stalePackets,
                m_networkStats.commandsReceived, m_networkStats.commandsAccepted,
                m_networkStats.commandsRejected, m_networkStats.rateLimitedCommands,
                m_networkStats.cooldownRejected, m_networkStats.ownershipRejected,
                m_networkStats.rangeRejected, m_networkStats.unsupportedCommands,
                m_networkStats.assignmentsSent, m_networkStats.snapshotsSent,
                m_networkStats.snapshotFailures, m_networkStats.playerStatesSent,
                m_networkStats.playerStateFailures, m_networkStats.socialStatesSent,
                m_networkStats.chatMessagesReceived, m_networkStats.chatMessagesRejected,
                m_networkStats.gunshots, m_networkStats.projectilesFired,
                m_networkStats.projectilesHit,
                m_networkStats.profileRestores, m_networkStats.profileSaves,
                m_networkStats.playerDeaths, m_networkStats.playerRespawns,
                sessionStats.transport.datagramsReceived,
                sessionStats.transport.datagramsSent,
                sessionStats.transport.datagramsRejected);
    }
}

}  // namespace zl::server
