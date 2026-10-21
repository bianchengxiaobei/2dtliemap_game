#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace zl::server {
class ServerHost;
}

namespace zl::game {

class EmbeddedServer {
public:
    struct Options {
        std::string configPath = "config/game.json";
        std::string bindHost = "0.0.0.0";
        std::uint16_t port = 7777;
        std::uint64_t contentHash = 0;
        bool spawnDemoPopulation = true;
        std::string playerProfilesFile;
        float tickRate = 60.0f;
    };

    EmbeddedServer();
    ~EmbeddedServer();

    EmbeddedServer(const EmbeddedServer&) = delete;
    EmbeddedServer& operator=(const EmbeddedServer&) = delete;

    bool Start(const Options& options, std::string* error = nullptr);
    void Stop();

    bool Running() const;
    std::uint16_t Port() const;

private:
    void Loop();

    std::unique_ptr<server::ServerHost> m_host;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::uint16_t m_port = 0;
    float m_tickRate = 60.0f;
};

}  // namespace zl::game
