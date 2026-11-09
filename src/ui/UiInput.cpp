#include "ui/UiInput.h"

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>

#include "core/Input.h"
#include "platform/KeyNames.h"

namespace zl::ui {

Rml::Input::KeyIdentifier ToRmlKey(int glfwKey) {
    switch (glfwKey) {
        case GLFW_KEY_A: return Rml::Input::KI_A;
        case GLFW_KEY_B: return Rml::Input::KI_B;
        case GLFW_KEY_C: return Rml::Input::KI_C;
        case GLFW_KEY_D: return Rml::Input::KI_D;
        case GLFW_KEY_E: return Rml::Input::KI_E;
        case GLFW_KEY_F: return Rml::Input::KI_F;
        case GLFW_KEY_G: return Rml::Input::KI_G;
        case GLFW_KEY_H: return Rml::Input::KI_H;
        case GLFW_KEY_I: return Rml::Input::KI_I;
        case GLFW_KEY_J: return Rml::Input::KI_J;
        case GLFW_KEY_K: return Rml::Input::KI_K;
        case GLFW_KEY_L: return Rml::Input::KI_L;
        case GLFW_KEY_M: return Rml::Input::KI_M;
        case GLFW_KEY_N: return Rml::Input::KI_N;
        case GLFW_KEY_O: return Rml::Input::KI_O;
        case GLFW_KEY_P: return Rml::Input::KI_P;
        case GLFW_KEY_Q: return Rml::Input::KI_Q;
        case GLFW_KEY_R: return Rml::Input::KI_R;
        case GLFW_KEY_S: return Rml::Input::KI_S;
        case GLFW_KEY_T: return Rml::Input::KI_T;
        case GLFW_KEY_U: return Rml::Input::KI_U;
        case GLFW_KEY_V: return Rml::Input::KI_V;
        case GLFW_KEY_W: return Rml::Input::KI_W;
        case GLFW_KEY_X: return Rml::Input::KI_X;
        case GLFW_KEY_Y: return Rml::Input::KI_Y;
        case GLFW_KEY_Z: return Rml::Input::KI_Z;

        case GLFW_KEY_0: return Rml::Input::KI_0;
        case GLFW_KEY_1: return Rml::Input::KI_1;
        case GLFW_KEY_2: return Rml::Input::KI_2;
        case GLFW_KEY_3: return Rml::Input::KI_3;
        case GLFW_KEY_4: return Rml::Input::KI_4;
        case GLFW_KEY_5: return Rml::Input::KI_5;
        case GLFW_KEY_6: return Rml::Input::KI_6;
        case GLFW_KEY_7: return Rml::Input::KI_7;
        case GLFW_KEY_8: return Rml::Input::KI_8;
        case GLFW_KEY_9: return Rml::Input::KI_9;

        case GLFW_KEY_BACKSPACE: return Rml::Input::KI_BACK;
        case GLFW_KEY_TAB: return Rml::Input::KI_TAB;
        case GLFW_KEY_ENTER: return Rml::Input::KI_RETURN;
        case GLFW_KEY_PAUSE: return Rml::Input::KI_PAUSE;
        case GLFW_KEY_CAPS_LOCK: return Rml::Input::KI_CAPITAL;
        case GLFW_KEY_ESCAPE: return Rml::Input::KI_ESCAPE;
        case GLFW_KEY_SPACE: return Rml::Input::KI_SPACE;
        case GLFW_KEY_PAGE_UP: return Rml::Input::KI_PRIOR;
        case GLFW_KEY_PAGE_DOWN: return Rml::Input::KI_NEXT;
        case GLFW_KEY_END: return Rml::Input::KI_END;
        case GLFW_KEY_HOME: return Rml::Input::KI_HOME;
        case GLFW_KEY_LEFT: return Rml::Input::KI_LEFT;
        case GLFW_KEY_UP: return Rml::Input::KI_UP;
        case GLFW_KEY_RIGHT: return Rml::Input::KI_RIGHT;
        case GLFW_KEY_DOWN: return Rml::Input::KI_DOWN;
        case GLFW_KEY_PRINT_SCREEN: return Rml::Input::KI_SNAPSHOT;
        case GLFW_KEY_INSERT: return Rml::Input::KI_INSERT;
        case GLFW_KEY_DELETE: return Rml::Input::KI_DELETE;

        case GLFW_KEY_LEFT_SUPER: return Rml::Input::KI_LWIN;
        case GLFW_KEY_RIGHT_SUPER: return Rml::Input::KI_RWIN;

        case GLFW_KEY_KP_0: return Rml::Input::KI_NUMPAD0;
        case GLFW_KEY_KP_1: return Rml::Input::KI_NUMPAD1;
        case GLFW_KEY_KP_2: return Rml::Input::KI_NUMPAD2;
        case GLFW_KEY_KP_3: return Rml::Input::KI_NUMPAD3;
        case GLFW_KEY_KP_4: return Rml::Input::KI_NUMPAD4;
        case GLFW_KEY_KP_5: return Rml::Input::KI_NUMPAD5;
        case GLFW_KEY_KP_6: return Rml::Input::KI_NUMPAD6;
        case GLFW_KEY_KP_7: return Rml::Input::KI_NUMPAD7;
        case GLFW_KEY_KP_8: return Rml::Input::KI_NUMPAD8;
        case GLFW_KEY_KP_9: return Rml::Input::KI_NUMPAD9;
        case GLFW_KEY_KP_ENTER: return Rml::Input::KI_NUMPADENTER;
        case GLFW_KEY_KP_MULTIPLY: return Rml::Input::KI_MULTIPLY;
        case GLFW_KEY_KP_ADD: return Rml::Input::KI_ADD;
        case GLFW_KEY_KP_SUBTRACT: return Rml::Input::KI_SUBTRACT;
        case GLFW_KEY_KP_DECIMAL: return Rml::Input::KI_DECIMAL;
        case GLFW_KEY_KP_DIVIDE: return Rml::Input::KI_DIVIDE;

        case GLFW_KEY_F1: return Rml::Input::KI_F1;
        case GLFW_KEY_F2: return Rml::Input::KI_F2;
        case GLFW_KEY_F3: return Rml::Input::KI_F3;
        case GLFW_KEY_F4: return Rml::Input::KI_F4;
        case GLFW_KEY_F5: return Rml::Input::KI_F5;
        case GLFW_KEY_F6: return Rml::Input::KI_F6;
        case GLFW_KEY_F7: return Rml::Input::KI_F7;
        case GLFW_KEY_F8: return Rml::Input::KI_F8;
        case GLFW_KEY_F9: return Rml::Input::KI_F9;
        case GLFW_KEY_F10: return Rml::Input::KI_F10;
        case GLFW_KEY_F11: return Rml::Input::KI_F11;
        case GLFW_KEY_F12: return Rml::Input::KI_F12;

        case GLFW_KEY_NUM_LOCK: return Rml::Input::KI_NUMLOCK;
        case GLFW_KEY_SCROLL_LOCK: return Rml::Input::KI_SCROLL;
        case GLFW_KEY_LEFT_SHIFT: return Rml::Input::KI_LSHIFT;
        case GLFW_KEY_LEFT_CONTROL: return Rml::Input::KI_LCONTROL;
        case GLFW_KEY_RIGHT_SHIFT: return Rml::Input::KI_RSHIFT;
        case GLFW_KEY_RIGHT_CONTROL: return Rml::Input::KI_RCONTROL;
        case GLFW_KEY_MENU: return Rml::Input::KI_LMENU;
        case GLFW_KEY_KP_EQUAL: return Rml::Input::KI_OEM_NEC_EQUAL;
        default: break;
    }
    return Rml::Input::KI_UNKNOWN;
}

int ModifiersFromInput(const core::Input& input) {
    int modifiers = 0;
    if (input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT)) {
        modifiers |= Rml::Input::KM_SHIFT;
    }
    if (input.IsKeyDown(GLFW_KEY_LEFT_CONTROL) || input.IsKeyDown(GLFW_KEY_RIGHT_CONTROL)) {
        modifiers |= Rml::Input::KM_CTRL;
    }
    if (input.IsKeyDown(GLFW_KEY_LEFT_ALT) || input.IsKeyDown(GLFW_KEY_RIGHT_ALT)) {
        modifiers |= Rml::Input::KM_ALT;
    }
    return modifiers;
}

bool IsInputFocusBlocking(Rml::Context* context) {
    if (context == nullptr) {
        return false;
    }
    Rml::Element* focus = context->GetFocusElement();
    if (focus == nullptr) {
        return false;
    }
    // RmlUi 在无显式焦点时会返回 body；它不是输入控件，不应阻断游戏输入。
    const Rml::String tag = focus->GetTagName();
    return tag != "body" && tag != "html";
}

std::string KeyNameFromRmlKey(Rml::Input::KeyIdentifier key) {
    if (key == Rml::Input::KI_UNKNOWN) {
        return {};
    }
    for (int glfwKey = GLFW_KEY_SPACE; glfwKey <= GLFW_KEY_LAST; ++glfwKey) {
        if (ToRmlKey(glfwKey) != key) {
            continue;
        }
        if (glfwKey == GLFW_KEY_LEFT_SHIFT || glfwKey == GLFW_KEY_RIGHT_SHIFT ||
            glfwKey == GLFW_KEY_LEFT_CONTROL || glfwKey == GLFW_KEY_RIGHT_CONTROL ||
            glfwKey == GLFW_KEY_LEFT_ALT || glfwKey == GLFW_KEY_RIGHT_ALT ||
            glfwKey == GLFW_KEY_LEFT_SUPER || glfwKey == GLFW_KEY_RIGHT_SUPER) {
            return {};
        }
        return platform::KeyNameFromCode(glfwKey);
    }
    return {};
}

}  // namespace zl::ui
