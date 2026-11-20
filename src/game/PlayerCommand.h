#pragma once

#include <string>

#include "net/Command.h"

namespace zl::game {

struct GameContext;

// M35-F3-T1：脚本写动作的统一路由结果。
struct PlayerCommandResult {
    bool ok = false;
    bool queued = false;
    std::string error;
};

// 按 direct/loopback/KCP 路由一条已填好 type/payload 的玩家命令。
// 调用方负责校验玩家与 StableId；本函数不判断脚本来源。
PlayerCommandResult SubmitPlayerCommand(const GameContext& ctx, bool networkMode,
                                        const net::Command& command);

}  // namespace zl::game
