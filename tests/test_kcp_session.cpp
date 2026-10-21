#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "net/FrameCodec.h"
#include "net/Kcp.h"
#include "net/KcpSession.h"
#include "net/SessionProtocol.h"

using namespace zl::net;

namespace {

std::vector<std::uint8_t> Pattern(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>(seed + i * 19U);
    }
    return bytes;
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

bool WaitConnected(KcpSessionServer& server, KcpSessionClient& client,
                   SessionId* sessionId, int timeoutMs = 2000) {
    bool serverConnected = false;
    const bool ok = WaitUntil(
        [&] {
            server.Poll();
            client.Poll();
            KcpSessionServerEvent event;
            if (!serverConnected && server.PollEvent(&event) &&
                event.type == KcpSessionServerEventType::Connected) {
                serverConnected = true;
                if (sessionId != nullptr) {
                    *sessionId = event.sessionId;
                }
            }
            return serverConnected && client.IsConnected();
        },
        timeoutMs);
    return ok;
}

bool WaitClientData(KcpSessionClient& client, KcpSessionServer& server,
                    std::vector<std::uint8_t>* data, int timeoutMs = 2000) {
    return WaitUntil(
        [&] {
            server.Poll();
            client.Poll();
            return client.Receive(data);
        },
        timeoutMs);
}

bool WaitServerData(KcpSessionServer& server, KcpSessionClient& client,
                    SessionId expectedId, std::vector<std::uint8_t>* data,
                    int timeoutMs = 2000) {
    return WaitUntil(
        [&] {
            server.Poll();
            client.Poll();
            KcpSessionServerEvent event;
            if (!server.PollEvent(&event)) {
                return false;
            }
            if (event.type == KcpSessionServerEventType::Data &&
                event.sessionId == expectedId) {
                *data = std::move(event.data);
                return true;
            }
            return false;
        },
        timeoutMs);
}

bool WaitServerDisconnected(KcpSessionServer& server, SessionId expectedId,
                            SessionDisconnectReason* reason, int timeoutMs = 2000) {
    return WaitUntil(
        [&] {
            server.Poll();
            KcpSessionServerEvent event;
            if (!server.PollEvent(&event)) {
                return false;
            }
            if (event.type == KcpSessionServerEventType::Disconnected &&
                event.sessionId == expectedId) {
                if (reason != nullptr) {
                    *reason = event.reason;
                }
                return true;
            }
            return false;
        },
        timeoutMs);
}

KcpSessionServerOptions ServerOptions(std::uint64_t contentHash,
                                      std::size_t maxSessions = 4) {
    KcpSessionServerOptions options;
    options.contentHash = contentHash;
    options.maxSessions = maxSessions;
    options.handshakeTimeoutMs = 1000;
    options.sessionTimeoutMs = 3000;
    options.heartbeatMs = 100;
    return options;
}

KcpSessionClientOptions ClientOptions(std::uint64_t contentHash) {
    KcpSessionClientOptions options;
    options.contentHash = contentHash;
    options.handshakeTimeoutMs = 1000;
    options.heartbeatIntervalMs = 100;
    options.heartbeatTimeoutMs = 600;
    return options;
}

}  // namespace

TEST_CASE("M13 SessionProtocol：Hello/Welcome/Reject/Disconnect/Ping/Pong 往返") {
    SessionHello hello;
    hello.clientNonce = 0x12345678;
    hello.contentHash = 0x1122334455667788ull;
    hello.playerIdentity = "player-roundtrip";
    hello.playerName = "Alice";
    const std::vector<std::uint8_t> helloFrame = EncodeSessionHello(hello);
    REQUIRE_FALSE(helloFrame.empty());

    SessionMessage message;
    REQUIRE(DecodeSessionMessage(helloFrame, &message));
    CHECK(message.type == SessionMessageType::Hello);
    SessionHello decodedHello;
    REQUIRE(DecodeSessionHello(message.payload, &decodedHello));
    CHECK(decodedHello.clientNonce == hello.clientNonce);
    CHECK(decodedHello.contentHash == hello.contentHash);
    CHECK(decodedHello.playerIdentity == hello.playerIdentity);
    CHECK(decodedHello.playerName == hello.playerName);

    SessionWelcome welcome;
    welcome.sessionId = 42;
    welcome.serverTick = 999;
    welcome.contentHash = 0xAABBCCDDEEFF0011ull;
    welcome.heartbeatMs = 250;
    const std::vector<std::uint8_t> welcomeFrame = EncodeSessionWelcome(welcome);
    REQUIRE(DecodeSessionMessage(welcomeFrame, &message));
    CHECK(message.type == SessionMessageType::Welcome);
    SessionWelcome decodedWelcome;
    REQUIRE(DecodeSessionWelcome(message.payload, &decodedWelcome));
    CHECK(decodedWelcome.sessionId == 42);
    CHECK(decodedWelcome.serverTick == 999);
    CHECK(decodedWelcome.contentHash == welcome.contentHash);
    CHECK(decodedWelcome.heartbeatMs == 250);

    SessionReject reject;
    reject.reason = SessionDisconnectReason::ContentMismatch;
    reject.message = "内容版本不一致";
    const std::vector<std::uint8_t> rejectFrame = EncodeSessionReject(reject);
    REQUIRE(DecodeSessionMessage(rejectFrame, &message));
    CHECK(message.type == SessionMessageType::Reject);
    SessionReject decodedReject;
    REQUIRE(DecodeSessionReject(message.payload, &decodedReject));
    CHECK(decodedReject.reason == reject.reason);
    CHECK(decodedReject.message == reject.message);

    SessionDisconnect disconnect;
    disconnect.reason = SessionDisconnectReason::ClientLeft;
    disconnect.message = "bye";
    const std::vector<std::uint8_t> disconnectFrame = EncodeSessionDisconnect(disconnect);
    REQUIRE(DecodeSessionMessage(disconnectFrame, &message));
    SessionDisconnect decodedDisconnect;
    REQUIRE(DecodeSessionDisconnect(message.payload, &decodedDisconnect));
    CHECK(decodedDisconnect.reason == disconnect.reason);
    CHECK(decodedDisconnect.message == disconnect.message);

    const std::vector<std::uint8_t> ping = EncodeSessionPing(0x1234567890ABCDEFull);
    REQUIRE(DecodeSessionMessage(ping, &message));
    CHECK(message.type == SessionMessageType::Ping);
    std::uint64_t nonce = 0;
    REQUIRE(DecodeSessionPing(message.payload, &nonce));
    CHECK(nonce == 0x1234567890ABCDEFull);

    const std::vector<std::uint8_t> pong = EncodeSessionPong(7);
    REQUIRE(DecodeSessionMessage(pong, &message));
    CHECK(message.type == SessionMessageType::Pong);
    REQUIRE(DecodeSessionPong(message.payload, &nonce));
    CHECK(nonce == 7);
}

TEST_CASE("M13 SessionProtocol：版本、magic、类型和长度校验") {
    const std::vector<std::uint8_t> good = EncodeSessionHello(SessionHello{1, 2});
    REQUIRE_FALSE(good.empty());

    SessionMessage message;
    SessionDecodeError code = SessionDecodeError::None;
    std::vector<std::uint8_t> bad = good;
    bad[4] = 99;
    bad[5] = 0;
    CHECK_FALSE(DecodeSessionMessage(bad, &message, nullptr, &code));
    CHECK(code == SessionDecodeError::BadVersion);

    bad = good;
    bad[0] = 0;
    CHECK_FALSE(DecodeSessionMessage(bad, &message, nullptr, &code));
    CHECK(code == SessionDecodeError::BadMagic);

    bad = good;
    bad[6] = 0;
    CHECK_FALSE(DecodeSessionMessage(bad, &message, nullptr, &code));
    CHECK(code == SessionDecodeError::BadType);

    bad = good;
    bad.pop_back();
    CHECK_FALSE(DecodeSessionMessage(bad, &message, nullptr, &code));
    CHECK(code == SessionDecodeError::BadLength);

    CHECK_FALSE(DecodeSessionMessage(
        std::span<const std::uint8_t>(good.data(), kSessionHeaderSize - 1), &message,
        nullptr, &code));
    CHECK(code == SessionDecodeError::TooShort);
}

TEST_CASE("M13 KCP Session：握手、双向 Data、客户端断开") {
    constexpr std::uint64_t contentHash = 0x1234567890ABCDEFull;
    KcpSessionServer server(ServerOptions(contentHash));
    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));

    KcpSessionClient client;
    REQUIRE(client.Connect("127.0.0.1", server.Port(), ClientOptions(contentHash), &error));

    SessionId sessionId = 0;
    REQUIRE(WaitConnected(server, client, &sessionId));
    CHECK(sessionId != 0);
    CHECK(client.Id() == sessionId);
    CHECK(server.Sessions().size() == 1);
    CHECK(server.Sessions().front() == sessionId);
    CHECK(server.GetStats().accepted == 1);

    const std::vector<std::uint8_t> clientPayload = Pattern(1024, 3);
    REQUIRE(client.Send(clientPayload, &error));
    std::vector<std::uint8_t> received;
    REQUIRE(WaitServerData(server, client, sessionId, &received));
    CHECK(received == clientPayload);

    const std::vector<std::uint8_t> serverPayload = Pattern(2048, 7);
    REQUIRE(server.Send(sessionId, serverPayload, &error));
    REQUIRE(WaitClientData(client, server, &received));
    CHECK(received == serverPayload);

    client.Close();
    SessionDisconnectReason reason = SessionDisconnectReason::None;
    REQUIRE(WaitServerDisconnected(server, sessionId, &reason));
    CHECK(reason == SessionDisconnectReason::ClientLeft);
    CHECK(server.Sessions().empty());
}

TEST_CASE("M13 KCP Session：内容 hash 不一致被拒绝") {
    KcpSessionServer server(ServerOptions(111));
    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));

    KcpSessionClient client;
    REQUIRE(client.Connect("127.0.0.1", server.Port(), ClientOptions(222), &error));

    REQUIRE(WaitUntil([&] {
        server.Poll();
        client.Poll();
        return client.State() == KcpSessionClientState::Rejected;
    }));
    CHECK(client.LastReason() == SessionDisconnectReason::ContentMismatch);
    CHECK(server.Sessions().empty());
    CHECK(server.GetStats().rejected >= 1);
}

TEST_CASE("M13 KCP Session：服务器满时拒绝新客户端") {
    constexpr std::uint64_t contentHash = 7;
    KcpSessionServer server(ServerOptions(contentHash, 1));
    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));

    KcpSessionClient first;
    REQUIRE(first.Connect("127.0.0.1", server.Port(), ClientOptions(contentHash), &error));
    SessionId firstId = 0;
    REQUIRE(WaitConnected(server, first, &firstId));
    CHECK(first.IsConnected());

    KcpSessionClient second;
    REQUIRE(second.Connect("127.0.0.1", server.Port(), ClientOptions(contentHash), &error));
    REQUIRE(WaitUntil([&] {
        server.Poll();
        second.Poll();
        return second.State() == KcpSessionClientState::Rejected;
    }));
    CHECK(second.LastReason() == SessionDisconnectReason::ServerFull);
    CHECK(first.IsConnected());
    CHECK(server.Sessions().size() == 1);
}

TEST_CASE("M13 KCP Session：协议版本不匹配返回明确拒绝原因") {
    constexpr std::uint64_t contentHash = 9;
    KcpSessionServer server(ServerOptions(contentHash));
    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));

    KcpClient raw;
    REQUIRE(raw.Connect("127.0.0.1", server.Port(), &error));

    const std::vector<std::uint8_t> hello =
        EncodeSessionHello(SessionHello{1, contentHash});
    std::vector<std::uint8_t> wrongVersion = hello;
    wrongVersion[4] = 99;
    wrongVersion[5] = 0;
    REQUIRE(raw.Send(EncodeFrame(wrongVersion), &error));

    std::vector<std::uint8_t> received;
    REQUIRE(WaitUntil([&] {
        server.Poll();
        raw.Poll();
        return raw.Receive(&received);
    }));

    SessionMessage message;
    REQUIRE(DecodeSessionMessage(received, &message));
    CHECK(message.type == SessionMessageType::Reject);
    SessionReject reject;
    REQUIRE(DecodeSessionReject(message.payload, &reject));
    CHECK(reject.reason == SessionDisconnectReason::ProtocolVersion);
    CHECK(server.GetStats().rejected >= 1);
}

TEST_CASE("M13 KCP Session：服务器握手超时") {
    KcpSessionServerOptions options = ServerOptions(1);
    options.handshakeTimeoutMs = 100;
    KcpSessionServer server(options);
    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));

    KcpClient raw;
    REQUIRE(raw.Connect("127.0.0.1", server.Port(), &error));
    REQUIRE(raw.Send(std::vector<std::uint8_t>{'Z', 'L', 'F', '1'}, &error));

    std::vector<std::uint8_t> received;
    REQUIRE(WaitUntil([&] {
        server.Poll();
        raw.Poll();
        return raw.Receive(&received);
    }, 1500));

    SessionMessage message;
    REQUIRE(DecodeSessionMessage(received, &message));
    CHECK(message.type == SessionMessageType::Reject);
    SessionReject reject;
    REQUIRE(DecodeSessionReject(message.payload, &reject));
    CHECK(reject.reason == SessionDisconnectReason::HandshakeTimeout);
    CHECK(server.Sessions().empty());
}
TEST_CASE("M13 KCP Session：心跳保活与无心跳超时") {
    constexpr std::uint64_t contentHash = 11;
    KcpSessionServerOptions serverOptions = ServerOptions(contentHash);
    serverOptions.sessionTimeoutMs = 250;
    KcpSessionServer server(serverOptions);

    std::string error;
    REQUIRE(server.Listen("127.0.0.1", 0, &error));

    KcpSessionClientOptions clientOptions = ClientOptions(contentHash);
    clientOptions.heartbeatIntervalMs = 30;
    clientOptions.heartbeatTimeoutMs = 150;

    KcpSessionClient client;
    REQUIRE(client.Connect("127.0.0.1", server.Port(), clientOptions, &error));
    SessionId sessionId = 0;
    REQUIRE(WaitConnected(server, client, &sessionId));

    CHECK_FALSE(WaitUntil([&] {
        server.Poll();
        client.Poll();
        return !client.IsConnected();
    }, 500));
    CHECK(client.IsConnected());
    CHECK(server.Sessions().size() == 1);

    SessionDisconnectReason reason = SessionDisconnectReason::None;
    REQUIRE(WaitServerDisconnected(server, sessionId, &reason, 1500));
    CHECK(reason == SessionDisconnectReason::SessionTimeout);
    CHECK(server.Sessions().empty());
}

TEST_CASE("M13 KCP Session：握手超时") {
    KcpServer sink;
    std::string error;
    REQUIRE(sink.Listen("127.0.0.1", 0, &error));

    KcpSessionClientOptions options;
    options.handshakeTimeoutMs = 120;
    options.heartbeatIntervalMs = 30;
    options.heartbeatTimeoutMs = 150;

    KcpSessionClient client;
    REQUIRE(client.Connect("127.0.0.1", sink.Port(), options, &error));

    REQUIRE(WaitUntil([&] {
        sink.Poll();
        client.Poll();
        return client.State() == KcpSessionClientState::Disconnected;
    }, 1000));
    CHECK(client.LastReason() == SessionDisconnectReason::HandshakeTimeout);
}
