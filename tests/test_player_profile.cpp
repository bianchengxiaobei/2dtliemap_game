#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "net/PlayerIdentity.h"
#include "net/PlayerState.h"
#include "server/PlayerProfile.h"

namespace {

std::filesystem::path TempProfilePath(const char* name) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "zomboidlike_t8_profiles" / name;
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root / "players.bin";
}

}  // namespace

TEST_CASE("M13-T8 PlayerProfileStore：身份分配高位 StableId 并跨加载复用") {
    const std::filesystem::path path = TempProfilePath("roundtrip");

    zl::server::PlayerProfileStore store({path});
    std::string error;
    REQUIRE_MESSAGE(store.Load(&error), error);
    CHECK(store.Size() == 0);
    CHECK(store.Find("alice") == nullptr);

    zl::server::PlayerProfile& alice = store.Create("alice", "Alice");
    zl::server::PlayerProfile& bob = store.Create("bob", "Bob");
    CHECK(alice.stableId >= zl::ecs::EcsWorld::kExternalStableIdStart);
    CHECK(bob.stableId != alice.stableId);
    CHECK(alice.state.playerStableId == alice.stableId);
    CHECK(alice.state.alive);
    alice.state.bag.push_back(zl::net::PlayerStateItem{"bandage", 3, 0.8f});
    alice.state.equipment[0].present = true;
    alice.state.equipment[0].item = zl::net::PlayerStateItem{"axe", 1, 0.6f};
    alice.state.health = 42.0f;
    alice.state.healthMax = 100.0f;
    alice.state.knownRecipes.push_back("field_bandage");
    alice.hasSnapshot = true;
    alice.entity.stableId = alice.stableId;

    REQUIRE_MESSAGE(store.Save(&error), error);

    zl::server::PlayerProfileStore loaded({path});
    REQUIRE_MESSAGE(loaded.Load(&error), error);
    CHECK(loaded.Size() == 2);
    const zl::server::PlayerProfile* restored = loaded.Find("alice");
    REQUIRE(restored != nullptr);
    CHECK(restored->stableId == alice.stableId);
    CHECK(restored->state.health == doctest::Approx(42.0f));
    REQUIRE(restored->state.bag.size() == 1);
    CHECK(restored->state.bag[0].defId == "bandage");
    CHECK(restored->state.bag[0].count == 3);
    REQUIRE(restored->state.equipment[0].present);
    CHECK(restored->state.equipment[0].item.defId == "axe");
    REQUIRE(restored->state.knownRecipes.size() == 1);
    CHECK(restored->state.knownRecipes[0] == "field_bandage");
    CHECK(restored->hasSnapshot);
    CHECK(restored->entity.stableId == alice.stableId);

    const zl::server::PlayerProfile& next = loaded.Create("carol", "Carol");
    CHECK(next.stableId != alice.stableId);
    CHECK(next.stableId != bob.stableId);
    CHECK(next.stableId > bob.stableId);
}

TEST_CASE("M13-T8 PlayerProfileStore：死亡/复活字段与损坏文件拒绝") {
    const std::filesystem::path path = TempProfilePath("death");
    zl::server::PlayerProfileStore store({path});
    std::string error;
    REQUIRE_MESSAGE(store.Load(&error), error);
    zl::server::PlayerProfile& dead = store.Create("dead-player", "Dead");
    dead.alive = false;
    dead.state.alive = false;
    dead.state.health = 0.0f;
    dead.respawnSeconds = 3.5f;
    REQUIRE_MESSAGE(store.Save(&error), error);

    zl::server::PlayerProfileStore loaded({path});
    REQUIRE_MESSAGE(loaded.Load(&error), error);
    const zl::server::PlayerProfile* restored = loaded.Find("dead-player");
    REQUIRE(restored != nullptr);
    CHECK_FALSE(restored->alive);
    CHECK_FALSE(restored->state.alive);
    CHECK(restored->state.health == 0.0f);
    CHECK(restored->respawnSeconds == doctest::Approx(3.5f));

    const std::filesystem::path corrupt = TempProfilePath("corrupt");
    {
        std::ofstream output(corrupt, std::ios::binary | std::ios::trunc);
        output << "not a player profile";
    }
    zl::server::PlayerProfileStore broken({corrupt});
    CHECK_FALSE(broken.Load(&error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("M13-T8 PlayerIdentity：合法格式与落盘复用") {
    const std::filesystem::path path =
        TempProfilePath("identity").parent_path() / "identity.txt";
    std::string error;
    const std::string first = zl::net::LoadOrCreatePlayerIdentity(path, &error);
    CHECK(error.empty());
    CHECK(zl::net::IsValidPlayerIdentity(first));
    const std::string second = zl::net::LoadOrCreatePlayerIdentity(path, &error);
    CHECK(second == first);
    CHECK(zl::net::IsValidPlayerIdentity("player-1"));
    CHECK_FALSE(zl::net::IsValidPlayerIdentity(""));
    CHECK_FALSE(zl::net::IsValidPlayerIdentity("has space"));
    CHECK_FALSE(zl::net::IsValidPlayerIdentity(std::string(65, 'a')));
}
