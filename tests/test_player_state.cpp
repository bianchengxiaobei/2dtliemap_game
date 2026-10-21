#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "net/PlayerState.h"

using namespace zl;

TEST_CASE("M13-T7 玩家状态协议：背包/装备/配方/制作往返") {
    net::PlayerState state;
    state.playerStableId = 42;
    state.serverTick = 1234;
    state.capacityKg = 25.5f;
    state.health = 37.0f;
    state.healthMax = 120.0f;
    state.alive = true;
    state.respawnSeconds = 0.0f;
    state.bag.push_back(net::PlayerStateItem{"bandage", 3, 0.75f});
    state.bag.push_back(net::PlayerStateItem{"axe", 1, 0.5f});
    state.equipment[0].present = true;
    state.equipment[0].item = net::PlayerStateItem{"jacket", 1, 1.0f};
    state.knownRecipes = {"campfire_beans", "field_bandage"};
    state.craftingActive = true;
    state.craftingRecipeId = "field_bandage";
    state.craftingElapsed = 0.25f;
    state.craftingDuration = 0.8f;
    state.craftingToolEfficiency = 0.9f;
    state.batchTotal = 3;
    state.batchCompleted = 1;
    state.craftQueue.push_back(net::PlayerStateCraftQueue{"field_bandage", 2});
    state.statusSequence = 7;
    state.statusTone = 1;
    state.status = "拾取了绷带 ×3";

    std::string error;
    const std::vector<std::uint8_t> bytes = net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    CHECK(bytes.size() <= net::kMaxPlayerStateBytes);

    net::PlayerState decoded;
    REQUIRE_MESSAGE(net::DecodePlayerState(bytes, &decoded, &error), error);
    CHECK(decoded.playerStableId == 42);
    CHECK(decoded.serverTick == 1234);
    CHECK(decoded.capacityKg == doctest::Approx(25.5f));
    CHECK(decoded.health == doctest::Approx(37.0f));
    CHECK(decoded.healthMax == doctest::Approx(120.0f));
    CHECK(decoded.alive);
    CHECK(decoded.respawnSeconds == doctest::Approx(0.0f));
    REQUIRE(decoded.bag.size() == 2);
    CHECK(decoded.bag[0].defId == "bandage");
    CHECK(decoded.bag[0].count == 3);
    CHECK(decoded.bag[0].condition == doctest::Approx(0.75f));
    CHECK(decoded.equipment[0].present);
    CHECK(decoded.equipment[0].item.defId == "jacket");
    CHECK_FALSE(decoded.equipment[1].present);
    CHECK(decoded.knownRecipes.size() == 2);
    CHECK(decoded.craftingActive);
    CHECK(decoded.craftingRecipeId == "field_bandage");
    CHECK(decoded.craftingElapsed == doctest::Approx(0.25f));
    CHECK(decoded.craftingDuration == doctest::Approx(0.8f));
    CHECK(decoded.craftingToolEfficiency == doctest::Approx(0.9f));
    CHECK(decoded.batchTotal == 3);
    CHECK(decoded.batchCompleted == 1);
    REQUIRE(decoded.craftQueue.size() == 1);
    CHECK(decoded.craftQueue[0].recipeId == "field_bandage");
    CHECK(decoded.craftQueue[0].count == 2);
    CHECK(decoded.statusSequence == 7);
    CHECK(decoded.statusTone == 1);
    CHECK(decoded.status == "拾取了绷带 ×3");
}

TEST_CASE("M13-T7 玩家状态协议：拒绝截断、缺少玩家与超长提示") {
    net::PlayerState state;
    state.playerStableId = 1;
    state.capacityKg = 20.0f;
    std::string error;
    const std::vector<std::uint8_t> bytes = net::EncodePlayerState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);

    net::PlayerState decoded;
    const std::vector<std::uint8_t> truncated(bytes.begin(), bytes.begin() + 12);
    CHECK_FALSE(net::DecodePlayerState(truncated, &decoded, &error));

    net::PlayerState missingPlayer;
    missingPlayer.capacityKg = 20.0f;
    CHECK(net::EncodePlayerState(missingPlayer, &error).empty());

    net::PlayerState longStatus;
    longStatus.playerStableId = 1;
    longStatus.capacityKg = 20.0f;
    longStatus.status.assign(net::kMaxPlayerStateStatusBytes + 1, 'x');
    CHECK(net::EncodePlayerState(longStatus, &error).empty());

    net::PlayerState badItem;
    badItem.playerStableId = 1;
    badItem.capacityKg = 20.0f;
    badItem.bag.push_back(net::PlayerStateItem{"bandage", 0, 1.0f});
    CHECK(net::EncodePlayerState(badItem, &error).empty());

    net::PlayerState badDeath;
    badDeath.playerStableId = 1;
    badDeath.capacityKg = 20.0f;
    badDeath.alive = false;
    badDeath.health = 10.0f;
    CHECK(net::EncodePlayerState(badDeath, &error).empty());
}
