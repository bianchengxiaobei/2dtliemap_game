#include "core/Input.h"

#include "core/Log.h"

namespace zl::core {

void Input::Bind(std::string action, int keyCode) {
    m_actionToKey[std::move(action)] = keyCode;
}

void Input::ClearBindings() {
    m_actionToKey.clear();
}

bool Input::HasAction(std::string_view action) const {
    return m_actionToKey.contains(std::string(action));
}

int Input::KeyCodeFor(std::string_view action) const {
    const auto it = m_actionToKey.find(std::string(action));
    return it == m_actionToKey.end() ? -1 : it->second;
}

bool Input::IsActionPressed(std::string_view action) const {
    if (m_gameplayBlocked) {
        return false;
    }
    const int code = KeyCodeFor(action);
    if (code < 0 || code >= kKeyCount) {
        return false;
    }
    return m_keyDown[code] && !m_keyPrev[code];
}

bool Input::IsActionDown(std::string_view action) const {
    if (m_gameplayBlocked) {
        return false;
    }
    const int code = KeyCodeFor(action);
    if (code < 0 || code >= kKeyCount) {
        return false;
    }
    return m_keyDown[code];
}

bool Input::IsActionReleased(std::string_view action) const {
    if (m_gameplayBlocked) {
        return false;
    }
    const int code = KeyCodeFor(action);
    if (code < 0 || code >= kKeyCount) {
        return false;
    }
    return !m_keyDown[code] && m_keyPrev[code];
}

void Input::SetKeyState(int keyCode, bool down) {
    if (keyCode >= 0 && keyCode < kKeyCount) {
        m_keyDown[keyCode] = down;
    }
}

void Input::SetMouseButton(int button, bool down) {
    if (button >= 0 && button < kMouseButtonCount) {
        m_mouseDown[button] = down;
    }
}

void Input::LatchMouseButton(int button, bool down) {
    if (button < 0 || button >= kMouseButtonCount) {
        return;
    }
    m_mouseDown[button] = down;
    if (down) {
        m_mousePressedLatch[button] = true;
    }
}

void Input::SetMousePosition(float x, float y) {
    m_mouseX = x;
    m_mouseY = y;
}

void Input::AddScroll(float deltaX, float deltaY) {
    (void)deltaX;
    m_scrollY += deltaY;
}

void Input::NewFrame() {
    m_gameplayBlocked = false;
    m_keyPrev = m_keyDown;
    m_mousePrev = m_mouseDown;
    m_mousePressedLatch.fill(false);
    m_scrollY = 0.0f;
}

bool Input::IsMouseButtonPressed(int button) const {
    if (m_gameplayBlocked) {
        return false;
    }
    if (button < 0 || button >= kMouseButtonCount) {
        return false;
    }
    return (m_mouseDown[button] && !m_mousePrev[button]) ||
           m_mousePressedLatch[button];
}

bool Input::IsMouseButtonPressedRaw(int button) const {
    if (button < 0 || button >= kMouseButtonCount) {
        return false;
    }
    return (m_mouseDown[button] && !m_mousePrev[button]) ||
           m_mousePressedLatch[button];
}

bool Input::IsMouseButtonDown(int button) const {
    if (m_gameplayBlocked) {
        return false;
    }
    if (button < 0 || button >= kMouseButtonCount) {
        return false;
    }
    return m_mouseDown[button];
}

bool Input::IsMouseButtonReleased(int button) const {
    if (m_gameplayBlocked) {
        return false;
    }
    if (button < 0 || button >= kMouseButtonCount) {
        return false;
    }
    return !m_mouseDown[button] && m_mousePrev[button];
}

bool Input::IsKeyDown(int keyCode) const {
    if (m_gameplayBlocked) {
        return false;
    }
    if (keyCode < 0 || keyCode >= kKeyCount) {
        return false;
    }
    return m_keyDown[keyCode];
}

bool Input::IsKeyPressed(int keyCode) const {
    if (m_gameplayBlocked) {
        return false;
    }
    if (keyCode < 0 || keyCode >= kKeyCount) {
        return false;
    }
    return m_keyDown[keyCode] && !m_keyPrev[keyCode];
}

bool Input::IsKeyReleased(int keyCode) const {
    if (m_gameplayBlocked) {
        return false;
    }
    if (keyCode < 0 || keyCode >= kKeyCount) {
        return false;
    }
    return !m_keyDown[keyCode] && m_keyPrev[keyCode];
}

}  // namespace zl::core
