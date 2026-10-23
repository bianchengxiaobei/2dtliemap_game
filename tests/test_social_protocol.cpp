#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/GameTime.h"
#include "net/SocialProtocol.h"

namespace {

std::vector<std::uint8_t> Bytes(std::initializer_list<int> values) {
    std::vector<std::uint8_t> out;
    out.reserve(values.size());
    for (const int value : values) {
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}

}  // namespace

TEST_CASE("M13-T9 社交协议：聊天消息往返与校验") {
    zl::net::ChatMessage message;
    message.text = "有人吗？集合点在北边。";
    std::string error;
    const auto bytes = zl::net::EncodeChatMessage(message, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    CHECK(bytes.size() == 8 + message.text.size());

    zl::net::ChatMessage decoded;
    REQUIRE(zl::net::DecodeChatMessage(bytes, &decoded, &error));
    CHECK(decoded.text == message.text);

    CHECK_FALSE(zl::net::DecodeChatMessage(Bytes({}), &decoded, &error));
    CHECK_FALSE(zl::net::DecodeChatMessage(Bytes({0x5A, 0x4C, 0x48, 0x32}), &decoded,
                                           &error));
    CHECK_FALSE(zl::net::EncodeChatMessage(zl::net::ChatMessage{""}, &error).size());
    CHECK_FALSE(zl::net::EncodeChatMessage(zl::net::ChatMessage{"a\nb"}, &error).size());
    CHECK_FALSE(zl::net::EncodeChatMessage(
                    zl::net::ChatMessage{std::string(zl::net::kMaxChatBytes + 1, 'a')},
                    &error)
                    .size());
    CHECK(zl::net::IsValidChatText("hello"));
    CHECK(zl::net::IsValidChatText("你好"));
    CHECK_FALSE(zl::net::IsValidChatText("\xE4\xBD"));
}

TEST_CASE("M13-T9 社交协议：玩家列表/聊天记录往返与严格校验") {
    zl::net::SocialState state;
    state.serverTick = 120;
    state.sequence = 7;
    state.gameMinutes = 123.5;
    state.players.push_back({1, "Alice"});
    state.players.push_back({2, "小明"});
    state.chat.push_back({118, "Alice", "hello"});
    state.chat.push_back({120, "小明", "你好"});

    std::string error;
    const auto bytes = zl::net::EncodeSocialState(state, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    CHECK(bytes.size() <= zl::net::kMaxSocialStateBytes);

    zl::net::SocialState decoded;
    REQUIRE(zl::net::DecodeSocialState(bytes, &decoded, &error));
    CHECK(decoded.serverTick == 120);
    CHECK(decoded.sequence == 7);
    CHECK(decoded.gameMinutes == 123.5);
    REQUIRE(decoded.players.size() == 2);
    CHECK(decoded.players[0].stableId == 1);
    CHECK(decoded.players[0].name == "Alice");
    REQUIRE(decoded.chat.size() == 2);
    CHECK(decoded.chat[1].sender == "小明");
    CHECK(decoded.chat[1].text == "你好");

    zl::net::SocialState bad = state;
    bad.players[1].stableId = 1;
    CHECK_FALSE(zl::net::EncodeSocialState(bad, &error).size());

    bad = state;
    bad.players[0].stableId = 0;
    CHECK_FALSE(zl::net::EncodeSocialState(bad, &error).size());

    bad = state;
    bad.chat[0].serverTick = 121;
    CHECK_FALSE(zl::net::EncodeSocialState(bad, &error).size());

    bad = state;
    bad.chat[0].text = "line\r\nbreak";
    CHECK_FALSE(zl::net::EncodeSocialState(bad, &error).size());

    auto truncated = bytes;
    truncated.pop_back();
    CHECK_FALSE(zl::net::DecodeSocialState(truncated, &decoded, &error));

    auto version = bytes;
    version[4] = 3;
    CHECK_FALSE(zl::net::DecodeSocialState(version, &decoded, &error));

    auto extra = bytes;
    extra.push_back(0);
    CHECK_FALSE(zl::net::DecodeSocialState(extra, &decoded, &error));
}

TEST_CASE("M13-T9 社交协议：玩家/聊天数量与字节上限") {
    zl::net::SocialState state;
    for (std::size_t i = 0; i < zl::net::kMaxSocialPlayers + 1; ++i) {
        state.players.push_back({i + 1, "P" + std::to_string(i)});
    }
    std::string error;
    CHECK_FALSE(zl::net::EncodeSocialState(state, &error).size());

    state.players.clear();
    for (std::size_t i = 0; i < zl::net::kMaxSocialChatMessages + 1; ++i) {
        state.chat.push_back({0, "P", "x"});
    }
    CHECK_FALSE(zl::net::EncodeSocialState(state, &error).size());

    state.chat.clear();
    state.chat.push_back({0, "P", std::string(zl::net::kMaxChatBytes, 'x')});
    CHECK(!zl::net::EncodeSocialState(state, &error).empty());
}

TEST_CASE("M14-T9 联机时间：按服务器总游戏分钟重建日期") {
    zl::core::GameTime time;
    time.Configure(0.4, zl::core::DateTime{2000, 7, 9, 0, 0});
    time.SetTotalGameMinutes(1950.0);
    CHECK(time.Now().year == 2000);
    CHECK(time.Now().month == 7);
    CHECK(time.Now().day == 10);
    CHECK(time.Now().hour == 8);
    CHECK(time.Now().minute == 30);
    CHECK(time.TotalGameMinutes() == 1950.0);

    time.Configure(0.4, zl::core::DateTime{2000, 7, 9, 23, 30});
    time.SetTotalGameMinutes(60.0);
    CHECK(time.Now().day == 10);
    CHECK(time.Now().hour == 0);
    CHECK(time.Now().minute == 30);
}
