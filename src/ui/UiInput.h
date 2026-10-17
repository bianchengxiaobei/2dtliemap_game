#pragma once

#include <RmlUi/Core/Input.h>

namespace zl::core {
class Input;
}

namespace zl::ui {

Rml::Input::KeyIdentifier ToRmlKey(int glfwKey);
int ModifiersFromInput(const core::Input& input);

}  // namespace zl::ui
