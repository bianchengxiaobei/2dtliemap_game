#include "game/EmbeddedServer.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

#include "core/Config.h"
#include "core/Log.h"
#include "server/ServerHost.h"

namespace zl::game {

EmbeddedServer::EmbeddedServer() = default;

EmbeddedServer::~EmbeddedServer() { Stop(); }

bool EmbeddedServer::Start(const Options& options, std::string* error) {
    Stop();
    auto host = std::make_unique<server::ServerHost>();
    server::ServerHost::Options hostOptions;
    hostOptions.configPath = options.configPath;
    hostOptions.network = true;
    hostOptions.bindHost = options.bindHost;
    hostOptions.bindHostOverride = true;
    hostOptions.port = options.port;
    hostOptions.portOverride = true;
    hostOptions.contentHash = options.contentHash;
    hostOptions.spawnDemoPopulation = options.spawnDemoPopulation;
    hostOptions.spawnAnimals = options.spawnAnimals;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.enablePlayerProfiles = true;
    hostOptions.playerProfilesFile = options.playerProfilesFile;
    hostOptions.tickRate = options.tickRate;
    std::string initError;
    if (!host->Init(hostOptions, &initError)) {
        if (error != nullptr) {
            *error = initError.empty() ? "内嵌服务器初始化失败" : initError;
        }
        host->Shutdown();
        return false;
    }
    m_host = std::move(host);
    m_port = m_host->NetworkPort();
    m_tickRate = static_cast<float>(
        m_host->Config().Get<double>("server.tickRate", options.tickRate));
    m_stop = false;
    try {
        m_thread = std::thread(&EmbeddedServer::Loop, this);
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("内嵌服务器线程创建失败：") + exception.what();
        }
        m_stop = true;
        m_host->RequestStop();
        m_host->Shutdown();
        m_host.reset();
        m_port = 0;
        return false;
    }
    ZL_INFO("server", "内嵌主机已启动：UDP 端口 {}", m_port);
    return true;
}

void EmbeddedServer::Loop() {
    const double tickRate = std::max(1.0, static_cast<double>(m_tickRate));
    const double dt = 1.0 / tickRate;
    auto next = std::chrono::steady_clock::now();
    while (!m_stop && m_host != nullptr && m_host->Running()) {
        m_host->Tick(static_cast<float>(dt));
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(dt));
        const auto now = std::chrono::steady_clock::now();
        if (now - next > std::chrono::milliseconds(500)) {
            next = now;
        }
        std::this_thread::sleep_until(next);
    }
}

void EmbeddedServer::Stop() {
    if (m_host == nullptr) {
        return;
    }
    m_stop = true;
    m_host->RequestStop();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_host->Shutdown();
    m_host.reset();
    m_port = 0;
    ZL_INFO("server", "内嵌主机已停止");
}

bool EmbeddedServer::Running() const {
    return m_host != nullptr && !m_stop && m_host->Running();
}

std::uint16_t EmbeddedServer::Port() const { return m_port; }

std::uint64_t EmbeddedServer::ContentHash() const {
    return m_host != nullptr ? m_host->ContentHash() : 0;
}

}  // namespace zl::game
