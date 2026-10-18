#include "platform/KeyNames.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

#include <GLFW/glfw3.h>

namespace zl::platform {
namespace {

const std::unordered_map<std::string, int>& KeyTable() {
    static const std::unordered_map<std::string, int> table = [] {
        std::unordered_map<std::string, int> t;
        for (int c = 'A'; c <= 'Z'; ++c) {
            t[std::string(1, static_cast<char>(c))] = GLFW_KEY_A + (c - 'A');
        }
        for (int d = '0'; d <= '9'; ++d) {
            t[std::string(1, static_cast<char>(d))] = GLFW_KEY_0 + (d - '0');
        }
        for (int f = 1; f <= 12; ++f) {
            t["F" + std::to_string(f)] = GLFW_KEY_F1 + (f - 1);
        }
        t["UP"] = GLFW_KEY_UP;
        t["DOWN"] = GLFW_KEY_DOWN;
        t["LEFT"] = GLFW_KEY_LEFT;
        t["RIGHT"] = GLFW_KEY_RIGHT;
        t["SPACE"] = GLFW_KEY_SPACE;
        t["ESCAPE"] = GLFW_KEY_ESCAPE;
        t["ESC"] = GLFW_KEY_ESCAPE;
        t["ENTER"] = GLFW_KEY_ENTER;
        t["RETURN"] = GLFW_KEY_ENTER;
        t["TAB"] = GLFW_KEY_TAB;
        t["SHIFT"] = GLFW_KEY_LEFT_SHIFT;
        t["LSHIFT"] = GLFW_KEY_LEFT_SHIFT;
        t["RSHIFT"] = GLFW_KEY_RIGHT_SHIFT;
        t["CTRL"] = GLFW_KEY_LEFT_CONTROL;
        t["LCTRL"] = GLFW_KEY_LEFT_CONTROL;
        t["RCTRL"] = GLFW_KEY_RIGHT_CONTROL;
        t["ALT"] = GLFW_KEY_LEFT_ALT;
        t["LALT"] = GLFW_KEY_LEFT_ALT;
        t["RALT"] = GLFW_KEY_RIGHT_ALT;
        t["BACKSPACE"] = GLFW_KEY_BACKSPACE;
        t["DELETE"] = GLFW_KEY_DELETE;
        t["INSERT"] = GLFW_KEY_INSERT;
        t["HOME"] = GLFW_KEY_HOME;
        t["END"] = GLFW_KEY_END;
        t["PAGEUP"] = GLFW_KEY_PAGE_UP;
        t["PAGEDOWN"] = GLFW_KEY_PAGE_DOWN;
        t["COMMA"] = GLFW_KEY_COMMA;
        t["PERIOD"] = GLFW_KEY_PERIOD;
        t["SLASH"] = GLFW_KEY_SLASH;
        t["SEMICOLON"] = GLFW_KEY_SEMICOLON;
        t["APOSTROPHE"] = GLFW_KEY_APOSTROPHE;
        t["LBRACKET"] = GLFW_KEY_LEFT_BRACKET;
        t["RBRACKET"] = GLFW_KEY_RIGHT_BRACKET;
        t["BACKSLASH"] = GLFW_KEY_BACKSLASH;
        t["MINUS"] = GLFW_KEY_MINUS;
        t["EQUAL"] = GLFW_KEY_EQUAL;
        t["GRAVE"] = GLFW_KEY_GRAVE_ACCENT;
        return t;
    }();
    return table;
}

}  // namespace

int KeyCodeFromName(std::string_view name) {
    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const auto& table = KeyTable();
    const auto it = table.find(key);
    return it == table.end() ? -1 : it->second;
}

std::string KeyNameFromCode(int keyCode) {
    if (keyCode >= GLFW_KEY_A && keyCode <= GLFW_KEY_Z) {
        return std::string(1, static_cast<char>('A' + (keyCode - GLFW_KEY_A)));
    }
    if (keyCode >= GLFW_KEY_0 && keyCode <= GLFW_KEY_9) {
        return std::string(1, static_cast<char>('0' + (keyCode - GLFW_KEY_0)));
    }
    if (keyCode >= GLFW_KEY_F1 && keyCode <= GLFW_KEY_F12) {
        return "F" + std::to_string(keyCode - GLFW_KEY_F1 + 1);
    }
    switch (keyCode) {
        case GLFW_KEY_UP: return "UP";
        case GLFW_KEY_DOWN: return "DOWN";
        case GLFW_KEY_LEFT: return "LEFT";
        case GLFW_KEY_RIGHT: return "RIGHT";
        case GLFW_KEY_SPACE: return "SPACE";
        case GLFW_KEY_ESCAPE: return "ESCAPE";
        case GLFW_KEY_ENTER: return "ENTER";
        case GLFW_KEY_TAB: return "TAB";
        case GLFW_KEY_LEFT_SHIFT: return "LSHIFT";
        case GLFW_KEY_RIGHT_SHIFT: return "RSHIFT";
        case GLFW_KEY_LEFT_CONTROL: return "LCTRL";
        case GLFW_KEY_RIGHT_CONTROL: return "RCTRL";
        case GLFW_KEY_LEFT_ALT: return "LALT";
        case GLFW_KEY_RIGHT_ALT: return "RALT";
        case GLFW_KEY_BACKSPACE: return "BACKSPACE";
        case GLFW_KEY_DELETE: return "DELETE";
        case GLFW_KEY_INSERT: return "INSERT";
        case GLFW_KEY_HOME: return "HOME";
        case GLFW_KEY_END: return "END";
        case GLFW_KEY_PAGE_UP: return "PAGEUP";
        case GLFW_KEY_PAGE_DOWN: return "PAGEDOWN";
        case GLFW_KEY_COMMA: return "COMMA";
        case GLFW_KEY_PERIOD: return "PERIOD";
        case GLFW_KEY_SLASH: return "SLASH";
        case GLFW_KEY_SEMICOLON: return "SEMICOLON";
        case GLFW_KEY_APOSTROPHE: return "APOSTROPHE";
        case GLFW_KEY_LEFT_BRACKET: return "LBRACKET";
        case GLFW_KEY_RIGHT_BRACKET: return "RBRACKET";
        case GLFW_KEY_BACKSLASH: return "BACKSLASH";
        case GLFW_KEY_MINUS: return "MINUS";
        case GLFW_KEY_EQUAL: return "EQUAL";
        case GLFW_KEY_GRAVE_ACCENT: return "GRAVE";
        default: return {};
    }
}

}  // namespace zl::platform
