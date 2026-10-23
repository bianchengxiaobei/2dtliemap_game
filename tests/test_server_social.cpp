#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include "core/Log.h"
#include "game/NetworkClient.h"
#include "server/ServerHost.h"

namespace {

using Clock = std::chrono::steady_clock;

template <typename Predicate>
bool PumpUntil(zl::server::ServerHost& host, zl::game::NetworkClient& first,
               zl::game::NetworkClient& second, int timeoutMs, Predicate predicate) {
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(timeoutMs);
    Clock::time_point nextTick = Clock::now();
    while (Clock::now() < deadline) {
        if (Clock::now() >= nextTick) {
            host.Tick(1.0f / 60.0f);
            nextTick += std::chrono::milliseconds(16);
        }
        first.Poll();
        second.Poll();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

bool HasPlayer(const zl::net::SocialState& state, const std::string& name) {
    for (const zl::net::SocialPlayer& player : state.players) {
        if (player.name == name) {
            return true;
        }
    }
    return false;
}

bool HasChat(const zl::net::SocialState& state, const std::string& text) {
    for (const zl::net::SocialChatLine& line : state.chat) {
        if (line.text == text) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("M13-T9 联机社交：玩家列表与聊天广播") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    const std::filesystem::path old = std::filesystem::current_path();
    std::filesystem::current_path(root);

    zl::server::ServerHost::Options hostOptions;
    hostOptions.configPath = (root / "config" / "game.json").string();
    hostOptions.spawnDemoPopulation = false;
    hostOptions.spawnLocalPlayer = false;
    hostOptions.network = true;
    hostOptions.port = 0;
    hostOptions.contentHash = 0x5A4C544E455439ull;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(hostOptions, &error), error);
    REQUIRE(host.NetworkEnabled());

    zl::game::NetworkClient::Options options;
    options.host = "127.0.0.1";
    options.port = host.NetworkPort();
    options.contentHash = hostOptions.contentHash;
    options.autoReconnect = false;
    options.playerName = "Alice";
    zl::game::NetworkClient alice(options);
    options.playerName = "Bob";
    zl::game::NetworkClient bob(options);
    REQUIRE(alice.Connect(&error));
    REQUIRE(bob.Connect(&error));

    REQUIRE(PumpUntil(host, alice, bob, 8000, [&] {
        return alice.IsConnected() && bob.IsConnected() && alice.HasPlayer() &&
               bob.HasPlayer() && alice.HasSocialState() && bob.HasSocialState() &&
               alice.SocialState().players.size() == 2 &&
               bob.SocialState().players.size() == 2;
    }));
    CHECK(HasPlayer(alice.SocialState(), "Alice"));
    CHECK(HasPlayer(alice.SocialState(), "Bob"));
    CHECK(alice.SocialState().players[0].stableId !=
          alice.SocialState().players[1].stableId);
    CHECK(alice.SocialState().gameMinutes > 0.0);
    CHECK(alice.SocialState().gameMinutes == bob.SocialState().gameMinutes);

    REQUIRE(alice.SendChat("集合点在北边", &error));
    REQUIRE(PumpUntil(host, alice, bob, 4000, [&] {
        return bob.HasSocialState() && HasChat(bob.SocialState(), "集合点在北边");
    }));
    CHECK(alice.SocialState().sequence == bob.SocialState().sequence);
    CHECK(host.GetNetworkStats().chatMessagesReceived >= 1);
    CHECK(host.GetNetworkStats().socialStatesSent >= 2);

    const std::size_t chatCount = bob.SocialState().chat.size();
    REQUIRE(alice.SendChat("太快了", &error));
    REQUIRE(PumpUntil(host, alice, bob, 1000, [&] {
        return host.GetNetworkStats().chatMessagesRejected >= 1;
    }));
    CHECK(bob.SocialState().chat.size() == chatCount);

    alice.Close();
    REQUIRE(PumpUntil(host, alice, bob, 3000, [&] {
        return host.SessionCount() == 1;
    }));
    REQUIRE(PumpUntil(host, alice, bob, 3000, [&] {
        return bob.HasSocialState() && bob.SocialState().players.size() == 1;
    }));
    CHECK(bob.SocialState().players[0].name == "Bob");

    bob.Close();
    host.Shutdown();
    std::filesystem::current_path(old);
}
