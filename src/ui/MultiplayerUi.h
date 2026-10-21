#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace zl::ui {

enum class MultiplayerActionType {
    Host,
    Join,
    AddServer,
    RefreshServers,
    Back,
};

struct MultiplayerServerUi {
    std::string name;
    std::string host;
    std::uint16_t port = 7777;
};

struct MultiplayerMenuState {
    std::string playerName = "Player";
    std::string joinHost = "127.0.0.1";
    std::uint16_t joinPort = 7777;
    std::uint16_t hostPort = 7777;
    std::vector<MultiplayerServerUi> servers;
    std::string status;
    std::string statusTone = "info";
};

using MultiplayerActionHandler =
    std::function<void(MultiplayerActionType, const MultiplayerMenuState&)>;

}  // namespace zl::ui
