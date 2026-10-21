#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace zl::net {

using KcpConnectionId = std::uint32_t;

struct KcpStats {
    std::uint64_t datagramsSent = 0;
    std::uint64_t datagramsReceived = 0;
    std::uint64_t bytesSent = 0;
    std::uint64_t bytesReceived = 0;
    std::uint64_t framesSent = 0;
    std::uint64_t framesReceived = 0;
    std::uint64_t datagramsRejected = 0;
};

class KcpClient {
public:
    KcpClient();
    ~KcpClient();

    KcpClient(const KcpClient&) = delete;
    KcpClient& operator=(const KcpClient&) = delete;

    bool Connect(std::string_view host, std::uint16_t port, std::string* error = nullptr);
    void Close();
    bool IsConnected() const;

    void Poll();

    bool Send(std::span<const std::uint8_t> frame, std::string* error = nullptr);
    bool Receive(std::vector<std::uint8_t>* frame);

    const KcpStats& GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class KcpServer {
public:
    explicit KcpServer(std::size_t maxConnections = 8, std::uint32_t timeoutMs = 30000);
    ~KcpServer();

    KcpServer(const KcpServer&) = delete;
    KcpServer& operator=(const KcpServer&) = delete;

    bool Listen(std::string_view bindHost, std::uint16_t port, std::string* error = nullptr);
    void Close();
    bool IsListening() const;
    std::uint16_t Port() const;

    void Poll();

    bool Send(KcpConnectionId id, std::span<const std::uint8_t> frame,
              std::string* error = nullptr);
    bool Receive(KcpConnectionId* id, std::vector<std::uint8_t>* frame);
    void Disconnect(KcpConnectionId id);

    std::vector<KcpConnectionId> Connections() const;
    const KcpStats& GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace zl::net
