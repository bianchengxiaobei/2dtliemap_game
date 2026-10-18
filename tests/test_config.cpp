#include <filesystem>
#include <fstream>
#include <string>

#include <doctest/doctest.h>

#include "core/Config.h"

TEST_CASE("M9-T6 设置写回配置文件") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "zomboidlike_config_t6_test.json";
    std::filesystem::remove(path);
    {
        std::ofstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        file << R"({
  "audio": { "masterVolume": 65 },
  "input": { "bindings": { "MoveUp": "W" } }
})";
    }

    zl::core::Config config;
    REQUIRE(config.LoadFile(path));
    CHECK(config.Get<int>("audio.masterVolume", 0) == 65);
    const char* argv[] = {"ZomboidLikeTests", "--game.startState=world"};
    config.ApplyCommandLine(2, argv);
    CHECK(config.Get<std::string>("game.startState", "") == "world");
    config.Set("audio.masterVolume", 33);
    config.Set("input.bindings.MoveUp", "Q");
    std::string error;
    REQUIRE(config.Save(&error));
    CHECK(error.empty());
    CHECK(config.Get<std::string>("game.startState", "") == "world");

    zl::core::Config reloaded;
    REQUIRE(reloaded.LoadFile(path));
    CHECK(reloaded.Get<int>("audio.masterVolume", 0) == 33);
    CHECK(reloaded.Get<std::string>("input.bindings.MoveUp", "") == "Q");
    CHECK(!reloaded.Has("game.startState"));  // 命令行覆盖不写回文件

    std::filesystem::remove(path);
    std::filesystem::remove(std::filesystem::path(path.string() + ".tmp"));
}
