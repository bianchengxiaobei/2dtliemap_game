#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "game/ServerList.h"

namespace {

std::filesystem::path TempPath(const char* name) {
    const std::filesystem::path root =
        std::filesystem::path(ZL_SOURCE_DIR) / "build" / "test-server-list" / name;
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root / "servers.json";
}

}  // namespace

TEST_CASE("M13-T9 服务器列表：新增、去重、排序与持久化") {
    const std::filesystem::path path = TempPath("roundtrip");
    zl::game::ServerListStore store(path);
    std::string error;
    REQUIRE_MESSAGE(store.Load(&error), error);
    CHECK(store.Entries().empty());

    REQUIRE(store.Upsert("本地", "127.0.0.1", 7777, &error));
    REQUIRE(store.Upsert("远程", "192.168.1.20", 7788, &error));
    REQUIRE(store.Upsert("本地（新）", "127.0.0.1", 7777, &error));
    REQUIRE(store.Entries().size() == 2);
    CHECK(store.Entries()[0].name == "本地（新）");
    CHECK(store.Entries()[1].host == "192.168.1.20");
    REQUIRE_MESSAGE(store.Save(&error), error);

    zl::game::ServerListStore reloaded(path);
    REQUIRE_MESSAGE(reloaded.Load(&error), error);
    REQUIRE(reloaded.Entries().size() == 2);
    CHECK(reloaded.Entries()[0].name == "本地（新）");
    CHECK(reloaded.Entries()[0].port == 7777);

    CHECK_FALSE(reloaded.Upsert("", "localhost", 7777, &error));
    CHECK_FALSE(reloaded.Upsert("bad", "bad host", 7777, &error));
    CHECK_FALSE(reloaded.Upsert("bad", "localhost", 0, &error));
}

TEST_CASE("M13-T9 服务器列表：数量上限与损坏文件") {
    const std::filesystem::path path = TempPath("limits");
    zl::game::ServerListStore store(path);
    std::string error;
    for (std::size_t i = 0; i < zl::game::ServerListStore::kMaxEntries + 3; ++i) {
        REQUIRE(store.Upsert("S" + std::to_string(i), "10.0.0." + std::to_string(i + 1),
                             7000 + static_cast<std::uint16_t>(i), &error));
    }
    CHECK(store.Entries().size() == zl::game::ServerListStore::kMaxEntries);
    REQUIRE_MESSAGE(store.Save(&error), error);

    std::ofstream broken(path, std::ios::binary | std::ios::trunc);
    broken << "{not json";
    broken.close();
    zl::game::ServerListStore bad(path);
    CHECK_FALSE(bad.Load(&error));
    CHECK(bad.Entries().empty());

    std::ofstream partial(path, std::ios::binary | std::ios::trunc);
    partial << R"({"servers":[{"name":"ok","host":"127.0.0.1","port":7777},)"
               R"({"name":"bad","host":"","port":7778},)"
               R"({"name":"dup","host":"127.0.0.1","port":7777}]})";
    partial.close();
    zl::game::ServerListStore filtered(path);
    REQUIRE_MESSAGE(filtered.Load(&error), error);
    REQUIRE(filtered.Entries().size() == 1);
    CHECK(filtered.Entries()[0].name == "ok");
}
