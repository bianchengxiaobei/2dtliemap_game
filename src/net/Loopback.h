#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "net/Command.h"
#include "net/CommandCodec.h"

namespace zl::net {

// M12-T2：单进程环回传输。队列只存放已编码字节包，便于以后替换成 socket/pipe；
// 当前所有调用都在主线程，不做跨线程同步（联机实装时由 transport 层负责）。
class LoopbackTransport {
public:
    explicit LoopbackTransport(std::size_t maxQueuedPackets = 256);

    bool SendToServer(std::vector<std::uint8_t> packet);
    bool ReceiveOnServer(std::vector<std::uint8_t>* packet);

    bool SendToClient(std::vector<std::uint8_t> packet);
    bool ReceiveOnClient(std::vector<std::uint8_t>* packet);

    std::size_t ToServerQueued() const { return m_toServer.size(); }
    std::size_t ToClientQueued() const { return m_toClient.size(); }
    void Clear();

private:
    std::deque<std::vector<std::uint8_t>> m_toServer;
    std::deque<std::vector<std::uint8_t>> m_toClient;
    std::size_t m_maxQueuedPackets = 256;
};

struct LoopbackClientStats {
    std::uint64_t packetsSent = 0;
    std::uint64_t commandsSent = 0;
    std::uint64_t bytesSent = 0;
    std::uint64_t snapshotsReceived = 0;
    std::uint64_t snapshotBytesReceived = 0;
    std::uint64_t sendFailures = 0;
    std::uint64_t decodeFailures = 0;
};

class LoopbackClient {
public:
    LoopbackClient(LoopbackTransport& transport, std::size_t maxCommandsPerPacket = 32);

    bool Send(const std::vector<Command>& commands, std::string* error = nullptr);
    bool PollSnapshot(ServerSnapshot* snapshot, std::string* error = nullptr);

    const LoopbackClientStats& GetStats() const { return m_stats; }
    std::uint32_t NextSequence() const { return m_sequence; }

private:
    LoopbackTransport& m_transport;
    std::size_t m_maxCommandsPerPacket;
    std::uint32_t m_sequence = 1;
    LoopbackClientStats m_stats;
};

struct LoopbackServerStats {
    std::uint64_t packetsReceived = 0;
    std::uint64_t packetsRejected = 0;
    std::uint64_t commandsReceived = 0;
    std::uint64_t commandsExecuted = 0;
    std::uint64_t commandsRejected = 0;
    std::uint64_t snapshotsSent = 0;
    std::uint64_t snapshotFailures = 0;
    std::string lastError;
};

class LoopbackServer {
public:
    using CommandHandler = std::function<bool(const Command&, std::string&)>;
    using SnapshotProvider = std::function<ServerSnapshot()>;

    LoopbackServer(LoopbackTransport& transport, CommandHandler handler,
                   SnapshotProvider snapshotProvider = {}, std::size_t maxPacketsPerPump = 64);

    void SetHandler(CommandHandler handler) { m_handler = std::move(handler); }
    void SetSnapshotProvider(SnapshotProvider provider) {
        m_snapshotProvider = std::move(provider);
    }

    // 解包 → 逐条执行 → 回一帧快照；返回本次处理的包数。
    std::size_t Pump();

    const LoopbackServerStats& GetStats() const { return m_stats; }

private:
    void SendSnapshot();

    LoopbackTransport& m_transport;
    CommandHandler m_handler;
    SnapshotProvider m_snapshotProvider;
    std::size_t m_maxPacketsPerPump;
    LoopbackServerStats m_stats;
};

}  // namespace zl::net
