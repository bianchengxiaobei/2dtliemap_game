#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include "game/EmbeddedServer.h"
#include "game/NetworkClient.h"

TEST_CASE("M13-T9 内嵌主机：启动后可连接并收发社交状态") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    const std::filesystem::path old = std::filesystem::current_path();
    std::filesystem::current_path(root);

    const std::filesystem::path profiles =
        std::filesystem::temp_directory_path() / "zl-embedded-server-test.bin";
    std::error_code ignored;
    std::filesystem::remove(profiles, ignored);

    zl::game::EmbeddedServer server;
    zl::game::EmbeddedServer::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.bindHost = "127.0.0.1";
    options.port = 0;
    options.contentHash = 0x454D424544444544ull;
    options.spawnDemoPopulation = false;
    options.playerProfilesFile = profiles.string();
    std::string error;
    REQUIRE_MESSAGE(server.Start(options, &error), error);
    REQUIRE(server.Running());
    REQUIRE(server.Port() != 0);

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = server.Port();
    clientOptions.contentHash = options.contentHash;
    clientOptions.autoReconnect = false;
    clientOptions.playerName = "HostTester";
    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        client.Poll();
        if (client.IsConnected() && client.HasPlayer() && client.HasSocialState() &&
            client.SocialState().players.size() == 1) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(client.IsConnected());
    CHECK(client.HasPlayer());
    REQUIRE(client.HasSocialState());
    REQUIRE(client.SocialState().players.size() == 1);
    CHECK(client.SocialState().players[0].name == "HostTester");

    client.Close();
    server.Stop();
    CHECK(!server.Running());
    std::filesystem::remove(profiles, ignored);
    std::filesystem::current_path(old);
}
