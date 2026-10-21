#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace zl::ui {

struct SocialPlayerUi {
    std::uint64_t stableId = 0;
    std::string name;
    bool local = false;
};

struct SocialChatUi {
    std::string sender;
    std::string text;
};

struct SocialUiState {
    bool visible = false;
    std::vector<SocialPlayerUi> players;
    std::vector<SocialChatUi> chat;
    std::string status;
    std::string statusTone = "info";
};

using SocialChatHandler =
    std::function<bool(const std::string& text, std::string& error)>;

}  // namespace zl::ui
