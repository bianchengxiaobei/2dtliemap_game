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
    void SetMousePosition(float x, float y);
    void AddScroll(float deltaX, float deltaY);

    // 主循环每帧最先调用：生成上一帧快照、清空滚轮增量
    void NewFrame();

    float MouseX() const { return m_mouseX; }
    float MouseY() const { return m_mouseY; }
    float ScrollDeltaY() const { return m_scrollY; }
    bool IsMouseButtonPressed(int button) const;
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
    std::unordered_map<std::string, int> m_actionToKey;

    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    float m_scrollY = 0.0f;
};

}  // namespace zl::core
