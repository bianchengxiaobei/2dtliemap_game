#include "game/PlayerCommand.h"

#include "core/Log.h"
#include "game/GameContext.h"
#include "game/NetworkClient.h"
#include "net/CommandPipeline.h"

namespace zl::game {

PlayerCommandResult SubmitPlayerCommand(const GameContext& ctx, bool networkMode,
                                        const net::Command& command) {
    PlayerCommandResult result;
    if (networkMode) {
        if (ctx.networkClient == nullptr) {
            result.error = "网络客户端不可用";
            return result;
        }
        if (!ctx.networkClient->SendCommand(command, &result.error)) {
            if (result.error.empty()) {
                result.error = "网络命令发送失败";
            }
            ZL_DEBUG("net", "玩家命令 {} 未发送：{}", net::CommandTypeName(command.type),
                     result.error);
            return result;
        }
        result.ok = true;
        result.queued = true;
        return result;
    }

    if (ctx.commands == nullptr) {
        result.error = "命令管线不可用";
        return result;
    }
    const bool direct = ctx.commands->Mode() == net::TransportMode::Direct;
    if (!ctx.commands->Submit(command, &result.error)) {
        if (result.error.empty()) {
            result.error = "玩家命令提交失败";
        }
        ZL_WARN("net", "玩家命令 {} 未提交：{}", net::CommandTypeName(command.type),
                result.error);
        return result;
    }
    result.ok = true;
    result.queued = !direct;
    return result;
}

}  // namespace zl::game
