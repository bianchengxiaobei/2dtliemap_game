#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "net/FrameCodec.h"
#include "net/Kcp.h"

using namespace zl::net;

namespace {

std::vector<std::uint8_t> Pattern(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>(seed + i * 37U);
    }
    return bytes;
}

class LossyUdpRelay {
public:
    LossyUdpRelay(std::uint16_t serverPort, int dropEvery)
        : m_dropEvery(std::max(1, dropEvery)) {
        WSADATA data{};
        m_wsaStarted = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        std::memset(&m_server, 0, sizeof(m_server));
        m_server.sin_family = AF_INET;
        m_server.sin_port = htons(serverPort);
        inet_pton(AF_INET, "127.0.0.1", &m_server.sin_addr);
    }

    ~LossyUdpRelay() { Close(); }

    LossyUdpRelay(const LossyUdpRelay&) = delete;
    LossyUdpRelay& operator=(const LossyUdpRelay&) = delete;

    bool Start(std::string* error) {
        if (!m_wsaStarted) {
            if (error != nullptr) {
                *error = "WSAStartup 失败";
            }
            return false;
        }
        m_front = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        m_back = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_front == INVALID_SOCKET || m_back == INVALID_SOCKET) {
            if (error != nullptr) {
                *error = "创建丢包代理 socket 失败";
            }
            Close();
            return false;
        }
        u_long nonBlocking = 1;
        ioctlsocket(m_front, FIONBIO, &nonBlocking);
        ioctlsocket(m_back, FIONBIO, &nonBlocking);

        sockaddr_in bindAddress{};
        bindAddress.sin_family = AF_INET;
        bindAddress.sin_port = 0;
        inet_pton(AF_INET, "127.0.0.1", &bindAddress.sin_addr);
        if (bind(m_front, reinterpret_cast<const sockaddr*>(&bindAddress),
                 sizeof(bindAddress)) == SOCKET_ERROR ||
            bind(m_back, reinterpret_cast<const sockaddr*>(&bindAddress),
                 sizeof(bindAddress)) == SOCKET_ERROR) {
            if (error != nullptr) {
                *error = "绑定丢包代理 socket 失败";
            }
            Close();
            return false;
        }
        sockaddr_in actual{};
        int actualLength = sizeof(actual);
        if (getsockname(m_front, reinterpret_cast<sockaddr*>(&actual), &actualLength) ==
            SOCKET_ERROR) {
            if (error != nullptr) {
                *error = "读取丢包代理端口失败";
            }
            Close();
            return false;
        }
        m_port = ntohs(actual.sin_port);
        return true;
    }

    std::uint16_t Port() const { return m_port; }
    std::uint64_t ClientToServer() const { return m_clientToServer; }
    std::uint64_t ServerToClient() const { return m_serverToClient; }
    std::uint64_t BackReceived() const { return m_backReceived; }
    int LastBackError() const { return m_lastBackError; }
    int LastFrontError() const { return m_lastFrontError; }

    void Poll() {
        if (m_front == INVALID_SOCKET || m_back == INVALID_SOCKET) {
            return;
        }
        std::array<char, 65536> buffer{};
        for (;;) {
            sockaddr_in from{};
            int fromLength = sizeof(from);
            const int received =
                recvfrom(m_front, buffer.data(), static_cast<int>(buffer.size()), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLength);
            if (received == SOCKET_ERROR) {
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    break;
                }
                return;
            }
            if (received <= 0) {
                break;
            }
            m_client = from;
            if (ShouldDrop()) {
                continue;
            }
            if (sendto(m_back, buffer.data(), received, 0,
                       reinterpret_cast<const sockaddr*>(&m_server),
                       sizeof(m_server)) != SOCKET_ERROR) {
                ++m_clientToServer;
            }
        }

        for (;;) {
            sockaddr_in from{};
            int fromLength = sizeof(from);
            const int received =
                recvfrom(m_back, buffer.data(), static_cast<int>(buffer.size()), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLength);
            if (received == SOCKET_ERROR) {
                m_lastBackError = WSAGetLastError();
                break;
            }
            if (received <= 0) {
                break;
            }
            ++m_backReceived;
            if (sendto(m_front, buffer.data(), received, 0,
                       reinterpret_cast<const sockaddr*>(&m_client),
                       sizeof(m_client)) != SOCKET_ERROR) {
                ++m_serverToClient;
            } else {
                m_lastFrontError = WSAGetLastError();
            }
        }
    }

private:
    bool ShouldDrop() {
        ++m_counter;
        return m_counter % m_dropEvery == 0;
    }

    void Close() {
        if (m_front != INVALID_SOCKET) {
            closesocket(m_front);
            m_front = INVALID_SOCKET;
        }
        if (m_back != INVALID_SOCKET) {
            closesocket(m_back);
            m_back = INVALID_SOCKET;
        }
        if (m_wsaStarted) {
            WSACleanup();
            m_wsaStarted = false;
        }
    }

    SOCKET m_front = INVALID_SOCKET;
    SOCKET m_back = INVALID_SOCKET;
    sockaddr_in m_server{};
    sockaddr_in m_client{};
    bool m_wsaStarted = false;
    std::uint16_t m_port = 0;
    int m_dropEvery = 3;
    std::uint64_t m_counter = 0;
    std::uint64_t m_clientToServer = 0;
    std::uint64_t m_serverToClient = 0;
    std::uint64_t m_backReceived = 0;
    int m_lastBackError = 0;
    int m_lastFrontError = 0;
};

bool WaitForServerFrame(KcpServer& server, KcpClient& client, KcpConnectionId* id,
                        std::vector<std::uint8_t>* frame, int timeoutMs = 2000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        server.Poll();
        client.Poll();
        if (server.Receive(id, frame)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    server.Poll();
    client.Poll();
    return server.Receive(id, frame);
}

bool WaitForClientFrame(KcpClient& client, KcpServer& server,
                        std::vector<std::uint8_t>* frame, int timeoutMs = 2000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        server.Poll();
        client.Poll();
        if (client.Receive(frame)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    server.Poll();
    client.Poll();
    return client.Receive(frame);
}

void PumpBoth(KcpServer& server, KcpClient& client, int rounds = 3) {
    for (int i = 0; i < rounds; ++i) {
        server.Poll();
        client.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace

TEST_CASE("M13 FrameCodec：完整帧、拆包、粘包与非法帧") {
    const std::vector<std::uint8_t> payload = Pattern(37, 11);
    const std::vector<std::uint8_t> encoded = EncodeFrame(payload);
    REQUIRE_FALSE(encoded.empty());
    CHECK(encoded.size() == kFrameHeaderSize + payload.size());
    CHECK(encoded[0] == 'Z');
    CHECK(encoded[1] == 'L');
    CHECK(encoded[2] == 'F');
    CHECK(encoded[3] == '1');

    FrameDecoder decoder;
    REQUIRE(decoder.Push(std::span<const std::uint8_t>(encoded.data(), 5)));
    CHECK(decoder.BufferedBytes() == 5);
    CHECK(decoder.PendingFrames() == 0);
    REQUIRE(decoder.Push(
        std::span<const std::uint8_t>(encoded.data() + 5, encoded.size() - 5)));

    std::vector<std::uint8_t> decoded;
    REQUIRE(decoder.Pop(&decoded));
    CHECK(decoded == payload);
    CHECK_FALSE(decoder.Pop(&decoded));
    CHECK(decoder.BufferedBytes() == 0);

    const std::vector<std::uint8_t> first = EncodeFrame(Pattern(3, 1));
    const std::vector<std::uint8_t> second = EncodeFrame(Pattern(9, 2));
    std::vector<std::uint8_t> combined = first;
    combined.insert(combined.end(), second.begin(), second.end());
    REQUIRE(decoder.Push(combined));
    CHECK(decoder.PendingFrames() == 2);
    REQUIRE(decoder.Pop(&decoded));
    CHECK(decoded == Pattern(3, 1));
    REQUIRE(decoder.Pop(&decoded));
    CHECK(decoded == Pattern(9, 2));

    std::vector<std::uint8_t> badMagic = encoded;
    badMagic[0] = 0;
    std::string error;
    CHECK_FALSE(decoder.Push(badMagic, &error));
    CHECK_FALSE(error.empty());
    CHECK(decoder.BufferedBytes() == 0);
    CHECK(decoder.PendingFrames() == 0);

    std::vector<std::uint8_t> badLength(kFrameHeaderSize, 0);
    badLength[0] = 'Z';
    badLength[1] = 'L';
    badLength[2] = 'F';
    badLength[3] = '1';
    badLength[4] = 0xFF;
    badLength[5] = 0xFF;
    badLength[6] = 0x01;
    badLength[7] = 0x00;
    CHECK_FALSE(decoder.Push(badLength, &error));
    CHECK_FALSE(error.empty());

    std::vector<std::uint8_t> tooLarge(kMaxFramePayload + 1);
    CHECK(EncodeFrame(tooLarge, &error).empty());
    CHECK_FALSE(error.empty());
}

TEST_CASE("M13 KCP：客户端发送、服务器接收并回包") {
    KcpServer server(4, 5000);
    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));
    REQUIRE(server.Port() != 0);

    KcpClient client;
    REQUIRE(client.Connect("127.0.0.1", server.Port(), &error));

    const std::vector<std::uint8_t> requestPayload = Pattern(128, 5);
    const std::vector<std::uint8_t> request = EncodeFrame(requestPayload);
    REQUIRE(client.Send(request, &error));

    KcpConnectionId id = 0;
    std::vector<std::uint8_t> received;
    REQUIRE(WaitForServerFrame(server, client, &id, &received));
    CHECK(id != 0);
    CHECK(received == requestPayload);
    CHECK(server.Connections().size() == 1);
    CHECK(server.Connections().front() == id);

    const std::vector<std::uint8_t> replyPayload = Pattern(64, 9);
    const std::vector<std::uint8_t> reply = EncodeFrame(replyPayload);
    REQUIRE(server.Send(id, reply, &error));
    REQUIRE(WaitForClientFrame(client, server, &received));
    CHECK(received == replyPayload);

    CHECK(client.GetStats().framesSent >= 1);
    CHECK(client.GetStats().framesReceived >= 1);
    CHECK(server.GetStats().framesReceived >= 1);
    CHECK(server.GetStats().framesSent >= 1);
    CHECK(client.GetStats().datagramsReceived > 0);
    CHECK(server.GetStats().datagramsReceived > 0);
}

TEST_CASE("M13 KCP：大帧分片与双客户端隔离") {
    KcpServer server(4, 5000);
    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));

    KcpClient first;
    KcpClient second;
    REQUIRE(first.Connect("127.0.0.1", server.Port(), &error));
    REQUIRE(second.Connect("127.0.0.1", server.Port(), &error));

    const std::vector<std::uint8_t> firstPayload = Pattern(8192, 3);
    const std::vector<std::uint8_t> secondPayload = Pattern(8192, 7);
    REQUIRE(first.Send(EncodeFrame(firstPayload), &error));
    KcpConnectionId firstId = 0;
    KcpConnectionId secondId = 0;
    std::vector<std::uint8_t> received;
    REQUIRE(WaitForServerFrame(server, first, &firstId, &received));
    CHECK(received == firstPayload);

    REQUIRE(second.Send(EncodeFrame(secondPayload), &error));
    REQUIRE(WaitForServerFrame(server, second, &secondId, &received));
    CHECK(received == secondPayload);
    CHECK(firstId != secondId);
    CHECK(server.Connections().size() == 2);

    const std::vector<std::uint8_t> replyPayload = Pattern(32, 42);
    REQUIRE(server.Send(firstId, EncodeFrame(replyPayload), &error));
    REQUIRE(WaitForClientFrame(first, server, &received));
    CHECK(received == replyPayload);
    CHECK_FALSE(second.Receive(&received));
}

TEST_CASE("M13 KCP：丢包代理下大帧仍可靠送达") {
    KcpServer server(2, 5000);
    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));

    // 客户端→服务器方向每 3 个 UDP 数据报丢 1 个；KCP 应重传并在测试超时内交付完整帧。
    LossyUdpRelay relay(server.Port(), 3);
    REQUIRE_MESSAGE(relay.Start(&error), error);

    KcpClient client;
    REQUIRE(client.Connect("127.0.0.1", relay.Port(), &error));
    const std::vector<std::uint8_t> payload = Pattern(12000, 17);
    REQUIRE(client.Send(EncodeFrame(payload), &error));

    KcpConnectionId id = 0;
    std::vector<std::uint8_t> received;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(6000);
    bool delivered = false;
    while (std::chrono::steady_clock::now() < deadline) {
        server.Poll();
        client.Poll();
        relay.Poll();
        if (server.Receive(&id, &received)) {
            delivered = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    MESSAGE("relay loss test: clientSent="
            << client.GetStats().datagramsSent << " clientRecv="
            << client.GetStats().datagramsReceived << " serverRecv="
            << server.GetStats().datagramsReceived << " serverSent="
            << server.GetStats().datagramsSent << " relayC2S=" << relay.ClientToServer()
            << " relayS2C=" << relay.ServerToClient()
            << " backRecv=" << relay.BackReceived()
            << " backErr=" << relay.LastBackError()
            << " frontErr=" << relay.LastFrontError());
    REQUIRE(delivered);
    CHECK(id != 0);
    CHECK(received == payload);
    CHECK(client.GetStats().datagramsSent >= 2);
}

TEST_CASE("M13 KCP：非法帧被丢弃且会话仍可工作") {
    KcpServer server(2, 5000);
    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));

    KcpClient client;
    REQUIRE(client.Connect("127.0.0.1", server.Port(), &error));
    REQUIRE(client.Send(std::vector<std::uint8_t>(8, 0), &error));

    KcpConnectionId id = 0;
    std::vector<std::uint8_t> received;
    PumpBoth(server, client);
    CHECK_FALSE(server.Receive(&id, &received));
    CHECK(server.GetStats().datagramsRejected > 0);

    const std::vector<std::uint8_t> payload = Pattern(16, 21);
    REQUIRE(client.Send(EncodeFrame(payload), &error));
    REQUIRE(WaitForServerFrame(server, client, &id, &received));
    CHECK(received == payload);
}

TEST_CASE("M13 KCP：服务器会话超时清理") {
    KcpServer server(2, 50);
    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));

    KcpClient client;
    REQUIRE(client.Connect("127.0.0.1", server.Port(), &error));
    REQUIRE(client.Send(EncodeFrame(Pattern(4, 1)), &error));

    KcpConnectionId id = 0;
    std::vector<std::uint8_t> received;
    REQUIRE(WaitForServerFrame(server, client, &id, &received));
    CHECK(server.Connections().size() == 1);

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (std::chrono::steady_clock::now() < deadline && !server.Connections().empty()) {
        server.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(server.Connections().empty());
}

TEST_CASE("M13 KCP：未连接/监听失败返回清晰错误") {
    KcpClient client;
    std::string error;
    CHECK_FALSE(client.Send(std::vector<std::uint8_t>{1}, &error));
    CHECK_FALSE(error.empty());

    KcpServer server;
    error.clear();
    CHECK_FALSE(server.Listen("256.256.256.256", 0, &error));
    CHECK_FALSE(error.empty());
}
