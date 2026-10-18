#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <doctest/doctest.h>

#include "core/Input.h"
#include "platform/KeyNames.h"
#include "ui/InventoryPanel.h"
#include "ui/InventoryUi.h"
#include "ui/MenuPanel.h"
#include "ui/MenuUi.h"
#include "ui/UiInput.h"

namespace {

void AppendUtf8(std::string& out, char32_t codepoint) {
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
                                                Rml::Span<const int>) override {
        return 0;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f,
                        Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String&) override {
        dimensions = Rml::Vector2i(0, 0);
        return 0;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
        return 0;
    }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

class RecordingRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
                                                Rml::Span<const int>) override {
        return m_nextGeometry++;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f,
                        Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String&) override {
        dimensions = Rml::Vector2i(0, 0);
        return 0;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,
                                       Rml::Vector2i dimensions) override {
        const Rml::TextureHandle handle = m_nextTexture++;
        m_textures[handle] = dimensions;
        return handle;
    }
    void ReleaseTexture(Rml::TextureHandle texture) override { m_textures.erase(texture); }
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}

    const std::map<Rml::TextureHandle, Rml::Vector2i>& Textures() const { return m_textures; }

private:
    Rml::CompiledGeometryHandle m_nextGeometry = 1;
    Rml::TextureHandle m_nextTexture = 1;
    std::map<Rml::TextureHandle, Rml::Vector2i> m_textures;
};

class NullSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return 0.0; }
    bool LogMessage(Rml::Log::Type, const Rml::String&) override { return true; }
};

class TestListener final : public Rml::EventListener {
public:
    explicit TestListener(bool& flag) : m_flag(flag) {}
    void ProcessEvent(Rml::Event&) override { m_flag = true; }

private:
    bool& m_flag;
};

}  // namespace

TEST_CASE("M9-T1 UI 输入映射与边沿") {
    CHECK(zl::ui::ToRmlKey(GLFW_KEY_A) == Rml::Input::KI_A);
    CHECK(zl::ui::ToRmlKey(GLFW_KEY_9) == Rml::Input::KI_9);
    CHECK(zl::ui::ToRmlKey(GLFW_KEY_F11) == Rml::Input::KI_F11);
    CHECK(zl::ui::ToRmlKey(GLFW_KEY_ESCAPE) == Rml::Input::KI_ESCAPE);
    CHECK(zl::ui::ToRmlKey(GLFW_KEY_LEFT) == Rml::Input::KI_LEFT);
    CHECK(zl::ui::ToRmlKey(0) == Rml::Input::KI_UNKNOWN);

    CHECK(zl::platform::KeyNameFromCode(GLFW_KEY_W) == "W");
    CHECK(zl::platform::KeyNameFromCode(GLFW_KEY_1) == "1");
    CHECK(zl::platform::KeyNameFromCode(GLFW_KEY_F11) == "F11");
    CHECK(zl::platform::KeyNameFromCode(GLFW_KEY_ESCAPE) == "ESCAPE");
    CHECK(zl::platform::KeyNameFromCode(GLFW_KEY_LEFT_SHIFT) == "LSHIFT");
    CHECK(zl::platform::KeyNameFromCode(0).empty());
    CHECK(zl::platform::KeyCodeFromName("esc") == GLFW_KEY_ESCAPE);

    zl::core::Input input;
    input.SetKeyState(GLFW_KEY_LEFT_SHIFT, true);
    input.SetKeyState(GLFW_KEY_LEFT_CONTROL, true);
    const int modifiers = zl::ui::ModifiersFromInput(input);
    CHECK((modifiers & Rml::Input::KM_SHIFT) != 0);
    CHECK((modifiers & Rml::Input::KM_CTRL) != 0);
    CHECK((modifiers & Rml::Input::KM_ALT) == 0);

    zl::core::Input edges;
    edges.NewFrame();
    edges.SetKeyState(GLFW_KEY_A, true);
    CHECK(edges.IsKeyPressed(GLFW_KEY_A));
    CHECK(edges.IsKeyDown(GLFW_KEY_A));
    CHECK(!edges.IsKeyReleased(GLFW_KEY_A));
    edges.NewFrame();
    edges.SetKeyState(GLFW_KEY_A, false);
    CHECK(edges.IsKeyReleased(GLFW_KEY_A));
    CHECK(!edges.IsKeyPressed(GLFW_KEY_A));

    edges.NewFrame();
    edges.SetMouseButton(0, true);
    CHECK(edges.IsMouseButtonPressed(0));
    edges.NewFrame();
    edges.SetMouseButton(0, false);
    CHECK(edges.IsMouseButtonReleased(0));
}

TEST_CASE("M9-T1 RmlUi 文档加载与点击路由（无渲染器）") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontPath =
        std::string(ZL_SOURCE_DIR) + "/assets/fonts/NotoSansSC-Regular.otf";
    CHECK(Rml::LoadFontFace(fontPath, "zl_ui", Rml::Style::FontStyle::Normal,
                            Rml::Style::FontWeight::Normal));

    Rml::Context* context = Rml::CreateContext("test", Rml::Vector2i(200, 200));
    REQUIRE(context != nullptr);
    Rml::ElementDocument* document = context->LoadDocumentFromMemory(R"rml(
<rml>
<head>
<style>
body { font-family: zl_ui; width: 100%; height: 100%; }
#btn { position: absolute; left: 20px; top: 20px; width: 80px; height: 40px; background: #ffffff; }
</style>
</head>
<body>
<div id="btn">按钮</div>
</body>
</rml>)rml");
    REQUIRE(document != nullptr);
    document->Show();
    context->Update();

    Rml::Element* button = document->GetElementById("btn");
    REQUIRE(button != nullptr);
    bool clicked = false;
    TestListener listener(clicked);
    button->AddEventListener("click", &listener);

    context->ProcessMouseMove(40, 40, 0);
    context->ProcessMouseButtonDown(0, 0);
    context->ProcessMouseButtonUp(0, 0);
    context->Update();
    CHECK(clicked);

    Rml::RemoveContext("test");
    Rml::Shutdown();
}

TEST_CASE("M9-T2 主题文档、圆角与 Tab 焦点") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontPath =
        std::string(ZL_SOURCE_DIR) + "/assets/fonts/NotoSansSC-Regular.otf";
    CHECK(Rml::LoadFontFace(fontPath, "zl_ui", Rml::Style::FontStyle::Normal,
                            Rml::Style::FontWeight::Normal));

    Rml::Context* context = Rml::CreateContext("theme", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    const std::string documentPath = std::string(ZL_SOURCE_DIR) + "/assets/ui/debug.rml";
    Rml::ElementDocument* document = context->LoadDocument(documentPath);
    REQUIRE(document != nullptr);
    document->Show();
    context->Update();

    Rml::Element* panel = document->GetElementById("panel");
    REQUIRE(panel != nullptr);
    CHECK(panel->GetComputedValues().border_top_left_radius() > 0.0f);
    CHECK(document->GetElementById("scroll") != nullptr);
    CHECK(document->GetElementById("volume") != nullptr);
    CHECK(document->GetElementById("ninepatch-demo") != nullptr);
    CHECK(document->GetElementById("disabled-sample")->HasAttribute("disabled"));

    context->ProcessKeyDown(Rml::Input::KI_TAB, 0);
    context->Update();
    Rml::Element* firstFocus = context->GetFocusElement();
    CHECK(firstFocus != nullptr);
    context->ProcessKeyUp(Rml::Input::KI_TAB, 0);

    context->ProcessKeyDown(Rml::Input::KI_TAB, 0);
    context->Update();
    Rml::Element* secondFocus = context->GetFocusElement();
    CHECK(secondFocus != nullptr);
    CHECK(secondFocus != firstFocus);
    context->ProcessKeyUp(Rml::Input::KI_TAB, 0);

    Rml::RemoveContext("theme");
    Rml::Shutdown();
}

TEST_CASE("M9-T3 字体映射、混排换行、省略号与富文本") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Bold.ttf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Bold));

    Rml::Context* context = Rml::CreateContext("typography", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    Rml::ElementDocument* document =
        context->LoadDocument(std::string(ZL_SOURCE_DIR) + "/assets/ui/debug.rml");
    REQUIRE(document != nullptr);
    document->Show();
    context->Update();

    Rml::Element* panel = document->GetElementById("type-panel");
    REQUIRE(panel != nullptr);
    CHECK(panel->GetFontFaceHandle() != 0);

    Rml::Element* xl = document->GetElementById("type-xl");
    Rml::Element* bold = document->GetElementById("weight-bold");
    Rml::Element* richBold = document->GetElementById("rich-bold-inline");
    Rml::Element* tagBold = document->GetElementById("rich-bold");
    REQUIRE(xl != nullptr);
    REQUIRE(bold != nullptr);
    REQUIRE(richBold != nullptr);
    REQUIRE(tagBold != nullptr);
    CHECK(xl->GetComputedValues().font_size() == doctest::Approx(22.0f));
    CHECK(bold->GetComputedValues().font_weight() == Rml::Style::FontWeight::Bold);
    CHECK(richBold->GetComputedValues().font_weight() == Rml::Style::FontWeight::Bold);
    CHECK(tagBold->GetComputedValues().font_weight() == Rml::Style::FontWeight::Bold);
    CHECK(bold->GetFontFaceHandle() != 0);
    CHECK(bold->GetFontFaceHandle() != document->GetElementById("weight-regular")->GetFontFaceHandle());

    Rml::Element* wrap = document->GetElementById("wrap-demo");
    REQUIRE(wrap != nullptr);
    CHECK(wrap->GetComputedValues().word_break() == Rml::Style::WordBreak::BreakWord);
    CHECK(wrap->GetClientWidth() <= 388.0f);
    CHECK(wrap->GetClientHeight() > 30.0f);

    Rml::Element* ellipsis = document->GetElementById("ellipsis-demo");
    REQUIRE(ellipsis != nullptr);
    CHECK(ellipsis->GetComputedValues().white_space() == Rml::Style::WhiteSpace::Nowrap);
    CHECK(ellipsis->GetComputedValues().text_overflow() == Rml::Style::TextOverflow::Ellipsis);
    Rml::Element* accent = document->GetElementById("rich-accent");
    REQUIRE(accent != nullptr);
    const Rml::Colourb accentColor = accent->GetComputedValues().color();
    CHECK(accentColor.red == 255);
    CHECK(accentColor.green == 212);
    CHECK(accentColor.blue == 121);
    CHECK(document->GetElementById("atlas-stats") != nullptr);

    Rml::RemoveContext("typography");
    Rml::Shutdown();
}

TEST_CASE("M9-T4 HUD 文档与游戏输入阻断") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Bold.ttf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Bold));

    Rml::Context* context = Rml::CreateContext("hud", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    Rml::ElementDocument* debugDocument =
        context->LoadDocument(std::string(ZL_SOURCE_DIR) + "/assets/ui/debug.rml");
    REQUIRE(debugDocument != nullptr);
    debugDocument->Show();
    debugDocument->Hide();
    Rml::ElementDocument* document =
        context->LoadDocument(std::string(ZL_SOURCE_DIR) + "/assets/ui/hud.rml");
    REQUIRE(document != nullptr);
    document->Show();
    context->Update();

    CHECK(document->GetElementById("hud-day") != nullptr);
    CHECK(document->GetElementById("hud-time") != nullptr);
    CHECK(document->GetElementById("hud-speed") != nullptr);
    CHECK(document->GetElementById("hp-fill") != nullptr);
    CHECK(document->GetElementById("stamina-fill") != nullptr);
    CHECK(document->GetElementById("notice") != nullptr);
    Rml::Element* firstSlot = document->GetElementById("slot-0");
    REQUIRE(firstSlot != nullptr);
    CHECK(firstSlot->IsClassSet("selected"));
    CHECK(!document->GetElementById("slot-1")->IsClassSet("selected"));
    for (int i = 0; i < 5; ++i) {
        CHECK(document->GetElementById("slot-name-" + std::to_string(i)) != nullptr);
        CHECK(document->GetElementById("slot-count-" + std::to_string(i)) != nullptr);
    }

    Rml::RemoveContext("hud");
    Rml::Shutdown();

    zl::core::Input input;
    input.Bind("MoveUp", GLFW_KEY_W);
    input.NewFrame();
    input.SetKeyState(GLFW_KEY_W, true);
    input.AddScroll(0.0f, 1.0f);
    input.SetMouseButton(0, true);
    CHECK(input.IsActionPressed("MoveUp"));
    CHECK(input.IsKeyDown(GLFW_KEY_W));
    CHECK(input.ScrollDeltaY() == doctest::Approx(1.0f));
    CHECK(input.IsMouseButtonPressed(0));

    input.SetGameplayBlocked(true);
    CHECK(input.IsGameplayBlocked());
    CHECK(!input.IsActionPressed("MoveUp"));
    CHECK(!input.IsActionDown("MoveUp"));
    CHECK(!input.IsKeyDown(GLFW_KEY_W));
    CHECK(input.ScrollDeltaY() == doctest::Approx(0.0f));
    CHECK(!input.IsMouseButtonPressed(0));
    CHECK(!input.IsMouseButtonDown(0));

    input.NewFrame();
    CHECK(!input.IsGameplayBlocked());
    CHECK(input.IsKeyDown(GLFW_KEY_W));
    CHECK(input.IsMouseButtonDown(0));
}

TEST_CASE("M9-T3 CJK 按需字形与图集预算") {
    RecordingRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));

    Rml::Context* context = Rml::CreateContext("atlas", Rml::Vector2i(1024, 3000));
    REQUIRE(context != nullptr);

    std::string corpus;
    for (int i = 0; i < 1500; ++i) {
        AppendUtf8(corpus, static_cast<char32_t>(0x4E00 + i));
    }
    const std::string documentSource =
        "<rml><head><style>"
        "body { font-family: zl_ui; font-size: 16px; width: 1000px; }"
        "</style></head><body><div id=\"cjk\">" +
        corpus + "</div></body></rml>";
    Rml::ElementDocument* document = context->LoadDocumentFromMemory(documentSource);
    REQUIRE(document != nullptr);
    document->Show();
    context->Update();
    context->Render();

    const auto& textures = renderInterface.Textures();
    REQUIRE(!textures.empty());
    uint64_t bytes = 0;
    int maxDimension = 0;
    for (const auto& entry : textures) {
        const Rml::Vector2i dimensions = entry.second;
        maxDimension = std::max(maxDimension, std::max(dimensions.x, dimensions.y));
        bytes += static_cast<uint64_t>(dimensions.x) * static_cast<uint64_t>(dimensions.y) * 4;
    }
    CHECK(maxDimension <= 1024);
    CHECK(textures.size() <= 2);
    CHECK(bytes <= 8ull * 1024 * 1024);

    Rml::RemoveContext("atlas");
    Rml::Shutdown();
}

TEST_CASE("M9-T5 背包状态渲染与右键菜单") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Bold.ttf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Bold));

    Rml::Context* context = Rml::CreateContext("inventory", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    zl::ui::InventoryPanel panel;
    REQUIRE(panel.Init(*context, std::string(ZL_SOURCE_DIR) + "/assets/ui/inventory.rml"));

    std::vector<zl::ui::InventoryAction> actions;
    panel.SetActionHandler(
        [&actions](const zl::ui::InventoryAction& action) { actions.push_back(action); });

    zl::ui::InventoryState state;
    state.playerValid = true;
    state.weight = 4.2f;
    state.capacity = 20.0f;
    state.bagStacks = 2;
    state.bag[0] = zl::ui::InventorySlot{true, "绷带", 3, true, false, 0};
    state.bag[1] = zl::ui::InventorySlot{true, "消防斧", 1, false, true, 0};
    state.equipment[0] = zl::ui::InventorySlot{true, "消防斧", 1, false, false, 0};
    state.loot[0] = zl::ui::InventorySlot{true, "破布", 2, false, false, 9001};
    state.lootStacks = 1;
    panel.SetState(state);
    panel.Show();
    context->Update();

    Rml::ElementDocument* document = panel.Document();
    REQUIRE(document != nullptr);
    Rml::Element* bag0 = document->GetElementById("bag-slot-0");
    Rml::Element* bag1 = document->GetElementById("bag-slot-1");
    REQUIRE(bag0 != nullptr);
    REQUIRE(bag1 != nullptr);
    CHECK(panel.IsOpen());
    CHECK(bag0->IsClassSet("filled"));
    CHECK(bag1->IsClassSet("filled"));
    CHECK(!document->GetElementById("bag-slot-2")->IsClassSet("filled"));
    CHECK(document->GetElementById("bag-name-0")->GetInnerRML() == "绷带");
    CHECK(document->GetElementById("bag-count-0")->GetInnerRML() == "3");
    CHECK(document->GetElementById("equip-slot-0")->IsClassSet("filled"));
    CHECK(document->GetElementById("equip-name-0")->GetInnerRML() == "消防斧");
    CHECK(document->GetElementById("loot-slot-0")->IsClassSet("filled"));
    CHECK(document->GetElementById("loot-slot-0")->GetAttribute<Rml::String>("data-entity", "") ==
          "9001");
    CHECK(document->GetElementById("inv-weight")->GetInnerRML().find("4.2 / 20.0") !=
          std::string::npos);
    CHECK(bag0->GetComputedValues().drag() == Rml::Style::Drag::Clone);

    auto clickElement = [&](Rml::Element* element, int button) {
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        context->ProcessMouseMove(static_cast<int>(offset.x) + 8,
                                  static_cast<int>(offset.y) + 8, 0);
        context->ProcessMouseButtonDown(button, 0);
        context->ProcessMouseButtonUp(button, 0);
        context->Update();
    };

    auto rightClick = [&](Rml::Element* element) {
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        context->ProcessMouseMove(static_cast<int>(offset.x) + 8,
                                  static_cast<int>(offset.y) + 8, 0);
        context->ProcessMouseButtonDown(1, 0);
        context->Update();
    };

    rightClick(bag0);
    Rml::Element* menu = document->GetElementById("context-menu");
    REQUIRE(menu != nullptr);
    CHECK(menu->IsVisible());
    CHECK(document->GetElementById("ctx-use")->IsVisible());
    CHECK(!document->GetElementById("ctx-equip")->IsVisible());
    CHECK(document->GetElementById("ctx-drop")->IsVisible());
    clickElement(document->GetElementById("ctx-use"), 0);
    REQUIRE(actions.size() == 1);
    CHECK(actions[0].type == zl::ui::InventoryActionType::Use);
    CHECK(actions[0].from == zl::ui::InventoryArea::Backpack);
    CHECK(actions[0].fromIndex == 0);

    rightClick(document->GetElementById("loot-slot-0"));
    REQUIRE(menu->IsVisible());
    CHECK(document->GetElementById("ctx-pickup")->IsVisible());
    clickElement(document->GetElementById("ctx-pickup"), 0);
    REQUIRE(actions.size() == 2);
    CHECK(actions[1].type == zl::ui::InventoryActionType::Pickup);
    CHECK(actions[1].from == zl::ui::InventoryArea::Loot);
    CHECK(actions[1].fromEntityId == 9001);

    rightClick(document->GetElementById("equip-slot-0"));
    REQUIRE(menu->IsVisible());
    CHECK(document->GetElementById("ctx-unequip")->IsVisible());
    clickElement(document->GetElementById("ctx-unequip"), 0);
    REQUIRE(actions.size() == 3);
    CHECK(actions[2].type == zl::ui::InventoryActionType::Unequip);
    CHECK(actions[2].from == zl::ui::InventoryArea::Equipment);
    CHECK(actions[2].fromIndex == 0);

    panel.Shutdown();
    Rml::RemoveContext("inventory");
    Rml::Shutdown();
}

TEST_CASE("M9-T5 背包拖拽转移") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));

    Rml::Context* context = Rml::CreateContext("inventory-drag", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    zl::ui::InventoryPanel panel;
    REQUIRE(panel.Init(*context, std::string(ZL_SOURCE_DIR) + "/assets/ui/inventory.rml"));

    std::vector<zl::ui::InventoryAction> actions;
    panel.SetActionHandler(
        [&actions](const zl::ui::InventoryAction& action) { actions.push_back(action); });

    zl::ui::InventoryState state;
    state.playerValid = true;
    state.bagStacks = 2;
    state.bag[0] = zl::ui::InventorySlot{true, "破布", 2, false, false, 0};
    state.bag[1] = zl::ui::InventorySlot{true, "绷带", 1, true, false, 0};
    panel.SetState(state);
    panel.Show();
    context->Update();

    Rml::Element* source = panel.Document()->GetElementById("bag-slot-0");
    Rml::Element* target = panel.Document()->GetElementById("bag-slot-1");
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);
    const Rml::Vector2f sourceOffset = source->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f targetOffset = target->GetAbsoluteOffset(Rml::BoxArea::Border);
    context->ProcessMouseMove(static_cast<int>(sourceOffset.x) + 8,
                              static_cast<int>(sourceOffset.y) + 8, 0);
    context->ProcessMouseButtonDown(0, 0);
    context->Update();
    context->ProcessMouseMove(static_cast<int>(targetOffset.x) + 8,
                              static_cast<int>(targetOffset.y) + 8, 0);
    context->Update();
    context->ProcessMouseButtonUp(0, 0);
    context->Update();

    REQUIRE(actions.size() == 1);
    CHECK(actions[0].type == zl::ui::InventoryActionType::Transfer);
    CHECK(actions[0].from == zl::ui::InventoryArea::Backpack);
    CHECK(actions[0].fromIndex == 0);
    CHECK(actions[0].to == zl::ui::InventoryArea::Backpack);
    CHECK(actions[0].toIndex == 1);

    panel.Shutdown();
    Rml::RemoveContext("inventory-drag");
    Rml::Shutdown();
}
TEST_CASE("M9-T5 背包 1280x720 与 1920x1080 布局") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));

    const Rml::Vector2i sizes[] = {Rml::Vector2i(1280, 720), Rml::Vector2i(1920, 1080)};
    for (int i = 0; i < 2; ++i) {
        const std::string name = "inventory-layout-" + std::to_string(i);
        Rml::Context* context = Rml::CreateContext(name, sizes[i]);
        REQUIRE(context != nullptr);
        zl::ui::InventoryPanel panel;
        REQUIRE(panel.Init(*context, std::string(ZL_SOURCE_DIR) +
                                         "/assets/ui/inventory.rml"));
        zl::ui::InventoryState state;
        state.playerValid = true;
        state.bag[0] = zl::ui::InventorySlot{true, "绷带", 2, true, false, 0};
        state.bagStacks = 1;
        panel.SetState(state);
        panel.Show();
        context->Update();

        Rml::Element* element = panel.Document()->GetElementById("inventory-panel");
        REQUIRE(element != nullptr);
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f box = element->GetBox().GetSize(Rml::BoxArea::Border);
        CHECK(offset.x >= 0.0f);
        CHECK(offset.y >= 0.0f);
        CHECK(offset.x + box.x <= static_cast<float>(sizes[i].x));
        CHECK(offset.y + box.y <= static_cast<float>(sizes[i].y));
        CHECK(box.x > 700.0f);
        CHECK(box.y > 500.0f);
        CHECK(panel.Document()->GetElementById("bag-slot-24") != nullptr);

        panel.Shutdown();
        Rml::RemoveContext(name);
    }
    Rml::Shutdown();
}
TEST_CASE("M9-T6 主菜单、暂停菜单与设置页") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Bold.ttf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Bold));

    Rml::Context* context = Rml::CreateContext("menu", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    zl::ui::MenuPanel panel;
    REQUIRE(panel.Init(*context, std::string(ZL_SOURCE_DIR) + "/assets/ui/menu.rml"));

    std::vector<zl::ui::MenuActionType> actions;
    panel.SetActionHandler(
        [&actions](zl::ui::MenuActionType action) { actions.push_back(action); });

    zl::ui::MenuSettings settings;
    settings.volume = 40;
    settings.musicVolume = 30;
    settings.sfxVolume = 60;
    settings.ambientVolume = 20;
    const std::pair<const char*, const char*> bindings[] = {
        {"MoveUp", "W"},      {"MoveDown", "S"}, {"MoveLeft", "A"},
        {"MoveRight", "D"},   {"Attack", "SPACE"}, {"Pickup", "F"},
        {"Use", "E"},         {"Equip", "R"},     {"Drop", "G"},
        {"Pause", "F1"},
    };
    for (const auto& binding : bindings) {
        settings.bindings.push_back(
            zl::ui::MenuKeyBinding{binding.first, binding.first, binding.second});
    }
    panel.SetSettings(settings);

    zl::ui::MenuSettings applied;
    int applyCount = 0;
    panel.SetSettingsHandler(
        [&applied, &applyCount](const zl::ui::MenuSettings& value, std::string& error) {
            applied = value;
            ++applyCount;
            error.clear();
            return true;
        });

    panel.ShowMainMenu(false);
    context->Update();
    REQUIRE(panel.IsOpen());
    Rml::ElementDocument* document = panel.Document();
    REQUIRE(document != nullptr);
    CHECK(document->GetElementById("main-menu")->IsVisible());
    CHECK(!document->GetElementById("pause-menu")->IsVisible());
    CHECK(document->GetElementById("menu-continue")->IsClassSet("disabled"));
    CHECK(document->GetElementById("menu-continue")->GetInnerRML().find("暂无") !=
          std::string::npos);

    auto clickElement = [&](const char* id) {
        Rml::Element* element = document->GetElementById(id);
        REQUIRE(element != nullptr);
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        context->ProcessMouseMove(static_cast<int>(offset.x) + 8,
                                  static_cast<int>(offset.y) + 8, 0);
        context->ProcessMouseButtonDown(0, 0);
        context->ProcessMouseButtonUp(0, 0);
        context->Update();
    };

    clickElement("menu-continue");
    CHECK(actions.empty());
    int soundClicks = 0;
    panel.SetSoundHandler([&soundClicks] { ++soundClicks; });
    clickElement("menu-settings");
    CHECK(soundClicks == 1);
    CHECK(panel.IsSettingsOpen());
    CHECK(document->GetElementById("settings-menu")->IsVisible());
    Rml::Element* settingsElement = document->GetElementById("settings-menu");
    const Rml::Vector2f settingsOffset =
        settingsElement->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f settingsSize = settingsElement->GetBox().GetSize(Rml::BoxArea::Border);
    CHECK(settingsOffset.x >= 0.0f);
    CHECK(settingsOffset.y >= 0.0f);
    CHECK(settingsOffset.x + settingsSize.x <= 1280.0f);
    CHECK(settingsOffset.y + settingsSize.y <= 720.0f);

    clickElement("bind-MoveUp");
    CHECK(panel.IsCapturingKey());
    CHECK(panel.CaptureKey(GLFW_KEY_Q));
    CHECK(!panel.IsCapturingKey());
    CHECK(document->GetElementById("bind-MoveUp")->GetInnerRML() == "Q");

    clickElement("settings-apply");
    REQUIRE(applyCount == 1);
    CHECK(applied.volume == 40);
    CHECK(applied.musicVolume == 30);
    CHECK(applied.sfxVolume == 60);
    CHECK(applied.ambientVolume == 20);
    REQUIRE(applied.bindings.size() == 10);
    CHECK(applied.bindings[0].key == "Q");

    clickElement("settings-back");
    CHECK(!panel.IsSettingsOpen());
    CHECK(document->GetElementById("main-menu")->IsVisible());

    clickElement("menu-new-game");
    REQUIRE(actions.size() == 1);
    CHECK(actions[0] == zl::ui::MenuActionType::NewGame);

    panel.ShowMainMenu(true);
    context->Update();
    CHECK(!document->GetElementById("menu-continue")->IsClassSet("disabled"));
    clickElement("menu-continue");
    REQUIRE(actions.size() == 2);
    CHECK(actions[1] == zl::ui::MenuActionType::Continue);

    panel.ShowPauseMenu();
    context->Update();
    CHECK(document->GetElementById("pause-menu")->IsVisible());
    clickElement("pause-resume");
    REQUIRE(actions.size() == 3);
    CHECK(actions[2] == zl::ui::MenuActionType::Resume);

    panel.ShowPauseMenu();
    context->Update();
    CHECK(panel.OnEscape());
    REQUIRE(actions.size() == 4);
    CHECK(actions[3] == zl::ui::MenuActionType::Resume);

    panel.ShowMainMenu(false);
    context->Update();
    clickElement("menu-settings");
    CHECK(panel.IsSettingsOpen());
    CHECK(panel.OnEscape());
    context->Update();
    CHECK(!panel.IsSettingsOpen());
    CHECK(document->GetElementById("main-menu")->IsVisible());
    CHECK(panel.OnEscape());  // 主菜单 ESC 保持打开
    CHECK(panel.IsOpen());

    panel.Hide();
    CHECK(!panel.IsOpen());
    panel.Shutdown();
    Rml::RemoveContext("menu");
    Rml::Shutdown();
}