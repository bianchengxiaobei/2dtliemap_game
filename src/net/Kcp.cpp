#include "net/Kcp.h"

#include <ikcp.h>

#include "net/FrameCodec.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <random>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;

inline int closesocket(SOCKET socket) { return ::close(socket); }
#endif

namespace zl::net {
namespace {

constexpr int kKcpMtu = 1200;
constexpr int kKcpInterval = 10;
constexpr int kKcpResend = 2;
constexpr int kKcpWindow = 128;
constexpr int kReceiveBufferBytes = 65536;
constexpr std::size_t kKcpHeaderBytes = 24;

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

#ifdef _WIN32
struct WinsockRuntime {
    WinsockRuntime() {
        WSADATA data{};
        code = WSAStartup(MAKEWORD(2, 2), &data);
        started = code == 0;
    }

    ~WinsockRuntime() {
        if (started) {
            WSACleanup();
        }
    }

    bool started = false;
    int code = 0;
};

WinsockRuntime& Runtime() {
    static WinsockRuntime runtime;
    return runtime;
}

bool EnsureWinsock(std::string* error) {
    const WinsockRuntime& runtime = Runtime();
    if (!runtime.started) {
        if (error != nullptr) {
            *error = "WSAStartup 失败（" + std::to_string(runtime.code) + "）";
        }
        return false;
    }
    return true;
}

using SocketLength = int;

constexpr int kWouldBlock = WSAEWOULDBLOCK;
constexpr int kConnectionReset = WSAECONNRESET;

std::string SocketError(std::string_view what) {
    return std::string(what) + "（WSA " + std::to_string(WSAGetLastError()) + "）";
}
#else
using SocketLength = socklen_t;

inline int WSAGetLastError() { return errno; }

bool EnsureWinsock(std::string*) { return true; }

constexpr int kWouldBlock = EWOULDBLOCK;
constexpr int kConnectionReset = ECONNRESET;

std::string SocketError(std::string_view what) {
    return std::string(what) + "（errno " + std::to_string(errno) + ": " +
           std::strerror(errno) + "）";
}
#endif

bool ResolveIPv4(std::string_view host, std::uint16_t port, sockaddr_in* address,
                 std::string* error) {
    if (address == nullptr) {
        if (error != nullptr) {
            *error = "地址输出为空";
        }
        return false;
    }

    std::memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons(port);

    if (host.empty() || host == "0.0.0.0" || host == "*") {
        address->sin_addr.s_addr = htonl(INADDR_ANY);
        return true;
    }

    const std::string hostText(host);
    if (inet_pton(AF_INET, hostText.c_str(), &address->sin_addr) == 1) {
        return true;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* result = nullptr;
    const int rc = getaddrinfo(hostText.c_str(), nullptr, &hints, &result);
    if (rc != 0 || result == nullptr) {
        if (error != nullptr) {
            *error = "无法解析主机 '" + hostText + "'（" + std::to_string(rc) + "）";
        }
        if (result != nullptr) {
            freeaddrinfo(result);
        }
        return false;
    }

    const auto* resolved = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    address->sin_addr = resolved->sin_addr;
    freeaddrinfo(result);
    return true;
}

bool SetNonBlocking(SOCKET socket) {
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void SetSocketBuffers(SOCKET socket) {
    int size = 256 * 1024;
    setsockopt(socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size),
               sizeof(size));
    setsockopt(socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&size),
               sizeof(size));
}

SOCKET CreateUdpSocket(std::string* error) {
    SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET) {
        if (error != nullptr) {
            *error = SocketError("创建 UDP socket 失败");
        }
        return INVALID_SOCKET;
    }
    if (!SetNonBlocking(socket)) {
        if (error != nullptr) {
            *error = SocketError("设置 UDP 非阻塞失败");
        }
        closesocket(socket);
        return INVALID_SOCKET;
    }
    SetSocketBuffers(socket);
    return socket;
}

bool SameEndpoint(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_family == b.sin_family && a.sin_port == b.sin_port &&
           a.sin_addr.s_addr == b.sin_addr.s_addr;
}

void ConfigureKcp(ikcpcb* kcp) {
    ikcp_nodelay(kcp, 1, kKcpInterval, kKcpResend, 1);
    ikcp_wndsize(kcp, kKcpWindow, kKcpWindow);
    ikcp_setmtu(kcp, kKcpMtu);
}

}  // namespace

struct KcpClient::Impl {
    SOCKET socket = INVALID_SOCKET;
    ikcpcb* kcp = nullptr;
    sockaddr_in server{};
    std::uint32_t conv = 0;
    bool connected = false;
    FrameDecoder decoder;
    KcpStats stats;
    std::uint64_t lastReceiveMs = 0;

    static int Output(const char* data, int length, ikcpcb*, void* user) {
        auto* self = static_cast<Impl*>(user);
        const int sent = static_cast<int>(send(self->socket, data, length, 0));
        if (sent == SOCKET_ERROR) {
            return -1;
        }
        ++self->stats.datagramsSent;
        self->stats.bytesSent += static_cast<std::uint64_t>(sent);
        return sent;
    }

    void Close() {
        if (kcp != nullptr) {
            ikcp_release(kcp);
            kcp = nullptr;
        }
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
            socket = INVALID_SOCKET;
        }
        connected = false;
        conv = 0;
        decoder.Clear();
    }

    void Drain() {
        for (;;) {
            const int size = ikcp_peeksize(kcp);
            if (size <= 0) {
                break;
            }
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            const int received = ikcp_recv(kcp, reinterpret_cast<char*>(bytes.data()), size);
            if (received < 0) {
                break;
            }
            bytes.resize(static_cast<std::size_t>(received));
            std::string error;
            if (!decoder.Push(bytes, &error)) {
                ++stats.datagramsRejected;
            }
        }
    }
};

KcpClient::KcpClient() : m_impl(std::make_unique<Impl>()) {}
KcpClient::~KcpClient() {
    if (m_impl != nullptr) {
        m_impl->Close();
    }
}

bool KcpClient::Connect(std::string_view host, std::uint16_t port, std::string* error) {
    if (m_impl == nullptr) {
        if (error != nullptr) {
            *error = "客户端未初始化";
        }
        return false;
    }
    m_impl->Close();
    if (!EnsureWinsock(error)) {
        return false;
    }
    if (!ResolveIPv4(host, port, &m_impl->server, error)) {
        return false;
    }

    const SOCKET socket = CreateUdpSocket(error);
    if (socket == INVALID_SOCKET) {
        return false;
    }
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&m_impl->server),
                  sizeof(m_impl->server)) == SOCKET_ERROR) {
        if (error != nullptr) {
            *error = SocketError("连接 UDP 服务器失败");
        }
        closesocket(socket);
        return false;
    }

    std::random_device device;
    std::mt19937 rng(device());
    std::uint32_t conv = rng();
    if (conv == 0) {
        conv = 1;
    }

    ikcpcb* kcp = ikcp_create(conv, m_impl.get());
    if (kcp == nullptr) {
        if (error != nullptr) {
            *error = "创建 KCP 客户端失败";
        }
        closesocket(socket);
        return false;
    }
    ikcp_setoutput(kcp, &Impl::Output);
    ConfigureKcp(kcp);

    m_impl->socket = socket;
    m_impl->kcp = kcp;
    m_impl->conv = conv;
    m_impl->connected = true;
    m_impl->lastReceiveMs = NowMs();
    return true;
}

void KcpClient::Close() {
    if (m_impl != nullptr) {
        m_impl->Close();
    }
}

bool KcpClient::IsConnected() const {
    return m_impl != nullptr && m_impl->connected;
}

void KcpClient::Poll() {
    if (m_impl == nullptr || !m_impl->connected || m_impl->kcp == nullptr) {
        return;
    }

    std::array<char, kReceiveBufferBytes> buffer{};
    for (;;) {
        const int received = static_cast<int>(
            recv(m_impl->socket, buffer.data(), static_cast<int>(buffer.size()), 0));
        if (received == SOCKET_ERROR) {
            const int code = WSAGetLastError();
            if (code == kWouldBlock) {
                break;
            }
            if (code == kConnectionReset) {
                m_impl->Close();
                return;
            }
            ++m_impl->stats.datagramsRejected;
            break;
        }
        if (received <= 0) {
            break;
        }

        ++m_impl->stats.datagramsReceived;
        m_impl->stats.bytesReceived += static_cast<std::uint64_t>(received);
        m_impl->lastReceiveMs = NowMs();
        if (ikcp_input(m_impl->kcp, buffer.data(), received) < 0) {
            ++m_impl->stats.datagramsRejected;
        }
    }

    ikcp_update(m_impl->kcp, static_cast<IUINT32>(NowMs()));
    m_impl->Drain();
}

bool KcpClient::Send(std::span<const std::uint8_t> frame, std::string* error) {
    if (m_impl == nullptr || !m_impl->connected || m_impl->kcp == nullptr) {
        if (error != nullptr) {
            *error = "KCP 客户端未连接";
        }
        return false;
    }
    if (frame.empty()) {
        if (error != nullptr) {
            *error = "不能发送空帧";
        }
        return false;
    }

    const int result =
        ikcp_send(m_impl->kcp, reinterpret_cast<const char*>(frame.data()),
                  static_cast<int>(frame.size()));
    if (result < 0) {
        if (error != nullptr) {
            *error = "KCP 发送失败";
        }
        return false;
    }
    ikcp_update(m_impl->kcp, static_cast<IUINT32>(NowMs()));
    ikcp_flush(m_impl->kcp);
    ++m_impl->stats.framesSent;
    return true;
}

bool KcpClient::Receive(std::vector<std::uint8_t>* frame) {
    if (m_impl == nullptr || !m_impl->decoder.Pop(frame)) {
        return false;
    }
    ++m_impl->stats.framesReceived;
    return true;
}

const KcpStats& KcpClient::GetStats() const {
    static const KcpStats empty;
    return m_impl != nullptr ? m_impl->stats : empty;
}

struct KcpServer::Impl {
    struct Session {
        Impl* owner = nullptr;
        std::uint32_t conv = 0;
        sockaddr_in peer{};
        ikcpcb* kcp = nullptr;
        FrameDecoder decoder;
        std::uint64_t lastReceiveMs = 0;

        static int Output(const char* data, int length, ikcpcb*, void* user);
    };

    SOCKET socket = INVALID_SOCKET;
    std::uint16_t port = 0;
    std::size_t maxConnections = 8;
    std::uint32_t timeoutMs = 30000;
    bool listening = false;
    std::unordered_map<std::uint32_t, std::unique_ptr<Session>> sessions;
    std::deque<std::pair<KcpConnectionId, std::vector<std::uint8_t>>> inbox;
    KcpStats stats;

    void Close() {
        for (auto& [id, session] : sessions) {
            static_cast<void>(id);
            if (session->kcp != nullptr) {
                ikcp_release(session->kcp);
                session->kcp = nullptr;
            }
        }
        sessions.clear();
        inbox.clear();
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
            socket = INVALID_SOCKET;
        }
        listening = false;
        port = 0;
    }

    int RawSend(const sockaddr_in& peer, const char* data, int length) {
        const int sent = static_cast<int>(
            sendto(socket, data, length, 0, reinterpret_cast<const sockaddr*>(&peer),
                   sizeof(peer)));
        if (sent == SOCKET_ERROR) {
            return -1;
        }
        ++stats.datagramsSent;
        stats.bytesSent += static_cast<std::uint64_t>(sent);
        return sent;
    }

    Session* CreateSession(std::uint32_t conv, const sockaddr_in& peer) {
        auto session = std::make_unique<Session>();
        session->owner = this;
        session->conv = conv;
        session->peer = peer;
        session->lastReceiveMs = NowMs();

        ikcpcb* kcp = ikcp_create(conv, session.get());
        if (kcp == nullptr) {
            return nullptr;
        }
        session->kcp = kcp;
        ikcp_setoutput(kcp, &Session::Output);
        ConfigureKcp(kcp);

        Session* raw = session.get();
        sessions.emplace(conv, std::move(session));
        return raw;
    }

    void DestroySession(std::uint32_t conv, Session* session) {
        if (session != nullptr && session->kcp != nullptr) {
            ikcp_release(session->kcp);
        }
        sessions.erase(conv);
    }

    void Drain(Session& session) {
        for (;;) {
            const int size = ikcp_peeksize(session.kcp);
            if (size <= 0) {
                break;
            }
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            const int received =
                ikcp_recv(session.kcp, reinterpret_cast<char*>(bytes.data()), size);
            if (received < 0) {
                break;
            }
            bytes.resize(static_cast<std::size_t>(received));
            std::string error;
            if (!session.decoder.Push(bytes, &error)) {
                ++stats.datagramsRejected;
                continue;
            }
            std::vector<std::uint8_t> frame;
            while (session.decoder.Pop(&frame)) {
                inbox.emplace_back(session.conv, std::move(frame));
            }
        }
    }

    void Poll() {
        if (!listening) {
            return;
        }

        std::array<char, kReceiveBufferBytes> buffer{};
        for (;;) {
            sockaddr_in from{};
            SocketLength fromLength = sizeof(from);
            const int received = static_cast<int>(
                recvfrom(socket, buffer.data(), static_cast<int>(buffer.size()), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLength));
            if (received == SOCKET_ERROR) {
                const int code = WSAGetLastError();
                if (code == kWouldBlock) {
                    break;
                }
                if (code == kConnectionReset) {
                    continue;
                }
                ++stats.datagramsRejected;
                break;
            }
            if (received < static_cast<int>(kKcpHeaderBytes)) {
                ++stats.datagramsRejected;
                continue;
            }

            ++stats.datagramsReceived;
            stats.bytesReceived += static_cast<std::uint64_t>(received);
            const std::uint32_t conv = ikcp_getconv(buffer.data());
            if (conv == 0) {
                ++stats.datagramsRejected;
                continue;
            }

            auto found = sessions.find(conv);
            const bool newSession = found == sessions.end();
            if (newSession) {
                if (sessions.size() >= maxConnections) {
                    ++stats.datagramsRejected;
                    continue;
                }
                Session* session = CreateSession(conv, from);
                if (session == nullptr) {
                    ++stats.datagramsRejected;
                    continue;
                }
                found = sessions.find(conv);
            } else if (!SameEndpoint(found->second->peer, from)) {
                ++stats.datagramsRejected;
                continue;
            }

            Session& session = *found->second;
            session.lastReceiveMs = NowMs();
            if (ikcp_input(session.kcp, buffer.data(), received) < 0) {
                ++stats.datagramsRejected;
                if (newSession) {
                    DestroySession(conv, &session);
                }
                continue;
            }
        }

        const std::uint64_t now = NowMs();
        for (auto& [id, session] : sessions) {
            static_cast<void>(id);
            ikcp_update(session->kcp, static_cast<IUINT32>(now));
            Drain(*session);
        }

        std::vector<std::uint32_t> expired;
        for (const auto& [id, session] : sessions) {
            if (now - session->lastReceiveMs > timeoutMs) {
                expired.push_back(id);
            }
        }
        for (const std::uint32_t id : expired) {
            auto found = sessions.find(id);
            if (found != sessions.end()) {
                DestroySession(id, found->second.get());
            }
        }
    }
};

int KcpServer::Impl::Session::Output(const char* data, int length, ikcpcb*, void* user) {
    auto* session = static_cast<Session*>(user);
    return session->owner->RawSend(session->peer, data, length);
}

KcpServer::KcpServer(std::size_t maxConnections, std::uint32_t timeoutMs)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->maxConnections = std::max<std::size_t>(maxConnections, 1);
    m_impl->timeoutMs = std::max<std::uint32_t>(timeoutMs, 1);
}

KcpServer::~KcpServer() {
    if (m_impl != nullptr) {
        m_impl->Close();
    }
}

bool KcpServer::Listen(std::string_view bindHost, std::uint16_t port, std::string* error) {
    if (m_impl == nullptr) {
        if (error != nullptr) {
            *error = "服务器未初始化";
        }
        return false;
    }
    m_impl->Close();
    if (!EnsureWinsock(error)) {
        return false;
    }

    sockaddr_in address{};
    if (!ResolveIPv4(bindHost, port, &address, error)) {
        return false;
    }

    const SOCKET socket = CreateUdpSocket(error);
    if (socket == INVALID_SOCKET) {
        return false;
    }
    if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
        SOCKET_ERROR) {
        if (error != nullptr) {
            *error = SocketError("绑定 UDP 端口失败");
        }
        closesocket(socket);
        return false;
    }

    sockaddr_in actual{};
    SocketLength actualLength = sizeof(actual);
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&actual), &actualLength) ==
        SOCKET_ERROR) {
        if (error != nullptr) {
            *error = SocketError("读取 UDP 端口失败");
        }
        closesocket(socket);
        return false;
    }

    m_impl->socket = socket;
    m_impl->port = ntohs(actual.sin_port);
    m_impl->listening = true;
    return true;
}

void KcpServer::Close() {
    if (m_impl != nullptr) {
        m_impl->Close();
    }
}

bool KcpServer::IsListening() const {
    return m_impl != nullptr && m_impl->listening;
}

std::uint16_t KcpServer::Port() const {
    return m_impl != nullptr ? m_impl->port : 0;
}

void KcpServer::Poll() {
    if (m_impl != nullptr) {
        m_impl->Poll();
    }
}

bool KcpServer::Send(KcpConnectionId id, std::span<const std::uint8_t> frame,
                     std::string* error) {
    if (m_impl == nullptr || !m_impl->listening) {
        if (error != nullptr) {
            *error = "KCP 服务器未监听";
        }
        return false;
    }
    if (frame.empty()) {
        if (error != nullptr) {
            *error = "不能发送空帧";
        }
        return false;
    }

    const auto found = m_impl->sessions.find(id);
    if (found == m_impl->sessions.end()) {
        if (error != nullptr) {
            *error = "KCP 连接不存在";
        }
        return false;
    }

    Impl::Session& session = *found->second;
    const int result =
        ikcp_send(session.kcp, reinterpret_cast<const char*>(frame.data()),
                  static_cast<int>(frame.size()));
    if (result < 0) {
        if (error != nullptr) {
            *error = "KCP 发送失败";
        }
        return false;
    }
    ikcp_update(session.kcp, static_cast<IUINT32>(NowMs()));
    ikcp_flush(session.kcp);
    ++m_impl->stats.framesSent;
    return true;
}

void KcpServer::Disconnect(KcpConnectionId id) {
    if (m_impl == nullptr) {
        return;
    }
    const auto found = m_impl->sessions.find(id);
    if (found == m_impl->sessions.end()) {
        return;
    }
    m_impl->DestroySession(id, found->second.get());
}

bool KcpServer::Receive(KcpConnectionId* id, std::vector<std::uint8_t>* frame) {
    if (m_impl == nullptr || m_impl->inbox.empty() || id == nullptr || frame == nullptr) {
        return false;
    }
    *id = m_impl->inbox.front().first;
    *frame = std::move(m_impl->inbox.front().second);
    m_impl->inbox.pop_front();
    ++m_impl->stats.framesReceived;
    return true;
}

std::vector<KcpConnectionId> KcpServer::Connections() const {
    std::vector<KcpConnectionId> result;
    if (m_impl == nullptr) {
        return result;
    }
    result.reserve(m_impl->sessions.size());
    for (const auto& [id, session] : m_impl->sessions) {
        static_cast<void>(session);
        result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

const KcpStats& KcpServer::GetStats() const {
    static const KcpStats empty;
    return m_impl != nullptr ? m_impl->stats : empty;
}

}  // namespace zl::net
