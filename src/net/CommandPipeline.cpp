#include "net/CommandPipeline.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace zl::net {
namespace {

CommandPipeline::Options Normalize(CommandPipeline::Options options) {
    options.maxCommandsPerPacket =
        std::clamp<std::size_t>(options.maxCommandsPerPacket, 1, kMaxCommandsPerPacket);
    options.maxPacketsPerPump = std::max<std::size_t>(options.maxPacketsPerPump, 1);
    options.maxQueuedPackets = std::max<std::size_t>(options.maxQueuedPackets, 1);
    return options;
}

}  // namespace

CommandPipeline::CommandPipeline(Options options)
    : m_options(Normalize(options)),
      m_transport(std::make_unique<LoopbackTransport>(m_options.maxQueuedPackets)),
      m_client(std::make_unique<LoopbackClient>(*m_transport,
                                                m_options.maxCommandsPerPacket)),
      m_server(std::make_unique<LoopbackServer>(
          *m_transport, [this](const Command& command, std::string& error) {
              return Execute(command, error);
          },
          SnapshotProvider{}, m_options.maxPacketsPerPump)) {}

CommandPipeline::~CommandPipeline() = default;

void CommandPipeline::SetHandler(CommandHandler handler) {
    m_handler = std::move(handler);
}

void CommandPipeline::SetSnapshotProvider(SnapshotProvider provider) {
    m_snapshotProvider = std::move(provider);
    m_server->SetSnapshotProvider(m_snapshotProvider);
}

bool CommandPipeline::Validate(const Command& command, std::string& error) {
    if (!IsValidCommandType(command.type)) {
        error = "未知命令类型";
        return false;
    }
    if (command.playerStableId == 0) {
        error = "命令缺少 playerStableId";
        return false;
    }
    if (!std::isfinite(command.payload.x) || !std::isfinite(command.payload.y)) {
        error = "命令 payload 浮点非法（NaN/Inf）";
        return false;
    }
    return true;
}

bool CommandPipeline::Execute(const Command& command, std::string& error) {
    if (!m_handler) {
        error = "当前没有命令处理器";
        return false;
    }
    if (!m_handler(command, error)) {
        if (error.empty()) {
            error = "命令处理器拒绝执行";
        }
        m_lastError = error;
        return false;
    }
    return true;
}

bool CommandPipeline::Submit(const Command& command, std::string* error) {
    std::string localError;
    if (!Validate(command, localError)) {
        m_lastError = localError;
        if (error != nullptr) {
            *error = localError;
        }
        return false;
    }
    ++m_submitted;
    if (m_options.mode == TransportMode::Direct) {
        if (Execute(command, localError)) {
            ++m_directExecuted;
            return true;
        }
        ++m_directRejected;
        if (error != nullptr) {
            *error = localError;
        }
        return false;
    }

    if (m_batch.size() >= m_options.maxCommandsPerPacket && !Flush(error)) {
        return false;
    }
    m_batch.push_back(command);
    if (m_batch.size() >= m_options.maxCommandsPerPacket) {
        if (!Flush(error)) {
            return false;
        }
    }
    return true;
}

bool CommandPipeline::Flush(std::string* error) {
    if (m_options.mode == TransportMode::Direct || m_batch.empty()) {
        return true;
    }
    std::string localError;
    if (!m_client->Send(m_batch, &localError)) {
        m_lastError = localError;
        if (error != nullptr) {
            *error = localError;
        }
        return false;
    }
    m_batch.clear();
    return true;
}

void CommandPipeline::Pump() {
    if (m_options.mode != TransportMode::Loopback) {
        return;
    }
    m_server->Pump();
    ServerSnapshot snapshot;
    std::string error;
    while (m_client->PollSnapshot(&snapshot, &error)) {
        m_lastSnapshot = snapshot;
        m_hasSnapshot = true;
        error.clear();
    }
    if (!error.empty()) {
        m_lastError = error;
    }
}

void CommandPipeline::Clear() {
    m_batch.clear();
    if (m_transport != nullptr) {
        m_transport->Clear();
    }
    m_hasSnapshot = false;
    m_lastSnapshot = ServerSnapshot{};
}

CommandPipeline::Stats CommandPipeline::GetStats() const {
    Stats stats;
    stats.mode = m_options.mode;
    stats.commandsSubmitted = m_submitted;
    stats.queuedCommands = m_batch.size();
    stats.lastError = m_lastError;

    if (m_options.mode == TransportMode::Direct) {
        stats.commandsExecuted = m_directExecuted;
        stats.commandsRejected = m_directRejected;
        return stats;
    }

    const LoopbackClientStats& client = m_client->GetStats();
    const LoopbackServerStats& server = m_server->GetStats();
    stats.commandsExecuted = server.commandsExecuted;
    stats.commandsRejected = server.commandsRejected;
    stats.packetsSent = client.packetsSent;
    stats.packetsReceived = server.packetsReceived;
    stats.packetsRejected = server.packetsRejected;
    stats.snapshotsReceived = client.snapshotsReceived;
    stats.bytesSent = client.bytesSent;
    stats.bytesReceived = client.snapshotBytesReceived;
    if (!server.lastError.empty()) {
        stats.lastError = server.lastError;
    }
    return stats;
}

}  // namespace zl::net
