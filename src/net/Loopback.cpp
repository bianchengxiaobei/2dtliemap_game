#include "net/Loopback.h"

#include <utility>

namespace zl::net {

namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

}  // namespace

LoopbackTransport::LoopbackTransport(std::size_t maxQueuedPackets)
    : m_maxQueuedPackets(maxQueuedPackets == 0 ? 1 : maxQueuedPackets) {}

bool LoopbackTransport::SendToServer(std::vector<std::uint8_t> packet) {
    if (m_toServer.size() >= m_maxQueuedPackets) {
        return false;
    }
    m_toServer.push_back(std::move(packet));
    return true;
}

bool LoopbackTransport::ReceiveOnServer(std::vector<std::uint8_t>* packet) {
    if (packet == nullptr || m_toServer.empty()) {
        return false;
    }
    *packet = std::move(m_toServer.front());
    m_toServer.pop_front();
    return true;
}

bool LoopbackTransport::SendToClient(std::vector<std::uint8_t> packet) {
    if (m_toClient.size() >= m_maxQueuedPackets) {
        return false;
    }
    m_toClient.push_back(std::move(packet));
    return true;
}

bool LoopbackTransport::ReceiveOnClient(std::vector<std::uint8_t>* packet) {
    if (packet == nullptr || m_toClient.empty()) {
        return false;
    }
    *packet = std::move(m_toClient.front());
    m_toClient.pop_front();
    return true;
}

void LoopbackTransport::Clear() {
    m_toServer.clear();
    m_toClient.clear();
}

LoopbackClient::LoopbackClient(LoopbackTransport& transport,
                               std::size_t maxCommandsPerPacket)
    : m_transport(transport),
      m_maxCommandsPerPacket(maxCommandsPerPacket == 0 ? 1 : maxCommandsPerPacket) {}

bool LoopbackClient::Send(const std::vector<Command>& commands, std::string* error) {
    if (commands.empty()) {
        return true;
    }
    if (commands.size() > m_maxCommandsPerPacket) {
        ++m_stats.sendFailures;
        SetError(error, "单包命令数超过客户端上限");
        return false;
    }
    CommandPacket packet;
    packet.sequence = m_sequence;
    packet.commands = commands;
    std::vector<std::uint8_t> bytes = EncodeCommandPacket(packet, error);
    if (bytes.empty()) {
        ++m_stats.sendFailures;
        return false;
    }
    if (!m_transport.SendToServer(std::move(bytes))) {
        ++m_stats.sendFailures;
        SetError(error, "环回服务器队列已满");
        return false;
    }
    ++m_sequence;
    ++m_stats.packetsSent;
    m_stats.commandsSent += commands.size();
    m_stats.bytesSent += kCommandPacketHeaderSize + commands.size() * kCommandWireSize;
    return true;
}

bool LoopbackClient::PollSnapshot(ServerSnapshot* snapshot, std::string* error) {
    if (snapshot == nullptr) {
        SetError(error, "输出 ServerSnapshot 为空");
        return false;
    }
    std::vector<std::uint8_t> bytes;
    if (!m_transport.ReceiveOnClient(&bytes)) {
        return false;
    }
    if (!DecodeServerSnapshot(bytes, snapshot, error)) {
        ++m_stats.decodeFailures;
        return false;
    }
    ++m_stats.snapshotsReceived;
    m_stats.snapshotBytesReceived += bytes.size();
    return true;
}

LoopbackServer::LoopbackServer(LoopbackTransport& transport, CommandHandler handler,
                               SnapshotProvider snapshotProvider,
                               std::size_t maxPacketsPerPump)
    : m_transport(transport),
      m_handler(std::move(handler)),
      m_snapshotProvider(std::move(snapshotProvider)),
      m_maxPacketsPerPump(maxPacketsPerPump == 0 ? 1 : maxPacketsPerPump) {}

std::size_t LoopbackServer::Pump() {
    std::size_t processedPackets = 0;
    while (processedPackets < m_maxPacketsPerPump) {
        std::vector<std::uint8_t> bytes;
        if (!m_transport.ReceiveOnServer(&bytes)) {
            break;
        }
        ++processedPackets;
        ++m_stats.packetsReceived;

        CommandPacket packet;
        std::string error;
        if (!DecodeCommandPacket(bytes, &packet, &error)) {
            ++m_stats.packetsRejected;
            m_stats.lastError = "命令包解码失败：" + error;
            continue;
        }

        for (const Command& command : packet.commands) {
            ++m_stats.commandsReceived;
            if (!m_handler) {
                ++m_stats.commandsRejected;
                m_stats.lastError = "服务器没有命令处理器";
                continue;
            }
            error.clear();
            if (m_handler(command, error)) {
                ++m_stats.commandsExecuted;
            } else {
                ++m_stats.commandsRejected;
                m_stats.lastError = error.empty() ? "命令执行失败" : error;
            }
        }
        SendSnapshot();
    }
    // 无命令时也回一帧心跳快照：客户端可见服务器 tick/位置，统计不会把"最后一次命令"
    // 误当成当前延迟。环回模式下每 tick 一个 44 字节包，成本可忽略。
    if (processedPackets == 0) {
        SendSnapshot();
    }
    return processedPackets;
}

void LoopbackServer::SendSnapshot() {
    if (!m_snapshotProvider) {
        return;
    }
    ServerSnapshot snapshot = m_snapshotProvider();
    snapshot.processedCommands = m_stats.commandsExecuted;
    std::string error;
    std::vector<std::uint8_t> bytes = EncodeServerSnapshot(snapshot, &error);
    if (bytes.empty() || !m_transport.SendToClient(std::move(bytes))) {
        ++m_stats.snapshotFailures;
        m_stats.lastError = bytes.empty() ? error : "环回客户端队列已满";
        return;
    }
    ++m_stats.snapshotsSent;
}

}  // namespace zl::net
