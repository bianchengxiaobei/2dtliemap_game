#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "net/Command.h"
#include "net/CommandCodec.h"
#include "net/Loopback.h"

namespace zl::net {

// M12-T2：直连 / 环回两种命令通路的统一入口。
//
//   direct   : Submit 立即调用 handler（默认；无序列化/无队列）
//   loopback : Submit 入 Client 批 → Flush 编码发包 → 下一 tick Server Pump 解码执行
//
// 两种模式使用同一 Command 结构与 handler；行为一致性测试只比较命令序列与执行结果。
class CommandPipeline {
public:
    using CommandHandler = LoopbackServer::CommandHandler;
    using SnapshotProvider = LoopbackServer::SnapshotProvider;

    struct Options {
        TransportMode mode = TransportMode::Direct;
        std::size_t maxCommandsPerPacket = 32;
        std::size_t maxPacketsPerPump = 64;
        std::size_t maxQueuedPackets = 256;
    };

    struct Stats {
        TransportMode mode = TransportMode::Direct;
        std::uint64_t commandsSubmitted = 0;
        std::uint64_t commandsExecuted = 0;
        std::uint64_t commandsRejected = 0;
        std::uint64_t packetsSent = 0;
        std::uint64_t packetsReceived = 0;
        std::uint64_t packetsRejected = 0;
        std::uint64_t snapshotsReceived = 0;
        std::uint64_t bytesSent = 0;
        std::uint64_t bytesReceived = 0;
        std::size_t queuedCommands = 0;
        std::string lastError;
    };

    explicit CommandPipeline(Options options = {});
    ~CommandPipeline();

    CommandPipeline(const CommandPipeline&) = delete;
    CommandPipeline& operator=(const CommandPipeline&) = delete;

    void SetHandler(CommandHandler handler);
    void SetSnapshotProvider(SnapshotProvider provider);

    // direct：立即执行；loopback：入批（达到 maxCommandsPerPacket 时自动 Flush）。
    bool Submit(const Command& command, std::string* error = nullptr);

    // 主循环渲染帧末调用：把 loopback 批编码为一个包投递给服务器。
    bool Flush(std::string* error = nullptr);

    // 固定 tick 开始调用：服务器消费队列中的包并回快照（direct 为空操作）。
    void Pump();

    // 状态切换时丢弃未发送的命令与未消费的快照（不清统计）。
    void Clear();

    TransportMode Mode() const { return m_options.mode; }
    bool HasSnapshot() const { return m_hasSnapshot; }
    const ServerSnapshot& LastSnapshot() const { return m_lastSnapshot; }
    Stats GetStats() const;

private:
    bool Execute(const Command& command, std::string& error);
    static bool Validate(const Command& command, std::string& error);

    Options m_options;
    CommandHandler m_handler;
    SnapshotProvider m_snapshotProvider;

    std::unique_ptr<LoopbackTransport> m_transport;
    std::unique_ptr<LoopbackClient> m_client;
    std::unique_ptr<LoopbackServer> m_server;

    std::vector<Command> m_batch;
    std::string m_lastError;
    ServerSnapshot m_lastSnapshot;
    bool m_hasSnapshot = false;

    std::uint64_t m_submitted = 0;
    std::uint64_t m_directExecuted = 0;
    std::uint64_t m_directRejected = 0;
};

}  // namespace zl::net
