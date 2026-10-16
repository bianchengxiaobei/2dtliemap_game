#pragma once

#include <string_view>

namespace zl::platform {

// 配置里的键名（大小写不敏感）→ GLFW 键值；未识别返回 -1。
// 支持：A-Z、0-9、F1-F12、方向键、SPACE/ESCAPE/ENTER/TAB、SHIFT/CTRL/ALT、
//      BACKSPACE/DELETE/INSERT/HOME/END/PAGEUP/PAGEDOWN 及常用标点。
int KeyCodeFromName(std::string_view name);

}  // namespace zl::platform
