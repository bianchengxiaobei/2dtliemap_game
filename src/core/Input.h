#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace zl::core {

// 输入系统（M1-T6）：平台层把原始键鼠状态喂进来，这里做 Action 映射与三态查询。
// 帧序：NewFrame()（快照上帧）→ 平台层 PollEvents 更新状态 → 游戏逻辑查询。
class Input {
public:
    static constexpr int kKeyCount = 512;

    void Bind(std::string action, int keyCode);
    void ClearBindings();
    bool HasAction(std::string_view action) const;
    int KeyCodeFor(std::string_view action) const;  // 未绑定返回 -1

    bool IsActionPressed(std::string_view action) const;   // 本帧刚按下
    bool IsActionDown(std::string_view action) const;      // 按住
    bool IsActionReleased(std::string_view action) const;  // 本帧刚松开

    // 平台层填充（每帧 PollEvents 时调用）
    void SetKeyState(int keyCode, bool down);
    void SetMouseButton(int button, bool down);
    // 平台层鼠标回调：即使按下与松开发生在同一帧轮询间隔内，也保留一次按下边沿。
    void LatchMouseButton(int button, bool down);
    void SetMousePosition(float x, float y);
    void AddScroll(float deltaX, float deltaY);

    // 主循环每帧最先调用：生成上一帧快照、清空滚轮增量
    void NewFrame();

    // M9-T4：UI 有焦点时阻断游戏输入（HUD 无焦点时照常透传）。
    // UiSystem 在每帧处理完 UI 后设置，NewFrame 重置为 false。
    void SetGameplayBlocked(bool blocked) { m_gameplayBlocked = blocked; }
    bool IsGameplayBlocked() const { return m_gameplayBlocked; }

    float MouseX() const { return m_mouseX; }
    float MouseY() const { return m_mouseY; }
    float ScrollDeltaY() const { return m_gameplayBlocked ? 0.0f : m_scrollY; }
    bool IsMouseButtonPressed(int button) const;
    // 不受 UI 阻断影响的按下边沿；调用方必须自行确认光标不在 UI 上。
    bool IsMouseButtonPressedRaw(int button) const;
    bool IsMouseButtonDown(int button) const;
    bool IsMouseButtonReleased(int button) const;

    bool IsKeyDown(int keyCode) const;
    bool IsKeyPressed(int keyCode) const;
    bool IsKeyReleased(int keyCode) const;

private:
    static constexpr int kMouseButtonCount = 8;

    std::array<bool, kKeyCount> m_keyDown{};
    std::array<bool, kKeyCount> m_keyPrev{};
    std::array<bool, kMouseButtonCount> m_mouseDown{};
    std::array<bool, kMouseButtonCount> m_mousePrev{};
    std::array<bool, kMouseButtonCount> m_mousePressedLatch{};
    std::unordered_map<std::string, int> m_actionToKey;

    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    float m_scrollY = 0.0f;
    bool m_gameplayBlocked = false;
};

}  // namespace zl::core
