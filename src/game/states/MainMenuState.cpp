#include "game/states/MainMenuState.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/GameTime.h"
#include "core/Log.h"
#include "game/ServerList.h"
#include "net/SocialProtocol.h"
#include "game/states/WorldDemoState.h"
#include "platform/Window.h"
#include "save/SaveManager.h"
#include "ui/UiSystem.h"

namespace zl::game {
namespace {

std::string Trim(std::string value) {
    const std::size_t begin = value.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = value.find_last_not_of(" \t");
    return value.substr(begin, end - begin + 1);
}

bool ValidPlayerName(const std::string& name) {
    return !name.empty() && name.size() <= 48 && net::IsValidChatText(name);
}

bool ValidHost(const std::string& host) {
    return !host.empty() && host.size() <= 255 &&
           std::none_of(host.begin(), host.end(), [](unsigned char value) {
               return value <= 0x20u || value == 0x7Fu || value == '/' ||
                      value == '\\';
           });
}

std::string ServerName(const std::string& host, std::uint16_t port) {
    return host + ":" + std::to_string(port);
}

}  // namespace

void MainMenuState::OnEnter() {
    ZL_INFO("ui", "进入主菜单");
    if (m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetSpeed(core::GameTime::Speed::Paused);
    }

    m_latestSaveId.clear();
    bool canContinue = false;
    if (m_ctx.saveManager != nullptr) {
        std::vector<save::SaveMetadata> saves;
        std::string saveError;
        if (m_ctx.saveManager->List(&saves, &saveError)) {
            if (!saves.empty()) {
                m_latestSaveId = saves.front().id;
                canContinue = true;
                ZL_INFO("save", "主菜单发现存档：{}（{} 个）", m_latestSaveId,
                        saves.size());
            }
        } else {
            ZL_WARN("save", "扫描存档失败：{}", saveError);
        }
    }

    const std::string serversFile =
        m_ctx.config != nullptr
            ? m_ctx.config->Get<std::string>("net.serversFile", "saves/servers.json")
            : std::string("saves/servers.json");
    m_serverList = std::make_unique<ServerListStore>(serversFile);
    std::string serverListError;
    if (!m_serverList->Load(&serverListError)) {
        ZL_WARN("net", "{}", serverListError);
    }
    m_multiplayerState = {};
    m_multiplayerState.playerName =
        m_ctx.config != nullptr
            ? m_ctx.config->Get<std::string>("net.playerName", "Player")
            : std::string("Player");
    m_multiplayerState.joinHost =
        m_ctx.config != nullptr
            ? m_ctx.config->Get<std::string>("net.host", "127.0.0.1")
            : std::string("127.0.0.1");
    const int joinPort =
        m_ctx.config != nullptr ? m_ctx.config->Get<int>("net.port", 7777) : 7777;
    m_multiplayerState.joinPort =
        static_cast<std::uint16_t>(std::clamp(joinPort, 1, 65535));
    m_multiplayerState.hostPort = m_multiplayerState.joinPort;
    m_multiplayerState.status = "选择加入服务器，或直接开始主机";
    m_multiplayerState.statusTone = "info";

    if (m_ctx.ui == nullptr) {
        if (m_ctx.states != nullptr) {
            m_ctx.states->SetState(std::make_unique<WorldDemoState>(m_ctx));
        }
        return;
    }

    m_ctx.ui->CloseInventory();
    m_ctx.ui->SetMenuActionHandler(
        [this](ui::MenuActionType action) { HandleMenuAction(action); });
    m_ctx.ui->ShowMainMenu(canContinue);
    if (!m_ctx.ui->IsMenuOpen()) {
        ZL_WARN("ui", "主菜单不可用（assets/ui/menu.rml 加载失败），直接进入世界");
        if (m_ctx.states != nullptr) {
            m_ctx.states->SetState(std::make_unique<WorldDemoState>(m_ctx));
        }
    }
}

void MainMenuState::OnExit() {
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->HideMultiplayerMenu();
        m_ctx.ui->HideMenu();
        m_ctx.ui->SetMenuActionHandler({});
        m_ctx.ui->SetMultiplayerActionHandler({});
    }
    if (m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetSpeed(core::GameTime::Speed::X1);
    }
}

void MainMenuState::OnTick(double tickDt) {
    (void)tickDt;
}

void MainMenuState::OnFrame(double frameDt, double alpha) {
    (void)frameDt;
    (void)alpha;
}

void MainMenuState::ShowMultiplayer() {
    if (m_ctx.ui == nullptr) {
        return;
    }
    m_multiplayerState.playerName = Trim(m_multiplayerState.playerName);
    if (m_multiplayerState.playerName.empty()) {
        m_multiplayerState.playerName = "Player";
    }
    m_multiplayerState.servers.clear();
    if (m_serverList != nullptr) {
        for (const ServerListEntry& entry : m_serverList->Entries()) {
            m_multiplayerState.servers.push_back(
                ui::MultiplayerServerUi{entry.name, entry.host, entry.port});
        }
    }
    m_ctx.ui->SetMultiplayerActionHandler(
        [this](ui::MultiplayerActionType action, const ui::MultiplayerMenuState& state) {
            HandleMultiplayerAction(action, state);
        });
    m_ctx.ui->ShowMultiplayerMenu(m_multiplayerState);
}

void MainMenuState::HideMultiplayer() {
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->HideMultiplayerMenu();
        m_ctx.ui->ShowMainMenu(!m_latestSaveId.empty());
    }
}

void MainMenuState::RefreshServerList() {
    if (m_serverList == nullptr) {
        return;
    }
    std::string error;
    if (!m_serverList->Load(&error)) {
        SetMultiplayerStatus(error, "fail");
        return;
    }
    m_multiplayerState.servers.clear();
    for (const ServerListEntry& entry : m_serverList->Entries()) {
        m_multiplayerState.servers.push_back(
            ui::MultiplayerServerUi{entry.name, entry.host, entry.port});
    }
    SetMultiplayerStatus("服务器列表已刷新", "ok");
}

bool MainMenuState::SaveServerList() {
    if (m_serverList == nullptr) {
        return false;
    }
    std::string error;
    if (!m_serverList->Save(&error)) {
        ZL_WARN("net", "保存服务器列表失败：{}", error);
        return false;
    }
    return true;
}

void MainMenuState::SetMultiplayerStatus(std::string text, std::string tone) {
    m_multiplayerState.status = std::move(text);
    m_multiplayerState.statusTone = std::move(tone);
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->SetMultiplayerState(m_multiplayerState);
    }
}

void MainMenuState::HandleMultiplayerAction(ui::MultiplayerActionType action,
                                            const ui::MultiplayerMenuState& state) {
    m_multiplayerState = state;
    m_multiplayerState.playerName = Trim(m_multiplayerState.playerName);
    m_multiplayerState.joinHost = Trim(m_multiplayerState.joinHost);

    if (action == ui::MultiplayerActionType::Back) {
        HideMultiplayer();
        return;
    }
    if (action == ui::MultiplayerActionType::RefreshServers) {
        RefreshServerList();
        return;
    }
    if (!ValidPlayerName(m_multiplayerState.playerName)) {
        SetMultiplayerStatus("玩家名称需为 1~48 个可见字符", "fail");
        return;
    }
    if (m_ctx.config != nullptr) {
        m_ctx.config->Set("net.playerName", m_multiplayerState.playerName);
    }

    if (action == ui::MultiplayerActionType::AddServer) {
        if (!ValidHost(m_multiplayerState.joinHost) ||
            m_multiplayerState.joinPort == 0) {
            SetMultiplayerStatus("服务器地址或端口非法", "fail");
            return;
        }
        if (m_serverList == nullptr ||
            !m_serverList->Upsert(
                ServerName(m_multiplayerState.joinHost,
                           m_multiplayerState.joinPort),
                m_multiplayerState.joinHost, m_multiplayerState.joinPort)) {
            SetMultiplayerStatus("收藏服务器失败", "fail");
            return;
        }
        if (!SaveServerList()) {
            SetMultiplayerStatus("服务器已收藏，但写入列表文件失败", "fail");
            return;
        }
        RefreshServerList();
        SetMultiplayerStatus("已收藏服务器地址", "ok");
        return;
    }

    if (action == ui::MultiplayerActionType::Join) {
        if (!ValidHost(m_multiplayerState.joinHost) ||
            m_multiplayerState.joinPort == 0) {
            SetMultiplayerStatus("服务器地址或端口非法", "fail");
            return;
        }
        if (!m_ctx.startJoining) {
            SetMultiplayerStatus("联机客户端不可用", "fail");
            return;
        }
        MultiplayerRequest request;
        request.host = m_multiplayerState.joinHost;
        request.port = m_multiplayerState.joinPort;
        request.playerName = m_multiplayerState.playerName;
        MultiplayerStartResult result;
        std::string error;
        if (!m_ctx.startJoining(request, result, error)) {
            SetMultiplayerStatus(error.empty() ? "加入服务器失败" : error, "fail");
            return;
        }
        std::string name = ServerName(request.host, request.port);
        if (m_serverList != nullptr) {
            for (const ServerListEntry& entry : m_serverList->Entries()) {
                if (entry.host == request.host && entry.port == request.port) {
                    name = entry.name;
                    break;
                }
            }
            (void)m_serverList->Upsert(name, request.host, request.port);
            (void)SaveServerList();
        }
        ZL_INFO("net", "联机入口：加入 {}:{}（名称 {}）", request.host, request.port,
                request.playerName);
        if (m_ctx.ui != nullptr) {
            m_ctx.ui->HideMultiplayerMenu();
        }
        if (m_ctx.states != nullptr) {
            m_ctx.states->SetState(std::make_unique<WorldDemoState>(m_ctx));
        }
        return;
    }

    if (action == ui::MultiplayerActionType::Host) {
        if (!m_ctx.startHosting) {
            SetMultiplayerStatus("内嵌主机不可用", "fail");
            return;
        }
        MultiplayerRequest request;
        request.host = "0.0.0.0";
        request.port = m_multiplayerState.hostPort;
        request.playerName = m_multiplayerState.playerName;
        MultiplayerStartResult result;
        std::string error;
        if (!m_ctx.startHosting(request, result, error)) {
            SetMultiplayerStatus(error.empty() ? "启动主机失败" : error, "fail");
            return;
        }
        if (m_serverList != nullptr) {
            (void)m_serverList->Upsert(
                "本机主机 " + std::to_string(result.serverPort), "127.0.0.1",
                result.serverPort);
            (void)SaveServerList();
        }
        ZL_INFO("net", "联机入口：开始主机，UDP 端口 {}，名称 {}", result.serverPort,
                request.playerName);
        if (m_ctx.ui != nullptr) {
            m_ctx.ui->HideMultiplayerMenu();
        }
        if (m_ctx.states != nullptr) {
            m_ctx.states->SetState(std::make_unique<WorldDemoState>(m_ctx));
        }
    }
}

void MainMenuState::HandleMenuAction(ui::MenuActionType action) {
    switch (action) {
        case ui::MenuActionType::Multiplayer:
            ZL_INFO("ui", "主菜单：联机游戏");
            ShowMultiplayer();
            break;
        case ui::MenuActionType::NewGame:
            ZL_INFO("ui", "主菜单：新游戏");
            m_ctx.activeSaveId.clear();
            m_ctx.saveWarning.clear();
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
        case ui::MenuActionType::Continue:
            if (!m_latestSaveId.empty() && m_ctx.states != nullptr) {
                ZL_INFO("save", "主菜单：继续存档 {}", m_latestSaveId);
                if (m_ctx.gameTime != nullptr) {
                    m_ctx.gameTime->SetSpeed(core::GameTime::Speed::X1);
                }
                m_ctx.states->SetState(
                    std::make_unique<WorldDemoState>(m_ctx, m_latestSaveId));
            }
            break;
        case ui::MenuActionType::Resume:
        case ui::MenuActionType::ReturnToMainMenu:
            break;
    }
}

}  // namespace zl::game
