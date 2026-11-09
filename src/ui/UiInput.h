#pragma once

#include <string>

#include <RmlUi/Core/Input.h>

namespace Rml {
class Context;
}

namespace zl::core {
class Input;
}

namespace zl::ui {

Rml::Input::KeyIdentifier ToRmlKey(int glfwKey);
int ModifiersFromInput(const core::Input& input);

// RmlUi 显式聚焦到输入控件时，应阻断同一帧的游戏按键/鼠标。
bool IsInputFocusBlocking(Rml::Context* context);

// RmlUi 按键标识 → 配置键名（W/SPACE/F1…）；修饰键/未知键返回空串。
std::string KeyNameFromRmlKey(Rml::Input::KeyIdentifier key);

}  // namespace zl::ui
