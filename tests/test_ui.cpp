#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <doctest/doctest.h>

#include "core/Input.h"
#include "game/ActionHints.h"
#include "game/RunStats.h"
#include "platform/KeyNames.h"
#include "ui/HudState.h"
#include "ui/InventoryPanel.h"
#include "ui/InventoryUi.h"
#include "ui/MenuPanel.h"
#include "ui/MenuUi.h"
#include "ui/MultiplayerPanel.h"
#include "ui/MultiplayerUi.h"
#include "ui/SocialPanel.h"
#include "ui/SocialUi.h"
#include "ui/UiInput.h"
#include "ui/VehicleModsPanel.h"
#include "ui/VehicleModsUi.h"
#include "ui/WorldMapUi.h"

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

struct LayoutRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

LayoutRect BorderRect(Rml::Element* element) {
    const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
    return {offset.x, offset.y, offset.x + size.x, offset.y + size.y};
}

bool Overlaps(const LayoutRect& a, const LayoutRect& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom &&
           a.bottom > b.top;
}

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
    CHECK(document->GetElementById("combat-status") != nullptr);
    CHECK(document->GetElementById("stealth-status") != nullptr);
    CHECK(document->GetElementById("stealth-prompt") != nullptr);
    CHECK(document->GetElementById("ranged-status") != nullptr);
    CHECK(document->GetElementById("forage-status") != nullptr);
    CHECK(document->GetElementById("fishing-status") != nullptr);
    CHECK(document->GetElementById("needs-status") != nullptr);
    CHECK(document->GetElementById("wound-status") != nullptr);
    CHECK(document->GetElementById("pain-status") != nullptr);
    CHECK(document->GetElementById("armor-status") != nullptr);
    CHECK(document->GetElementById("infection-status") != nullptr);
    CHECK(document->GetElementById("weather-status") != nullptr);
    CHECK(document->GetElementById("weather-overlay") != nullptr);
    CHECK(document->GetElementById("threat-status") != nullptr);
    CHECK(document->GetElementById("threat-left") != nullptr);
    CHECK(document->GetElementById("threat-right") != nullptr);
    CHECK(document->GetElementById("threat-top") != nullptr);
    CHECK(document->GetElementById("threat-bottom") != nullptr);
    Rml::Element* threatLeft = document->GetElementById("threat-left");
    REQUIRE(threatLeft != nullptr);
    threatLeft->SetProperty("display", "block");
    context->Update();
    CHECK(threatLeft->GetClientWidth() >= 6.0f);
    CHECK(threatLeft->GetClientHeight() > 0.0f);
    CHECK(document->GetElementById("hud-objective") != nullptr);

    // core_ui 脚本 HUD 使用 mods/core_ui/ui/hud.rml（与 native fallback 是两份文件），
    // 必须保持同结构；否则脚本路径下新增 HUD 行/边缘提示会静默缺失。
    Rml::ElementDocument* scriptDocument = context->LoadDocument(
        std::string(ZL_SOURCE_DIR) + "/mods/core_ui/ui/hud.rml");
    REQUIRE(scriptDocument != nullptr);
    CHECK(scriptDocument->GetElementById("combat-status") != nullptr);
    CHECK(scriptDocument->GetElementById("stealth-status") != nullptr);
    CHECK(scriptDocument->GetElementById("stealth-prompt") != nullptr);
    CHECK(scriptDocument->GetElementById("ranged-status") != nullptr);
    CHECK(scriptDocument->GetElementById("forage-status") != nullptr);
    CHECK(scriptDocument->GetElementById("fishing-status") != nullptr);
    CHECK(scriptDocument->GetElementById("wound-status") != nullptr);
    CHECK(scriptDocument->GetElementById("pain-status") != nullptr);
    CHECK(scriptDocument->GetElementById("armor-status") != nullptr);
    CHECK(scriptDocument->GetElementById("infection-status") != nullptr);
    CHECK(scriptDocument->GetElementById("weather-status") != nullptr);
    CHECK(scriptDocument->GetElementById("weather-overlay") != nullptr);
    CHECK(scriptDocument->GetElementById("threat-status") != nullptr);
    for (const char* id : {"threat-left", "threat-right", "threat-top", "threat-bottom"}) {
        CHECK(scriptDocument->GetElementById(id) != nullptr);
    }
    Rml::Element* scriptThreatLeft = scriptDocument->GetElementById("threat-left");
    REQUIRE(scriptThreatLeft != nullptr);
    scriptThreatLeft->SetProperty("display", "block");
    context->Update();
    CHECK(scriptThreatLeft->GetClientWidth() >= 6.0f);
    CHECK(scriptThreatLeft->GetClientHeight() > 0.0f);
    CHECK(document->GetElementById("objective-text") != nullptr);
    CHECK(document->GetElementById("objective-hint") != nullptr);
    CHECK(document->GetElementById("poi-guide") != nullptr);
    CHECK(scriptDocument->GetElementById("poi-guide") != nullptr);
    CHECK(document->GetElementById("mission-guide") != nullptr);
    CHECK(scriptDocument->GetElementById("mission-guide") != nullptr);
    CHECK(document->GetElementById("region-guide") != nullptr);
    CHECK(scriptDocument->GetElementById("region-guide") != nullptr);
    CHECK(document->GetElementById("strategic-guide") != nullptr);
    CHECK(scriptDocument->GetElementById("strategic-guide") != nullptr);
    CHECK(document->GetElementById("siege-status") != nullptr);
    CHECK(scriptDocument->GetElementById("siege-status") != nullptr);
    CHECK(document->GetElementById("base-status") != nullptr);
    CHECK(scriptDocument->GetElementById("base-status") != nullptr);
    for (Rml::ElementDocument* hud : {document, scriptDocument}) {
        REQUIRE(hud != nullptr);
        CHECK(hud->GetElementById("mod-fields") != nullptr);
        for (int i = 0; i < 8; ++i) {
            CHECK(hud->GetElementById("mod-field-" + std::to_string(i)) !=
                  nullptr);
            CHECK(hud->GetElementById("mod-field-label-" +
                                      std::to_string(i)) != nullptr);
            CHECK(hud->GetElementById("mod-field-value-" +
                                      std::to_string(i)) != nullptr);
        }
        CHECK(hud->GetElementById("siege-report") != nullptr);
        CHECK(hud->GetElementById("siege-report-title") != nullptr);
        CHECK(hud->GetElementById("siege-report-rating") != nullptr);
        CHECK(hud->GetElementById("siege-report-hint") != nullptr);
        for (int i = 0; i < 8; ++i) {
            CHECK(hud->GetElementById("siege-report-line-" +
                                      std::to_string(i)) != nullptr);
        }
        CHECK(hud->GetElementById("poi-stage-panel") != nullptr);
        CHECK(hud->GetElementById("poi-stage-title") != nullptr);
        CHECK(hud->GetElementById("poi-stage-objective") != nullptr);
        CHECK(hud->GetElementById("poi-stage-meta") != nullptr);
        CHECK(hud->GetElementById("poi-stage-narrative") != nullptr);
        CHECK(hud->GetElementById("poi-result-panel") != nullptr);
        CHECK(hud->GetElementById("poi-result-title") != nullptr);
        CHECK(hud->GetElementById("poi-result-body") != nullptr);
        CHECK(hud->GetElementById("world-event-panel") != nullptr);
        CHECK(hud->GetElementById("world-event-title") != nullptr);
        CHECK(hud->GetElementById("world-event-meta") != nullptr);
        CHECK(hud->GetElementById("world-event-stage") != nullptr);
        CHECK(hud->GetElementById("world-event-hint") != nullptr);
        CHECK(hud->GetElementById("world-event-result") != nullptr);
        for (int i = 0; i < 8; ++i) {
            CHECK(hud->GetElementById("world-event-option-" +
                                      std::to_string(i)) != nullptr);
        }
        for (int i = 0; i < 6; ++i) {
            CHECK(hud->GetElementById("world-event-log-" +
                                      std::to_string(i)) != nullptr);
        }
    }
    Rml::Element* worldEventPanel =
        document->GetElementById("world-event-panel");
    REQUIRE(worldEventPanel != nullptr);
    worldEventPanel->SetProperty("display", "block");
    context->Update();
    CHECK(worldEventPanel->GetClientWidth() >= 380.0f);
    CHECK(worldEventPanel->GetClientHeight() > 0.0f);
    worldEventPanel->SetProperty("display", "none");
    Rml::Element* stagePanel = document->GetElementById("poi-stage-panel");
    Rml::Element* stageTitle = document->GetElementById("poi-stage-title");
    Rml::Element* stageObjective =
        document->GetElementById("poi-stage-objective");
    Rml::Element* stageMeta = document->GetElementById("poi-stage-meta");
    Rml::Element* stageNarrative =
        document->GetElementById("poi-stage-narrative");
    Rml::Element* resultPanel = document->GetElementById("poi-result-panel");
    Rml::Element* resultTitle = document->GetElementById("poi-result-title");
    Rml::Element* resultBody = document->GetElementById("poi-result-body");
    REQUIRE(stagePanel != nullptr);
    REQUIRE(stageTitle != nullptr);
    REQUIRE(stageObjective != nullptr);
    REQUIRE(stageMeta != nullptr);
    REQUIRE(stageNarrative != nullptr);
    REQUIRE(resultPanel != nullptr);
    REQUIRE(resultTitle != nullptr);
    REQUIRE(resultBody != nullptr);
    stageTitle->SetInnerRML("阶段医院 · 阶段 2/3");
    stageObjective->SetInnerRML("在药房找到门禁卡并打开前门");
    stageMeta->SetInnerRML("钥匙：前门钥匙 · 供电：需要发电机燃料 · 路线：1 正门 / 2 后门");
    stageNarrative->SetInnerRML("医院封锁记录：不要相信广播里的声音");
    stageNarrative->SetProperty("display", "block");
    stagePanel->SetProperty("display", "block");
    resultTitle->SetInnerRML("阶段医院 · 阶段探索完成");
    resultBody->SetInnerRML("全部 3 阶段完成 · 奖励已结算 · 按 M 查看地图");
    resultPanel->SetProperty("display", "block");
    context->Update();
    const LayoutRect stageRect = BorderRect(stagePanel);
    const LayoutRect objectiveRect =
        BorderRect(document->GetElementById("hud-objective"));
    const LayoutRect resultRect = BorderRect(resultPanel);
    CHECK(stageRect.right <= 1280.0f - 15.0f);
    CHECK(stageRect.bottom <= 720.0f);
    CHECK(objectiveRect.right <= 1280.0f - 15.0f);
    CHECK(objectiveRect.bottom <= 720.0f);
    CHECK(resultRect.right <= 1280.0f - 15.0f);
    CHECK(resultRect.bottom <= 720.0f);
    CHECK(!Overlaps(objectiveRect, resultRect));
    CHECK(stagePanel->GetClientHeight() > 0.0f);
    CHECK(resultPanel->GetClientHeight() > 0.0f);
    stagePanel->SetProperty("display", "none");
    resultPanel->SetProperty("display", "none");
    Rml::Element* reportPanel = document->GetElementById("siege-report");
    REQUIRE(reportPanel != nullptr);
    reportPanel->SetProperty("display", "block");
    context->Update();
    CHECK(reportPanel->GetClientWidth() >= 1280.0f);
    CHECK(reportPanel->GetClientHeight() >= 720.0f);
    reportPanel->SetProperty("display", "none");
    Rml::Element* baseStatus = document->GetElementById("base-status");
    REQUIRE(baseStatus != nullptr);
    baseStatus->SetInnerRML("base");
    baseStatus->SetProperty("display", "block");
    context->Update();
    CHECK(baseStatus->GetClientHeight() > 0.0f);
    Rml::Element* scriptBaseStatus =
        scriptDocument->GetElementById("base-status");
    REQUIRE(scriptBaseStatus != nullptr);
    scriptBaseStatus->SetInnerRML("base");
    scriptBaseStatus->SetProperty("display", "block");
    context->Update();
    CHECK(scriptBaseStatus->GetClientHeight() > 0.0f);
    for (Rml::ElementDocument* hud : {document, scriptDocument}) {
        REQUIRE(hud != nullptr);
        CHECK(hud->GetElementById("hud-level") != nullptr);
        CHECK(hud->GetElementById("level-text") != nullptr);
        CHECK(hud->GetElementById("level-perk") != nullptr);
        CHECK(hud->GetElementById("level-fill") != nullptr);
        CHECK(hud->GetElementById("level-xp") != nullptr);
        CHECK(hud->GetElementById("perk-prompt") != nullptr);
        CHECK(hud->GetElementById("character-panel") != nullptr);
        CHECK(hud->GetElementById("character-level") != nullptr);
        CHECK(hud->GetElementById("character-xp") != nullptr);
        CHECK(hud->GetElementById("character-xp-fill") != nullptr);
        CHECK(hud->GetElementById("character-perks") != nullptr);
        for (int i = 0; i < 4; ++i) {
            CHECK(hud->GetElementById("character-skill-" + std::to_string(i)) !=
                  nullptr);
        }
        for (int i = 0; i < 3; ++i) {
            CHECK(hud->GetElementById("perk-choice-" + std::to_string(i)) !=
                  nullptr);
        }
    }
    Rml::Element* characterPanel =
        document->GetElementById("character-panel");
    REQUIRE(characterPanel != nullptr);
    characterPanel->SetProperty("display", "block");
    context->Update();
    CHECK(characterPanel->GetClientWidth() >= 1280.0f);
    CHECK(characterPanel->GetClientHeight() >= 720.0f);
    CHECK(document->GetElementById("action-hint") != nullptr);
    CHECK(scriptDocument->GetElementById("action-hint") != nullptr);
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

TEST_CASE("P1 HUD 1280x720 与 1920x1080 布局") {
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

    const Rml::Vector2i sizes[] = {Rml::Vector2i(1280, 720),
                                    Rml::Vector2i(1920, 1080)};
    const std::string longObjective =
        "清剿废弃加油站守军并搜索储物柜，取回维修工具、罐装食物、无线电零件和医疗包，确认安全后返回基地";
    const std::string longPoi =
        "POI · 废弃加油站（威胁高）西南 38 格 · 清剿守军并搜索储物柜（工具/零食）";
    const std::string longMission =
        "任务：无线电求救 | 搜索房屋工具柜并找到无线电零件 0/1 | 奖励 40 生存经验 | L切换/K放弃";
    const std::string longEvent =
        "事件 · 被感染房屋 · 清理屋内感染者 1/3；搜出屋内的感染者藏匿箱 0/1 · 目标东南 38格 · 奖励 60 生存经验 + 急救包×1";
    const std::string reportLine =
        "波次 3/3 | 结构损失 2/9 | 存活比例 78% | 破防方向 西北 | 奖励 燃料罐×1";
    const char* hudPaths[] = {"assets/ui/hud.rml",
                              "mods/core_ui/ui/hud.rml"};

    for (int sizeIndex = 0; sizeIndex < 2; ++sizeIndex) {
        const std::string contextName =
            "p1-hud-" + std::to_string(sizeIndex);
        Rml::Context* context = Rml::CreateContext(contextName, sizes[sizeIndex]);
        REQUIRE(context != nullptr);

        for (const char* hudPath : hudPaths) {
            Rml::ElementDocument* document = context->LoadDocument(
                std::string(ZL_SOURCE_DIR) + "/" + hudPath);
            REQUIRE(document != nullptr);
            document->Show();
            context->Update();

            Rml::Element* objective = document->GetElementById("hud-objective");
            Rml::Element* objectiveText =
                document->GetElementById("objective-text");
            Rml::Element* objectiveHint =
                document->GetElementById("objective-hint");
            Rml::Element* poiGuide = document->GetElementById("poi-guide");
            Rml::Element* missionGuide =
                document->GetElementById("mission-guide");
            Rml::Element* eventGuide =
                document->GetElementById("event-guide");
            Rml::Element* modFields = document->GetElementById("mod-fields");
            Rml::Element* baseStatus =
                document->GetElementById("base-status");
            Rml::Element* siegeStatus =
                document->GetElementById("siege-status");
            Rml::Element* threatStatus =
                document->GetElementById("threat-status");
            Rml::Element* perkPrompt =
                document->GetElementById("perk-prompt");
            REQUIRE(objective != nullptr);
            REQUIRE(objectiveText != nullptr);
            REQUIRE(objectiveHint != nullptr);
            REQUIRE(poiGuide != nullptr);
            REQUIRE(missionGuide != nullptr);
            REQUIRE(eventGuide != nullptr);
            REQUIRE(modFields != nullptr);
            REQUIRE(baseStatus != nullptr);
            REQUIRE(siegeStatus != nullptr);
            REQUIRE(threatStatus != nullptr);
            REQUIRE(perkPrompt != nullptr);

            objectiveText->SetInnerRML(longObjective);
            objectiveHint->SetInnerRML("目标提示：按 E 交互，按 I 搜刮");
            poiGuide->SetInnerRML(longPoi);
            missionGuide->SetInnerRML(longMission);
            eventGuide->SetInnerRML(longEvent);
            modFields->SetProperty("display", "block");
            for (int i = 0; i < 8; ++i) {
                Rml::Element* row =
                    document->GetElementById("mod-field-" + std::to_string(i));
                Rml::Element* label = document->GetElementById(
                    "mod-field-label-" + std::to_string(i));
                Rml::Element* value = document->GetElementById(
                    "mod-field-value-" + std::to_string(i));
                REQUIRE(row != nullptr);
                REQUIRE(label != nullptr);
                REQUIRE(value != nullptr);
                row->SetProperty("display", "block");
                label->SetInnerRML("补给站状态标签 " + std::to_string(i + 1));
                value->SetInnerRML("库存 12/24 · 发电机供电正常 · 低燃料警告未触发");
            }
            baseStatus->SetInnerRML(
                "基地 · 仓库 2（12.5/100.0kg） · 床位 2 · 篝火 1 · 工作台 1 · "
                "防御 8 完好 / 2 损坏 · 电力：燃料 320 分 · 负载 2.5 · 供电中");
            siegeStatus->SetInnerRML(
                "守夜 第 2/3 波 | 来袭 西北 | 预计破口 木墙 120/120");
            threatStatus->SetInnerRML("受击：撕咬（流血）");
            perkPrompt->SetInnerRML("升级！按 1/2/3 选择 Perk");
            baseStatus->SetProperty("display", "block");
            siegeStatus->SetProperty("display", "block");
            threatStatus->SetProperty("display", "block");
            perkPrompt->SetProperty("display", "block");
            context->Update();

            const LayoutRect objectiveRect = BorderRect(objective);
            const LayoutRect modFieldsRect = BorderRect(modFields);
            const LayoutRect baseStatusRect = BorderRect(baseStatus);
            const LayoutRect siegeStatusRect = BorderRect(siegeStatus);
            const LayoutRect threatStatusRect = BorderRect(threatStatus);
            const LayoutRect perkPromptRect = BorderRect(perkPrompt);
            CHECK(objectiveRect.left >= 0.0f);
            CHECK(objectiveRect.top >= 0.0f);
            CHECK(objectiveRect.right <=
                  static_cast<float>(sizes[sizeIndex].x) - 15.0f);
            CHECK(objectiveRect.bottom <= static_cast<float>(sizes[sizeIndex].y));
            CHECK(modFieldsRect.left >= 0.0f);
            CHECK(modFieldsRect.top >= 0.0f);
            CHECK(modFieldsRect.right <=
                  static_cast<float>(sizes[sizeIndex].x) - 15.0f);
            CHECK(modFieldsRect.bottom <= static_cast<float>(sizes[sizeIndex].y));
            CHECK(!Overlaps(objectiveRect, modFieldsRect));
            CHECK(modFieldsRect.top >= objectiveRect.bottom + 8.0f);
            CHECK(objective->GetScrollHeight() <=
                  objective->GetClientHeight() + 1.0f);
            CHECK(modFields->GetScrollHeight() <=
                  modFields->GetClientHeight() + 1.0f);
            CHECK(objective->GetClientHeight() > 0.0f);
            CHECK(poiGuide->GetClientHeight() >= 32.0f);
            CHECK(missionGuide->GetClientHeight() >= 32.0f);
            CHECK(eventGuide->GetClientHeight() >= 32.0f);
            CHECK((zl::ui::HudNoticeSuppressed(true, false, false)));
            CHECK((zl::ui::HudNoticeSuppressed(false, true, false)));
            CHECK((zl::ui::HudNoticeSuppressed(false, false, true)));
            CHECK_FALSE((zl::ui::HudNoticeSuppressed(false, false, false)));
            CHECK(baseStatusRect.right <= static_cast<float>(sizes[sizeIndex].x));
            CHECK(baseStatusRect.bottom <= static_cast<float>(sizes[sizeIndex].y));
            CHECK(siegeStatusRect.right <= static_cast<float>(sizes[sizeIndex].x));
            CHECK(siegeStatusRect.bottom <= static_cast<float>(sizes[sizeIndex].y));
            CHECK(threatStatusRect.right <= static_cast<float>(sizes[sizeIndex].x));
            CHECK(threatStatusRect.bottom <= static_cast<float>(sizes[sizeIndex].y));
            CHECK(!Overlaps(baseStatusRect, objectiveRect));
            CHECK(!Overlaps(baseStatusRect, modFieldsRect));
            CHECK(!Overlaps(siegeStatusRect, objectiveRect));
            CHECK(!Overlaps(siegeStatusRect, modFieldsRect));
            CHECK(!Overlaps(threatStatusRect, objectiveRect));
            CHECK(!Overlaps(threatStatusRect, modFieldsRect));
            CHECK(!Overlaps(baseStatusRect, siegeStatusRect));
            CHECK(!Overlaps(siegeStatusRect, threatStatusRect));
            CHECK(baseStatus->GetScrollHeight() <=
                  baseStatus->GetClientHeight() + 1.0f);
            CHECK(siegeStatus->GetScrollHeight() <=
                  siegeStatus->GetClientHeight() + 1.0f);
            CHECK(threatStatus->GetScrollHeight() <=
                  threatStatus->GetClientHeight() + 1.0f);
            CHECK(perkPromptRect.top >= threatStatusRect.bottom + 8.0f);
            CHECK(perkPromptRect.bottom <= static_cast<float>(sizes[sizeIndex].y));

            Rml::Element* reportPanel = document->GetElementById("siege-report");
            Rml::Element* reportCard =
                document->GetElementById("siege-report-card");
            Rml::Element* reportTitle =
                document->GetElementById("siege-report-title");
            Rml::Element* reportRating =
                document->GetElementById("siege-report-rating");
            Rml::Element* reportHint =
                document->GetElementById("siege-report-hint");
            REQUIRE(reportPanel != nullptr);
            REQUIRE(reportCard != nullptr);
            REQUIRE(reportTitle != nullptr);
            REQUIRE(reportRating != nullptr);
            REQUIRE(reportHint != nullptr);
            reportPanel->SetProperty("display", "block");
            reportTitle->SetInnerRML("守夜战报");
            reportRating->SetInnerRML("S");
            reportHint->SetInnerRML("ESC 关闭");
            for (int i = 0; i < 8; ++i) {
                Rml::Element* line = document->GetElementById(
                    "siege-report-line-" + std::to_string(i));
                REQUIRE(line != nullptr);
                line->SetInnerRML(reportLine);
            }
            context->Update();

            const LayoutRect reportRect = BorderRect(reportPanel);
            const LayoutRect cardRect = BorderRect(reportCard);
            CHECK(reportRect.left >= 0.0f);
            CHECK(reportRect.top >= 0.0f);
            CHECK(reportRect.right <= static_cast<float>(sizes[sizeIndex].x));
            CHECK(reportRect.bottom <= static_cast<float>(sizes[sizeIndex].y));
            CHECK(cardRect.left >= 0.0f);
            CHECK(cardRect.top >= 0.0f);
            CHECK(cardRect.right <= static_cast<float>(sizes[sizeIndex].x));
            CHECK(cardRect.bottom <= static_cast<float>(sizes[sizeIndex].y));
            CHECK(reportCard->GetScrollHeight() <=
                  reportCard->GetClientHeight() + 1.0f);
            for (int i = 0; i < 8; ++i) {
                Rml::Element* line = document->GetElementById(
                    "siege-report-line-" + std::to_string(i));
                REQUIRE(line != nullptr);
                CHECK(line->GetClientHeight() > 0.0f);
                CHECK(BorderRect(line).bottom <= cardRect.bottom + 1.0f);
            }

            reportPanel->SetProperty("display", "none");
            context->Update();
            document->Hide();
        }

        Rml::RemoveContext(contextName);
    }

    Rml::Shutdown();
}

TEST_CASE("P1 HUD 左侧状态堆叠与底部提示布局") {
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

    const Rml::Vector2i sizes[] = {Rml::Vector2i(1280, 720),
                                    Rml::Vector2i(1920, 1080)};
    const char* hudPaths[] = {"assets/ui/hud.rml",
                              "mods/core_ui/ui/hud.rml"};
    const char* statusIds[] = {"ranged-status", "forage-status", "fishing-status",
                               "needs-status", "wound-status", "pain-status",
                               "armor-status", "infection-status",
                               "weather-status"};
    const char* statusTexts[] = {
        "手枪 换弹中 42% · 3/8 · 备弹 9",
        "可觅食 · 森林（2.6 秒）",
        "可钓鱼 · 静水（需要 鱼饵）",
        "饥饿：严重（移速 -20%） · 口渴：中度（体力恢复 -10%）",
        "流血：2 处伤口（1.2 HP/s，未包扎）",
        "疼痛：严重 75%（近战命中 -20%）",
        "护甲：减伤 22% · 移速 -6%",
        "感染：发热期 · 剩余 12:30 · 抑制 03:00",
        "天气：寒潮 · 体温 34.8°C · 潮湿 68% · 室内"};

    for (int sizeIndex = 0; sizeIndex < 2; ++sizeIndex) {
        const std::string contextName =
            "p1-hud-bottom-" + std::to_string(sizeIndex);
        Rml::Context* context = Rml::CreateContext(contextName, sizes[sizeIndex]);
        REQUIRE(context != nullptr);

        for (const char* hudPath : hudPaths) {
            Rml::ElementDocument* document = context->LoadDocument(
                std::string(ZL_SOURCE_DIR) + "/" + hudPath);
            REQUIRE(document != nullptr);
            document->Show();
            context->Update();

            Rml::Element* level = document->GetElementById("hud-level");
            Rml::Element* vitals = document->GetElementById("hud-vitals");
            Rml::Element* hotbar = document->GetElementById("hotbar");
            Rml::Element* actionHint =
                document->GetElementById("action-hint");
            Rml::Element* stealthPrompt =
                document->GetElementById("stealth-prompt");
            Rml::Element* notice = document->GetElementById("notice");
            REQUIRE(level != nullptr);
            REQUIRE(vitals != nullptr);
            REQUIRE(hotbar != nullptr);
            REQUIRE(actionHint != nullptr);
            REQUIRE(stealthPrompt != nullptr);
            REQUIRE(notice != nullptr);

            for (std::size_t i = 0; i < 9; ++i) {
                Rml::Element* status = document->GetElementById(statusIds[i]);
                REQUIRE(status != nullptr);
                status->SetInnerRML(statusTexts[i]);
                status->SetProperty("display", "block");
            }
            actionHint->SetInnerRML(
                "流血中：按 I 打开背包使用绷带；附近木箱按 I 搜刮；受损木墙按 H "
                "维修；靠近泛光灯按 E 开关；背包已满时奖励掉在脚下");
            actionHint->SetProperty("display", "block");
            stealthPrompt->SetInnerRML("背刺可用 · SPACE");
            stealthPrompt->SetProperty("display", "block");
            notice->SetInnerRML(
                "背包已满，奖励掉落在脚下：燃料罐×1、维修包×1、木材×4、罐装食物×2");
            notice->SetProperty("display", "block");
            context->Update();

            const LayoutRect levelRect = BorderRect(level);
            const LayoutRect vitalsRect = BorderRect(vitals);
            const LayoutRect hotbarRect = BorderRect(hotbar);
            const LayoutRect actionRect = BorderRect(actionHint);
            const LayoutRect promptRect = BorderRect(stealthPrompt);
            const LayoutRect noticeRect = BorderRect(notice);
            CHECK(vitalsRect.left >= 15.0f);
            CHECK(vitalsRect.right <= static_cast<float>(sizes[sizeIndex].x));
            CHECK(vitalsRect.top >= 0.0f);
            CHECK(vitalsRect.bottom <= static_cast<float>(sizes[sizeIndex].y));
            CHECK(!Overlaps(levelRect, vitalsRect));
            CHECK(levelRect.bottom <= vitalsRect.top - 8.0f);
            CHECK(actionRect.bottom <= hotbarRect.top - 4.0f);
            CHECK(!Overlaps(actionRect, hotbarRect));
            CHECK(!Overlaps(noticeRect, actionRect));
            CHECK(!Overlaps(promptRect, actionRect));
            CHECK(!Overlaps(promptRect, noticeRect));
            CHECK(!Overlaps(promptRect, hotbarRect));
            CHECK(promptRect.top >= 0.0f);
            CHECK(promptRect.bottom <= static_cast<float>(sizes[sizeIndex].y));
            CHECK(noticeRect.top >= 0.0f);
            CHECK(noticeRect.bottom <= static_cast<float>(sizes[sizeIndex].y));

            Rml::Element* dialoguePanel =
                document->GetElementById("dialogue-panel");
            Rml::Element* dialogueSpeaker =
                document->GetElementById("dialogue-speaker");
            Rml::Element* dialogueText =
                document->GetElementById("dialogue-text");
            Rml::Element* dialogueHint =
                document->GetElementById("dialogue-hint");
            REQUIRE(dialoguePanel != nullptr);
            REQUIRE(dialogueSpeaker != nullptr);
            REQUIRE(dialogueText != nullptr);
            REQUIRE(dialogueHint != nullptr);
            actionHint->SetProperty("display", "none");
            stealthPrompt->SetProperty("display", "none");
            notice->SetProperty("display", "none");
            dialoguePanel->SetProperty("display", "block");
            dialogueSpeaker->SetInnerRML("求助者 · 村庄营地");
            dialogueText->SetInnerRML("你看起来能帮上忙。营地现在缺人手。");
            dialogueHint->SetInnerRML(
                "1-5 选择 · 滚轮定位 · E 确认 · ESC 关闭");
            for (int i = 0; i < 5; ++i) {
                Rml::Element* option = document->GetElementById(
                    "dialogue-option-" + std::to_string(i));
                REQUIRE(option != nullptr);
                option->SetInnerRML("[" + std::to_string(i + 1) +
                                    "] 查看委托与营地状态");
                option->SetProperty("display", "block");
            }
            context->Update();

            const LayoutRect dialogueRect = BorderRect(dialoguePanel);
            CHECK(dialogueRect.right - dialogueRect.left >= 640.0f);
            CHECK(dialogueRect.left >= 0.0f);
            CHECK(dialogueRect.right <= static_cast<float>(sizes[sizeIndex].x));
            CHECK(dialogueRect.bottom <= hotbarRect.top - 4.0f);
            CHECK(!Overlaps(dialogueRect, hotbarRect));
            CHECK(dialogueSpeaker->GetClientHeight() > 0.0f);
            CHECK(dialogueText->GetClientHeight() > 0.0f);
            for (int i = 0; i < 5; ++i) {
                Rml::Element* option = document->GetElementById(
                    "dialogue-option-" + std::to_string(i));
                REQUIRE(option != nullptr);
                CHECK(option->GetClientHeight() > 0.0f);
            }
            dialoguePanel->SetProperty("display", "none");

            document->Hide();
        }

        Rml::RemoveContext(contextName);
    }

    Rml::Shutdown();
}

TEST_CASE("M16-T1 mouse press latch survives same-poll release") {
    zl::core::Input input;
    input.NewFrame();
    input.LatchMouseButton(0, true);
    input.LatchMouseButton(0, false);
    CHECK(!input.IsMouseButtonDown(0));
    CHECK(input.IsMouseButtonPressed(0));
    input.NewFrame();
    CHECK(!input.IsMouseButtonPressed(0));

    input.NewFrame();
    input.SetMouseButton(0, true);
    CHECK(input.IsMouseButtonPressed(0));
    input.NewFrame();
    CHECK(input.IsMouseButtonDown(0));
    CHECK(!input.IsMouseButtonPressed(0));
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
    state.bagStacks = 4;
    state.bag[0] = zl::ui::InventorySlot{true, "绷带", 3, true, false, 0};
    state.bag[1] = zl::ui::InventorySlot{true, "消防斧", 1, false, true, 0};
    state.bag[2] = zl::ui::InventorySlot{true, "锤子", 1, false, true, 0,
                                         0.6f, true, true};
    state.bag[3] = zl::ui::InventorySlot{true, "烤豆子", 1, true, false, 0,
                                         0.5f, false, false, true};
    state.equipment[0] = zl::ui::InventorySlot{true, "消防斧", 1, false, false, 0};
    state.loot[0] = zl::ui::InventorySlot{true, "破布", 2, false, false, 9001};
    state.loot[7] = zl::ui::InventorySlot{true, "木材", 4, false, false, 9002};
    state.lootStacks = 2;
    state.lootTitle = "容器：木箱";
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
    CHECK(!document->GetElementById("bag-slot-4")->IsClassSet("filled"));
    CHECK(document->GetElementById("bag-name-0")->GetInnerRML() == "绷带");
    CHECK(document->GetElementById("bag-count-0")->GetInnerRML() == "3");
    CHECK(document->GetElementById("bag-name-2")->GetInnerRML() == "锤子 60%");
    CHECK(document->GetElementById("bag-name-3")->GetInnerRML() ==
          "烤豆子 新鲜 50%");
    CHECK(document->GetElementById("equip-slot-0")->IsClassSet("filled"));
    CHECK(document->GetElementById("equip-name-0")->GetInnerRML() == "消防斧");
    CHECK(document->GetElementById("loot-slot-0")->IsClassSet("filled"));
    CHECK(document->GetElementById("loot-slot-0")->GetAttribute<Rml::String>("data-entity", "") ==
          "9001");
    CHECK(document->GetElementById("loot-slot-7")->IsClassSet("filled"));
    CHECK(document->GetElementById("loot-slot-7")->GetAttribute<Rml::String>("data-entity", "") ==
          "9002");
    CHECK(document->GetElementById("loot-title")->GetInnerRML() == "容器：木箱");
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

    rightClick(document->GetElementById("bag-slot-2"));
    REQUIRE(menu->IsVisible());
    CHECK(document->GetElementById("ctx-repair")->IsVisible());
    CHECK(document->GetElementById("ctx-repair")->GetInnerRML() == "维修");
    clickElement(document->GetElementById("ctx-repair"), 0);
    REQUIRE(actions.size() == 4);
    CHECK(actions[3].type == zl::ui::InventoryActionType::Repair);
    CHECK(actions[3].from == zl::ui::InventoryArea::Backpack);
    CHECK(actions[3].fromIndex == 2);

    CHECK_FALSE(document->GetElementById("storage-actions")->IsVisible());
    state.lootIsStorage = true;
    state.lootEntityId = 9001;
    panel.SetState(state);
    context->Update();
    CHECK(document->GetElementById("storage-actions")->IsVisible());
    clickElement(document->GetElementById("storage-store-all"), 0);
    REQUIRE(actions.size() == 5);
    CHECK(actions[4].type == zl::ui::InventoryActionType::StoreAll);
    CHECK(actions[4].from == zl::ui::InventoryArea::Loot);
    CHECK(actions[4].fromEntityId == 9001);

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
        state.loot[7] = zl::ui::InventorySlot{true, "木材", 4, false, false, 9002};
        state.lootStacks = 1;
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
        Rml::Element* loot7 = panel.Document()->GetElementById("loot-slot-7");
        REQUIRE(loot7 != nullptr);
        const Rml::Vector2f lootOffset =
            loot7->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f lootSize =
            loot7->GetBox().GetSize(Rml::BoxArea::Border);
        CHECK(lootSize.x > 0.0f);
        CHECK(lootSize.y > 0.0f);
        CHECK(lootOffset.x >= offset.x);
        CHECK(lootOffset.y >= offset.y);
        CHECK(lootOffset.x + lootSize.x <= offset.x + box.x);
        CHECK(lootOffset.y + lootSize.y <= offset.y + box.y);

        panel.Shutdown();
        Rml::RemoveContext(name);
    }
    Rml::Shutdown();
}
TEST_CASE("M9-T5 core_ui 背包 8 槽布局") {
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

    const Rml::Vector2i sizes[] = {Rml::Vector2i(1280, 720),
                                   Rml::Vector2i(1920, 1080)};
    const std::string path =
        std::string(ZL_SOURCE_DIR) + "/mods/core_ui/ui/inventory.rml";
    for (int i = 0; i < 2; ++i) {
        const std::string name = "core-inventory-layout-" + std::to_string(i);
        Rml::Context* context = Rml::CreateContext(name, sizes[i]);
        REQUIRE(context != nullptr);
        Rml::ElementDocument* document = context->LoadDocument(path);
        REQUIRE(document != nullptr);
        document->Show();
        context->Update();

        Rml::Element* panel = document->GetElementById("inventory-panel");
        Rml::Element* loot7 = document->GetElementById("loot-slot-7");
        REQUIRE(panel != nullptr);
        REQUIRE(loot7 != nullptr);
        const Rml::Vector2f panelOffset =
            panel->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f panelSize =
            panel->GetBox().GetSize(Rml::BoxArea::Border);
        const Rml::Vector2f lootOffset =
            loot7->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f lootSize = loot7->GetBox().GetSize(Rml::BoxArea::Border);
        CHECK(panelOffset.x >= 0.0f);
        CHECK(panelOffset.y >= 0.0f);
        CHECK(panelOffset.x + panelSize.x <= static_cast<float>(sizes[i].x));
        CHECK(panelOffset.y + panelSize.y <= static_cast<float>(sizes[i].y));
        CHECK(lootSize.x > 0.0f);
        CHECK(lootSize.y > 0.0f);
        CHECK(lootOffset.x >= panelOffset.x);
        CHECK(lootOffset.y >= panelOffset.y);
        CHECK(lootOffset.x + lootSize.x <= panelOffset.x + panelSize.x);
        CHECK(lootOffset.y + lootSize.y <= panelOffset.y + panelSize.y);
        CHECK(document->GetElementById("bag-slot-24") != nullptr);

        context->UnloadDocument(document);
        Rml::RemoveContext(name);
    }
    Rml::Shutdown();
}

TEST_CASE("M9-T6/M14-T8 主菜单、暂停菜单、设置页与结算页") {
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
        {"MoveRight", "D"},   {"Attack", "SPACE"}, {"HeavyAttack", "LSHIFT"},
        {"Block", "LCTRL"},   {"Dodge", "Z"},    {"Pickup", "F"},
        {"Use", "E"},         {"Equip", "R"},    {"Drop", "G"},
        {"Pause", "F1"},      {"Stealth", "N"},
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
    REQUIRE(applied.bindings.size() == 14);
    CHECK(applied.bindings[0].key == "Q");
    CHECK(applied.bindings[6].action == "Block");
    CHECK(applied.bindings[6].key == "LCTRL");
    CHECK(applied.bindings[7].action == "Dodge");
    CHECK(applied.bindings[7].key == "Z");
    CHECK(applied.bindings[13].action == "Stealth");
    CHECK(applied.bindings[13].key == "N");

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

    clickElement("menu-multiplayer");
    REQUIRE(actions.size() == 5);
    CHECK(actions[4] == zl::ui::MenuActionType::Multiplayer);

    panel.ShowEndScreen(false, "你死了", "你倒在了尸潮中。",
                        "死因：撕咬<br/>存活：12 分 34 秒（第 2 天 03:15）<br/>"
                        "击杀：7 只僵尸<br/>受伤：4 次（累计 96 点）");
    context->Update();
    REQUIRE(panel.IsOpen());
    CHECK(document->GetElementById("end-menu")->IsVisible());
    CHECK(document->GetElementById("end-menu")->IsClassSet("death"));
    CHECK(document->GetElementById("end-title")->GetInnerRML() == "你死了");
    CHECK(document->GetElementById("end-stats")->GetInnerRML().find("死因：撕咬") !=
          std::string::npos);
    CHECK(document->GetElementById("end-stats")->GetInnerRML().find("击杀：7") !=
          std::string::npos);
    const std::size_t beforeEndEscape = actions.size();
    CHECK(panel.OnEscape());  // 结算页 ESC 不绕过
    CHECK(actions.size() == beforeEndEscape);
    clickElement("end-restart");
    REQUIRE(actions.size() == 6);
    CHECK(actions[5] == zl::ui::MenuActionType::NewGame);

    panel.ShowEndScreen(true, "成功撤离", "无线电已修复。", "第 1 天 08:00");
    context->Update();
    CHECK(document->GetElementById("end-menu")->IsClassSet("victory"));
    CHECK(document->GetElementById("end-title")->GetInnerRML() == "成功撤离");
    clickElement("end-main-menu");
    REQUIRE(actions.size() == 7);
    CHECK(actions[6] == zl::ui::MenuActionType::ReturnToMainMenu);

    panel.ShowEndScreen(true, "成功撤离（联机）",
                        "你已到达撤离点，其他玩家仍在废土中。",
                        "第 1 天 08:00", false);
    context->Update();
    CHECK(!document->GetElementById("end-restart")->IsVisible());
    CHECK(document->GetElementById("end-main-menu")->IsVisible());
    CHECK(document->GetElementById("end-hint")->GetInnerRML().find("重新联机") !=
          std::string::npos);

    panel.Hide();
    CHECK(!panel.IsOpen());
    panel.Shutdown();
    Rml::RemoveContext("menu");
    Rml::Shutdown();
}
TEST_CASE("M13-T9 UI 联机入口菜单：地址/端口/服务器列表/主机动线") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));

    Rml::Context* context = Rml::CreateContext("multiplayer", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    zl::ui::MultiplayerPanel panel;
    REQUIRE(panel.Init(*context,
                       std::string(ZL_SOURCE_DIR) + "/assets/ui/multiplayer.rml"));

    std::vector<zl::ui::MultiplayerActionType> actions;
    zl::ui::MultiplayerMenuState received;
    panel.SetActionHandler([&](zl::ui::MultiplayerActionType action,
                               const zl::ui::MultiplayerMenuState& state) {
        actions.push_back(action);
        received = state;
    });
    zl::ui::MultiplayerMenuState state;
    state.playerName = "Alice";
    state.joinHost = "192.168.1.7";
    state.joinPort = 7788;
    state.hostPort = 7777;
    state.servers = {
        {"本地测试", "127.0.0.1", 7777},
        {"朋友", "10.0.0.8", 9000},
    };
    state.status = "选择服务器";
    state.statusTone = "info";
    panel.Show(state);
    context->Update();
    REQUIRE(panel.IsOpen());
    Rml::ElementDocument* document = panel.Document();
    REQUIRE(document != nullptr);
    CHECK(document->GetElementById("mp-overlay")->IsVisible());
    CHECK(document->GetElementById("mp-server-0")->IsVisible());
    CHECK(document->GetElementById("mp-server-1")->IsVisible());
    CHECK(!document->GetElementById("mp-server-2")->IsVisible());
    CHECK(document->GetElementById("mp-server-name-0")->GetInnerRML() == "本地测试");
    CHECK(document->GetElementById("mp-status")->GetInnerRML() == "选择服务器");

    auto input = [&](const char* id) {
        return static_cast<Rml::ElementFormControlInput*>(document->GetElementById(id));
    };
    CHECK(input("mp-name")->GetValue() == "Alice");
    CHECK(input("mp-host")->GetValue() == "192.168.1.7");
    CHECK(input("mp-port")->GetValue() == "7788");
    CHECK(input("mp-host-port")->GetValue() == "7777");

    auto click = [&](const char* id) {
        Rml::Element* element = document->GetElementById(id);
        REQUIRE(element != nullptr);
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        context->ProcessMouseMove(static_cast<int>(offset.x) + 8,
                                  static_cast<int>(offset.y) + 8, 0);
        context->ProcessMouseButtonDown(0, 0);
        context->ProcessMouseButtonUp(0, 0);
        context->Update();
    };

    click("mp-server-0");
    CHECK(input("mp-host")->GetValue() == "127.0.0.1");
    CHECK(input("mp-port")->GetValue() == "7777");

    input("mp-host")->SetValue("10.0.0.8");
    input("mp-port")->SetValue("9000");
    input("mp-name")->SetValue("Bob");
    click("mp-join");
    REQUIRE(actions.size() == 1);
    CHECK(actions[0] == zl::ui::MultiplayerActionType::Join);
    CHECK(received.joinHost == "10.0.0.8");
    CHECK(received.joinPort == 9000);
    CHECK(received.playerName == "Bob");

    input("mp-host-port")->SetValue("0");
    click("mp-host-start");
    REQUIRE(actions.size() == 2);
    CHECK(actions[1] == zl::ui::MultiplayerActionType::Host);
    CHECK(received.hostPort == 0);

    click("mp-add-server");
    REQUIRE(actions.size() == 3);
    CHECK(actions[2] == zl::ui::MultiplayerActionType::AddServer);

    click("mp-refresh");
    REQUIRE(actions.size() == 4);
    CHECK(actions[3] == zl::ui::MultiplayerActionType::RefreshServers);

    click("mp-back");
    REQUIRE(actions.size() == 5);
    CHECK(actions[4] == zl::ui::MultiplayerActionType::Back);

    panel.Hide();
    panel.Shutdown();
    Rml::RemoveContext("multiplayer");
    Rml::Shutdown();
}

TEST_CASE("M13-T9 Social 聊天/玩家列表：显示、输入与抑制") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));

    Rml::Context* context = Rml::CreateContext("social", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    zl::ui::SocialPanel panel;
    REQUIRE(panel.Init(*context, std::string(ZL_SOURCE_DIR) + "/assets/ui/social.rml"));

    zl::ui::SocialUiState state;
    state.visible = true;
    state.players = {{1, "Alice", true}, {2, "Bob", false}};
    state.chat = {{"Bob", "集合点在北边"}};
    panel.SetState(state);
    context->Update();
    CHECK(panel.IsVisible());
    CHECK(panel.Document()->GetElementById("social-player-0")->IsVisible());
    CHECK(panel.Document()->GetElementById("social-player-1")->IsVisible());
    CHECK(!panel.Document()->GetElementById("social-player-2")->IsVisible());
    CHECK(panel.Document()->GetElementById("social-chat-0")
              ->GetInnerRML()
              .find("集合点在北边") != std::string::npos);
    CHECK(panel.Document()->GetElementById("social-player-count")->GetInnerRML() == "2");

    std::string sent;
    panel.SetSendHandler([&](const std::string& text, std::string& error) {
        sent = text;
        error.clear();
        return true;
    });
    panel.OpenChat();
    context->Update();
    CHECK(panel.IsChatOpen());
    CHECK(panel.Document()->GetElementById("social-chat-input-row")->IsVisible());
    CHECK(zl::ui::IsInputFocusBlocking(context));
    zl::core::Input gameplayInput;
    gameplayInput.Bind("MoveUp", GLFW_KEY_W);
    gameplayInput.NewFrame();
    gameplayInput.SetKeyState(GLFW_KEY_W, true);
    CHECK(gameplayInput.IsActionDown("MoveUp"));
    gameplayInput.SetGameplayBlocked(zl::ui::IsInputFocusBlocking(context));
    CHECK(!gameplayInput.IsActionDown("MoveUp"));
    CHECK(!gameplayInput.IsKeyDown(GLFW_KEY_W));
    auto* input = static_cast<Rml::ElementFormControlInput*>(
        panel.Document()->GetElementById("social-chat-input"));
    input->SetValue("你好");
    CHECK(panel.SubmitChat());
    CHECK(sent == "你好");
    CHECK(!panel.IsChatOpen());
    CHECK(!zl::ui::IsInputFocusBlocking(context));

    panel.SetSuppressed(true);
    context->Update();
    CHECK(!panel.Document()->GetElementById("social-players")->IsVisible());
    panel.SetSuppressed(false);
    context->Update();
    CHECK(panel.Document()->GetElementById("social-players")->IsVisible());

    panel.SetState({});
    context->Update();
    CHECK(!panel.Document()->GetElementById("social-players")->IsVisible());
    panel.Shutdown();
    Rml::RemoveContext("social");
    Rml::Shutdown();
}

TEST_CASE("M15-T5 结算统计与 HUD 操作提示文案") {
    zl::game::RunSummary summary;
    summary.deathCause = zl::ecs::DamageCause::Bite;
    summary.day = 2;
    summary.hour = 3;
    summary.minute = 7;
    summary.survivalSeconds = 65.0;
    summary.kills = 7;
    summary.injuries = 4;
    summary.damageTaken = 96.0f;
    const std::string death = zl::game::RunSummaryText(summary);
    CHECK(death.find("死因：撕咬") != std::string::npos);
    CHECK(death.find("存活：1 分 5 秒（第 2 天 03:07）") != std::string::npos);
    CHECK(death.find("击杀：7 只僵尸") != std::string::npos);
    CHECK(death.find("受伤：4 次（累计 96 点）") != std::string::npos);

    summary.victory = true;
    const std::string victory = zl::game::RunSummaryText(summary);
    CHECK(victory.find("死因") == std::string::npos);

    zl::game::ActionHintState hint;
    std::string tone;
    CHECK(zl::game::ActionHintText(hint, &tone).find("WASD") != std::string::npos);
    CHECK(tone == "info");
    hint.bleeding = true;
    CHECK(zl::game::ActionHintText(hint, &tone).find("寻找绷带") != std::string::npos);
    CHECK(tone == "fail");
    hint.hasBandage = true;
    CHECK(zl::game::ActionHintText(hint, &tone).find("使用绷带") != std::string::npos);
    hint.bleeding = false;
    hint.inPain = true;
    CHECK(zl::game::ActionHintText(hint, &tone).find("寻找止痛药") != std::string::npos);
    CHECK(tone == "warn");
    hint.hasPainkiller = true;
    CHECK(zl::game::ActionHintText(hint, &tone).find("使用止痛药") != std::string::npos);
    hint.inPain = false;
    hint.nearbyContainer = "木箱";
    CHECK(zl::game::ActionHintText(hint, &tone) == "按 I 搜刮 木箱");
    hint.nearbyContainer.clear();
    hint.nearbyBed = "床";
    CHECK(zl::game::ActionHintText(hint, &tone) == "按 E 睡觉：床");
    hint.nearbyBed.clear();
    hint.nearbyCampfire = "篝火";
    CHECK(zl::game::ActionHintText(hint, &tone) == "按 E 点燃：篝火");
    hint.campfireState = "燃烧";
    CHECK(zl::game::ActionHintText(hint, &tone).find("按 E 熄灭") !=
          std::string::npos);
    CHECK(tone == "warn");
    hint.campfireState.clear();
    hint.nearbyCampfire.clear();
    hint.nearbyFire = "木墙";
    hint.fireState = "燃烧";
    hint.fireExtinguishItem = "灭火器";
    hint.fireExtinguishChance = 0.95f;
    std::string fireHint = zl::game::ActionHintText(hint, &tone);
    CHECK(fireHint.find("火灾：木墙燃烧") != std::string::npos);
    CHECK(fireHint.find("用灭火器") != std::string::npos);
    CHECK(fireHint.find("95%") != std::string::npos);
    CHECK(fireHint.find("会受伤") == std::string::npos);
    CHECK(tone == "fail");
    hint.fireExtinguishBareHands = true;
    hint.fireExtinguishItem.clear();
    hint.fireExtinguishChance = 0.15f;
    fireHint = zl::game::ActionHintText(hint, &tone);
    CHECK(fireHint.find("徒手拍打") != std::string::npos);
    CHECK(fireHint.find("会受伤") != std::string::npos);
    hint.fireState = "阴燃";
    (void)zl::game::ActionHintText(hint, &tone);
    CHECK(tone == "warn");
    hint.nearbyFire.clear();
    hint.fireState.clear();
    hint.nearbyItem = "绷带";
    CHECK(zl::game::ActionHintText(hint, &tone) == "按 F 拾取 绷带");
    CHECK(tone == "info");
}

TEST_CASE("M23-F1-T8 远程武器 HUD 弹药/换弹/空仓文案") {
    zl::ui::HudState state;
    std::string tone;
    CHECK(zl::ui::RangedStatusText(state, &tone).empty());
    CHECK(tone == "info");

    state.rangedValid = true;
    state.rangedName = "手枪";
    state.rangedLoaded = 8;
    state.rangedMagazine = 8;
    state.rangedReserve = 16;
    CHECK(zl::ui::RangedStatusText(state, &tone) == "手枪 8/8 · 备弹 16");
    CHECK(tone == "info");

    state.rangedLoaded = 2;
    CHECK(zl::ui::RangedStatusText(state, &tone).find("弹药不足") !=
          std::string::npos);
    CHECK(tone == "warn");

    state.rangedLoaded = 0;
    CHECK(zl::ui::RangedStatusText(state, &tone).find("空仓") !=
          std::string::npos);
    CHECK(zl::ui::RangedStatusText(state, &tone).find("R 换弹") !=
          std::string::npos);
    CHECK(tone == "fail");

    state.rangedReloading = true;
    state.rangedReloadProgress = 0.42f;
    CHECK(zl::ui::RangedStatusText(state, &tone) ==
          "手枪 换弹中 42% · 0/8 · 备弹 16");
    CHECK(tone == "warn");

    state.rangedReloading = false;
    state.rangedLoaded = 1;
    state.rangedReserve = 0;
    CHECK(zl::ui::RangedStatusText(state, &tone).find("备弹耗尽") !=
          std::string::npos);
    CHECK(tone == "warn");

    zl::game::ActionHintState hint;
    hint.hasRangedWeapon = true;
    hint.rangedEmpty = true;
    CHECK(zl::game::ActionHintText(hint, &tone).find("空仓：按 R 换弹") !=
          std::string::npos);
    hint.rangedEmpty = false;
    hint.rangedReloading = true;
    CHECK(zl::game::ActionHintText(hint, &tone).find("换弹中") !=
          std::string::npos);
}

TEST_CASE("M23-F2a-T5 觅食/钓鱼 HUD 状态文案") {
    zl::ui::HudState state;
    std::string tone;
    CHECK(zl::ui::ForageStatusText(state, &tone).empty());
    CHECK(zl::ui::FishingStatusText(state, &tone).empty());
    CHECK(tone == "info");

    state.forageValid = true;
    state.forageName = "森林";
    state.forageSeconds = 2.6f;
    CHECK(zl::ui::ForageStatusText(state, &tone) ==
          "可觅食 · 森林（2.6 秒）");
    CHECK(tone == "info");

    state.forageMissingTool = true;
    state.forageToolName = "树枝";
    CHECK(zl::ui::ForageStatusText(state, &tone) ==
          "可觅食 · 森林（需要 树枝）");
    CHECK(tone == "warn");

    state.forageMissingTool = false;
    state.forageExhausted = true;
    state.forageRespawnMinutes = 4.2;
    CHECK(zl::ui::ForageStatusText(state, &tone) ==
          "觅食 · 森林（已耗尽，约 5 分钟刷新）");
    CHECK(tone == "warn");

    state.forageExhausted = false;
    state.forageSearching = true;
    state.forageProgress = 0.42f;
    CHECK(zl::ui::ForageStatusText(state, &tone) == "觅食中 · 森林 42%");
    CHECK(tone == "info");

    state.fishingValid = true;
    state.fishingName = "静水";
    state.fishingSeconds = 5.0f;
    CHECK(zl::ui::FishingStatusText(state, &tone) ==
          "可钓鱼 · 静水（5.0 秒）");
    CHECK(tone == "info");

    state.fishingMissingRod = true;
    state.fishingRodName = "鱼竿";
    CHECK(zl::ui::FishingStatusText(state, &tone) ==
          "可钓鱼 · 静水（需要 鱼竿）");
    CHECK(tone == "warn");

    state.fishingMissingRod = false;
    state.fishingMissingBait = true;
    state.fishingBaitName = "鱼饵";
    CHECK(zl::ui::FishingStatusText(state, &tone) ==
          "可钓鱼 · 静水（需要 鱼饵）");
    CHECK(tone == "warn");

    state.fishingMissingBait = false;
    state.fishingWaiting = true;
    state.fishingProgress = 0.42f;
    CHECK(zl::ui::FishingStatusText(state, &tone) == "钓鱼中 · 静水 42%");
    CHECK(tone == "info");

    state.fishingWaiting = false;
    state.fishingBiting = true;
    state.fishingBiteRemaining = 1.16f;
    CHECK(zl::ui::FishingStatusText(state, &tone) ==
          "咬钩！静水（1.2 秒） · E 收杆");
    CHECK(tone == "warn");
}

TEST_CASE("M32-T3 战略地图 1280x720 与 1920x1080 布局") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));

    const Rml::Vector2i sizes[] = {Rml::Vector2i(1280, 720),
                                    Rml::Vector2i(1920, 1080)};
    for (int index = 0; index < 2; ++index) {
        const std::string name = "world-map-layout-" + std::to_string(index);
        Rml::Context* context = Rml::CreateContext(name, sizes[index]);
        REQUIRE(context != nullptr);

        zl::ui::WorldMapPanel panel;
        REQUIRE(panel.Init(*context, std::string(ZL_SOURCE_DIR) +
                                         "/assets/ui/worldmap.rml"));

        zl::game::WorldMapState state;
        state.valid = true;
        state.mapId = "village_demo_m31";
        state.width = 360.0f;
        state.height = 360.0f;
        state.playerValid = true;
        state.playerX = 70.0f;
        state.playerY = 24.0f;
        state.currentRegionName = "村庄核心";
        state.nearestLandmarkName = "村庄避风港";
        state.regions.push_back({"village_core", "村庄核心", "village", 40.0f,
                                 0.0f, 80.0f, 60.0f, true});
        state.regions.push_back({"south_farmland", "南部农田", "farmland",
                                 0.0f, 120.0f, 120.0f, 100.0f, false});
        state.regions.push_back({"north_fields", "北部农田", "farmland",
                                 120.0f, 60.0f, 100.0f, 60.0f, true});
        state.regions.push_back({"east_wilds", "东部荒野", "wilds",
                                 240.0f, 0.0f, 100.0f, 200.0f, false});
        state.regionCount = 4;
        state.discoveredRegionCount = 2;
        state.roads.push_back({70.0f, 24.0f, 150.0f, 24.0f, false, true});
        state.waters.push_back({198.0f, 0.0f, 15.0f, 360.0f});
        state.pois.push_back(
            {"clinic", "诊所", 150.0f, 64.0f, 0.0f, 2, true, false});
        state.poiCount = 1;
        state.discoveredPoiCount = 1;
        state.settlements.push_back(
            {"village_haven", "村庄营地", "village_core", "neutral", "中立",
             "交易/委托/防御/招募", 0, 75.0f, 26.5f, true, false, false,
             true});
        state.settlements.push_back(
            {"north_fields", "北部诊所", "north_fields", "friendly", "友善",
             "交易/委托/防御", 25, 150.0f, 64.0f, true, false, true, true});
        state.events.push_back(
            {"event", "尸潮", "进行中", 70.0f, 40.0f, true});
        state.objective.valid = true;
        state.objective.title = "找到诊所";
        state.objective.x = 150.0f;
        state.objective.y = 64.0f;
        state.route.valid = true;
        state.route.label = "南部农田";
        state.route.targetRegion = "south_farmland";
        state.route.distance = 90.0f;
        state.route.points = {{70.0f, 24.0f}, {150.0f, 24.0f},
                              {150.0f, 64.0f}};
        state.gameMinute = 400;
        state.travel.walkSpeed = 4.0f;
        state.travel.vehicleSpeed = 10.0f;
        state.travel.fuelPerSecond = 0.012f;
        state.travel.currentFuel = 0.8f;
        state.travel.vehicleAvailable = true;
        state.travel.gameHoursPerRealSecond = 0.4f;
        state.routeCost.valid = true;
        state.routeCost.roadDistance = 90.0f;
        state.routeCost.travelDistance = 108.0f;
        state.routeCost.walkSeconds = 27.0f;
        state.routeCost.vehicleSeconds = 10.8f;
        state.routeCost.walkGameHours = 10.8f;
        state.routeCost.vehicleGameHours = 4.32f;
        state.routeCost.vehicleFuel = 0.1296f;
        state.routeCost.fuelEnough = true;
        state.strategic.enabled = true;
        state.strategic.gameMinute = 400;
        state.strategic.resources = {12, 180, 96, 3};
        zl::game::StrategicMapRegion village;
        village.id = "village_core";
        village.owner = zl::game::StrategicOwner::Human;
        village.pressure = 10.0f;
        village.defense = 70.0f;
        village.supply = 70.0f;
        village.population = 3;
        village.intelLevel = 3;
        village.strategicValue = 5;
        village.coreSafe = true;
        state.strategic.regions.push_back(village);
        zl::game::StrategicMapRegion farm;
        farm.id = "south_farmland";
        farm.owner = zl::game::StrategicOwner::Human;
        farm.siegePhase = zl::game::StrategicSiegePhase::Warning;
        farm.pressure = 80.0f;
        farm.defense = 35.0f;
        farm.supply = 45.0f;
        farm.population = 4;
        farm.intelLevel = 1;
        farm.strategicValue = 3;
        farm.warning.valid = true;
        farm.warning.attackMinute = 1000;
        farm.warning.warningMinutes = 48 * 60;
        farm.warning.remainingMinutes = 600;
        farm.warning.intelLevel = 1;
        farm.warning.enemyStrength = 58;
        farm.warning.reportedEnemyStrength = 56;
        state.strategic.regions.push_back(farm);
        zl::game::StrategicMapRegion north;
        north.id = "north_fields";
        north.owner = zl::game::StrategicOwner::Human;
        north.pressure = 15.0f;
        north.defense = 25.0f;
        north.supply = 60.0f;
        north.intelLevel = 2;
        north.strategicValue = 2;
        state.strategic.regions.push_back(north);
        zl::game::StrategicMapRegion east;
        east.id = "east_wilds";
        east.owner = zl::game::StrategicOwner::Infested;
        east.pressure = 85.0f;
        east.intelLevel = 1;
        east.strategicValue = 4;
        east.remote = true;
        state.strategic.regions.push_back(east);
        state.strategic.events.push_back(
            {"warning.south_farmland.1000", "south_farmland",
             zl::game::StrategicEventKind::Warning, 300,
             zl::game::StrategicOwner::Human, {}});
        state.strategic.events.push_back(
            {"siege.south_farmland.900", "south_farmland",
             zl::game::StrategicEventKind::Siege, 350,
             zl::game::StrategicOwner::Human, {0, -8, -10, 0}});
        state.strategic.events.push_back(
            {"resource.south_farmland.800", "south_farmland",
             zl::game::StrategicEventKind::Resource, 360,
             zl::game::StrategicOwner::Human, {0, -4, 0, 0}});
        panel.SetState(state, true);
        context->Update();

        Rml::Element* panelElement =
            panel.Document()->GetElementById("world-map-panel");
        Rml::Element* canvas =
            panel.Document()->GetElementById("world-map-canvas");
        Rml::Element* sidebar =
            panel.Document()->GetElementById("map-region");
        Rml::Element* marker =
            panel.Document()->GetElementById("map-poi-layer");
        Rml::Element* settlementLayer =
            panel.Document()->GetElementById("map-settlement-layer");
        Rml::Element* settlementList =
            panel.Document()->GetElementById("map-settlement-list");
        Rml::Element* strategicLayer =
            panel.Document()->GetElementById("map-strategic-layer");
        Rml::Element* strategicList =
            panel.Document()->GetElementById("map-strategic-list");
        Rml::Element* strategicAlert =
            panel.Document()->GetElementById("map-strategic-alert");
        Rml::Element* routeCost =
            panel.Document()->GetElementById("map-route-cost");
        Rml::Element* hint =
            panel.Document()->GetElementById("map-hint");
        REQUIRE(panelElement != nullptr);
        REQUIRE(canvas != nullptr);
        REQUIRE(sidebar != nullptr);
        REQUIRE(marker != nullptr);
        REQUIRE(settlementLayer != nullptr);
        REQUIRE(settlementList != nullptr);
        REQUIRE(strategicLayer != nullptr);
        REQUIRE(strategicList != nullptr);
        REQUIRE(strategicAlert != nullptr);
        REQUIRE(routeCost != nullptr);
        REQUIRE(hint != nullptr);
        const Rml::Vector2f offset =
            panelElement->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f box =
            panelElement->GetBox().GetSize(Rml::BoxArea::Border);
        CHECK(offset.x >= 0.0f);
        CHECK(offset.y >= 0.0f);
        CHECK(offset.x + box.x <= static_cast<float>(sizes[index].x));
        CHECK(offset.y + box.y <= static_cast<float>(sizes[index].y));
        CHECK(canvas->GetBox().GetSize(Rml::BoxArea::Border).x > 400.0f);
        CHECK(canvas->GetBox().GetSize(Rml::BoxArea::Border).y > 400.0f);
        const Rml::Vector2f canvasOffset =
            canvas->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f sidebarOffset =
            sidebar->GetAbsoluteOffset(Rml::BoxArea::Border);
        CHECK(canvasOffset.x + canvas->GetBox().GetSize(Rml::BoxArea::Border).x <=
              sidebarOffset.x + 1.0f);
        CHECK(marker->GetNumChildren() > 0);
        CHECK(settlementLayer->GetNumChildren() == 2);
        CHECK(settlementList->GetInnerRML().find("村庄营地") !=
              std::string::npos);
        CHECK(settlementList->GetInnerRML().find("中立") !=
              std::string::npos);
        CHECK(settlementList->GetInnerRML().find("（0）") !=
              std::string::npos);
        CHECK(settlementList->GetInnerRML().find("友善") !=
              std::string::npos);
        CHECK(settlementList->GetInnerRML().find("（25）") !=
              std::string::npos);
        CHECK(settlementList->GetInnerRML().find("夜袭") !=
              std::string::npos);
        Rml::Element* northSettlement = nullptr;
        for (int i = 0; i < settlementLayer->GetNumChildren(); ++i) {
            Rml::Element* child = settlementLayer->GetChild(i);
            if (child->GetAttribute<Rml::String>("data-settlement", "") ==
                "north_fields") {
                northSettlement = child;
                break;
            }
        }
        REQUIRE(northSettlement != nullptr);
        CHECK(northSettlement->IsClassSet("rep-friendly"));
        CHECK(northSettlement->IsClassSet("raid"));
        CHECK(panel.Document()
                  ->GetElementById("map-label-layer")
                  ->GetInnerRML()
                  .find("北部诊所 · 友善") != std::string::npos);
        CHECK(strategicLayer->GetNumChildren() == 4);
        CHECK(strategicList->GetNumChildren() == 4);
        Rml::Element* southOverlay = nullptr;
        Rml::Element* eastOverlay = nullptr;
        for (int i = 0; i < strategicLayer->GetNumChildren(); ++i) {
            Rml::Element* child = strategicLayer->GetChild(i);
            const Rml::String id =
                child->GetAttribute<Rml::String>("data-strategic-region", "");
            if (id == "south_farmland") {
                southOverlay = child;
            } else if (id == "east_wilds") {
                eastOverlay = child;
            }
        }
        REQUIRE(southOverlay != nullptr);
        REQUIRE(eastOverlay != nullptr);
        CHECK(southOverlay->GetClientHeight() > 0.0f);
        CHECK(eastOverlay->GetClientHeight() == 0.0f);
        CHECK(strategicList->GetInnerRML().find("南部农田") !=
              std::string::npos);
        CHECK(strategicAlert->GetInnerRML().find("预警") !=
              std::string::npos);
        CHECK(routeCost->GetInnerRML().find("需油") != std::string::npos);
        CHECK(routeCost->GetInnerRML().find("可提前") != std::string::npos);
        CHECK(routeCost->IsClassSet("ok"));
        REQUIRE(strategicList->GetNumChildren() > 0);
        Rml::Element* lastRow =
            strategicList->GetChild(strategicList->GetNumChildren() - 1);
        REQUIRE(lastRow != nullptr);
        const Rml::Vector2f rowOffset =
            lastRow->GetAbsoluteOffset(Rml::BoxArea::Border);
        const float rowBottom =
            rowOffset.y + lastRow->GetBox().GetSize(Rml::BoxArea::Border).y;
        CHECK(rowBottom <= offset.y + box.y + 1.0f);
        panel.Shutdown();
        Rml::RemoveContext(name);
    }
    Rml::Shutdown();
}

TEST_CASE("M32-T8 战略地图行动面板：选择/支援/反攻回调与禁用态") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());

    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));
    Rml::Context* context =
        Rml::CreateContext("strategic-actions", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);

    zl::ui::WorldMapPanel panel;
    REQUIRE(panel.Init(*context, std::string(ZL_SOURCE_DIR) +
                                     "/assets/ui/worldmap.rml"));

    zl::game::WorldMapState state;
    state.valid = true;
    state.mapId = "village_demo_m31";
    state.width = 360.0f;
    state.height = 360.0f;
    state.currentRegion = "village_core";
    state.strategic.enabled = true;
    state.strategic.gameMinute = 400;
    state.strategic.resources = {12, 180, 96, 3};
    zl::game::StrategicMapRegion farm;
    farm.id = "south_farmland";
    farm.owner = zl::game::StrategicOwner::Human;
    farm.recoveryStage = zl::game::StrategicRecoveryStage::None;
    farm.infectionCores = 0;
    farm.pressure = 20.0f;
    farm.supply = 45.0f;
    farm.population = 2;
    farm.strategicValue = 3;
    farm.warning.valid = true;
    farm.warning.remainingMinutes = 600;
    farm.warning.reportedEnemyStrength = 30;
    state.strategic.regions.push_back(farm);
    zl::game::StrategicMapRegion bridge;
    bridge.id = "riverlands";
    bridge.owner = zl::game::StrategicOwner::Contested;
    bridge.contestedFaction = zl::game::ContestedFaction::Raiders;
    bridge.pressure = 55.0f;
    bridge.supply = 40.0f;
    bridge.population = 2;
    bridge.garrison = 1;
    bridge.strategicValue = 3;
    state.strategic.regions.push_back(bridge);

    std::string selectedRegion;
    zl::game::WorldMapStrategicAction selectedAction =
        zl::game::WorldMapStrategicAction::Travel;
    int actionCalls = 0;
    panel.SetStrategicActionHandler(
        [&](const std::string& regionId,
            zl::game::WorldMapStrategicAction action) {
            selectedRegion = regionId;
            selectedAction = action;
            ++actionCalls;
        });
    panel.SetState(state, true);
    context->Update();

    Rml::Element* actions =
        panel.Document()->GetElementById("map-strategic-actions");
    Rml::Element* selected =
        panel.Document()->GetElementById("map-strategic-selected");
    Rml::Element* support =
        panel.Document()->GetElementById("map-action-support");
    Rml::Element* reclaim =
        panel.Document()->GetElementById("map-action-reclaim");
    REQUIRE(actions != nullptr);
    REQUIRE(selected != nullptr);
    REQUIRE(support != nullptr);
    REQUIRE(reclaim != nullptr);
    CHECK(actions->GetClientHeight() > 0.0f);
    Rml::Element* panelElement =
        panel.Document()->GetElementById("world-map-panel");
    REQUIRE(panelElement != nullptr);
    const Rml::Vector2f actionOffset =
        actions->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f panelOffset =
        panelElement->GetAbsoluteOffset(Rml::BoxArea::Border);
    CHECK(actionOffset.y + actions->GetBox().GetSize(Rml::BoxArea::Border).y <=
          panelOffset.y + panelElement->GetBox().GetSize(
                              Rml::BoxArea::Border).y + 1.0f);
    CHECK(selected->GetInnerRML().find("南部农田") != std::string::npos);
    CHECK_FALSE(support->IsClassSet("disabled"));
    CHECK(reclaim->IsClassSet("disabled"));

    Rml::Dictionary parameters;
    support->DispatchEvent("click", parameters);
    CHECK(actionCalls == 1);
    CHECK(selectedRegion == "south_farmland");
    CHECK(selectedAction == zl::game::WorldMapStrategicAction::Support);

    farm.owner = zl::game::StrategicOwner::Infested;
    farm.recoveryStage = zl::game::StrategicRecoveryStage::Counterattack;
    farm.warning.valid = false;
    farm.warning.remainingMinutes = 0;
    state.currentRegion = "south_farmland";
    state.strategic.regions[0] = farm;
    panel.SetState(state, true);
    context->Update();
    reclaim = panel.Document()->GetElementById("map-action-reclaim");
    REQUIRE(reclaim != nullptr);
    CHECK_FALSE(reclaim->IsClassSet("disabled"));
    Rml::Element* strategicList =
        panel.Document()->GetElementById("map-strategic-list");
    REQUIRE(strategicList != nullptr);
    CHECK(strategicList->GetInnerRML().find("反攻") != std::string::npos);
    CHECK(strategicList->GetInnerRML().find("河谷") != std::string::npos);
    CHECK(strategicList->GetInnerRML().find("争夺中") !=
          std::string::npos);
    CHECK(strategicList->GetInnerRML().find("掠夺者") !=
          std::string::npos);
    reclaim->DispatchEvent("click", parameters);
    CHECK(actionCalls == 2);
    CHECK(selectedAction == zl::game::WorldMapStrategicAction::Reclaim);

    panel.Shutdown();
    Rml::RemoveContext("strategic-actions");
    Rml::Shutdown();
}
TEST_CASE("M33-F4-T4 改装面板效果、失败提示与只读操作") {
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

    Rml::Context* context =
        Rml::CreateContext("vehicle-mods", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    zl::ui::VehicleModsPanel panel;
    REQUIRE(panel.Init(*context, std::string(ZL_SOURCE_DIR) +
                                    "/assets/ui/vehiclemods.rml"));
    std::vector<zl::ui::VehicleModPanelAction> actions;
    panel.SetActionHandler(
        [&actions](const zl::ui::VehicleModPanelAction& action) {
            actions.push_back(action);
        });

    zl::ui::VehicleModsUiState state;
    state.open = true;
    state.valid = true;
    state.subtitle = "皮卡 · 车况 92%";
    state.summary = "总重 86kg · 后备箱 64kg（+24kg） · 极速 +18%";
    state.status = "选择改装安装或拆除；材料来自背包";
    state.slots[0].slotName = "装甲";
    state.slots[0].occupied = true;
    state.slots[0].currentName = "装甲板";
    state.slots[0].currentEffect = "撞墙车况 -32%，撞尸车况 -28%";
    state.slots[0].currentDetail =
        "已安装 · 重量 28 kg · 拆除 15 秒 · 返还 50%";
    state.slots[0].canRemove = true;
    state.slots[1].slotName = "载重";
    state.slots[1].hasCandidate = true;
    state.slots[1].canInstall = true;
    state.slots[1].candidateIndex = 0;
    state.slots[1].candidateCount = 2;
    state.slots[1].candidateName = "行李架";
    state.slots[1].candidateEffect = "后备箱 +24kg";
    state.slots[1].materials = "材料：木材 4/4";
    state.slots[1].duration = "安装 20 秒 · 重量 18 kg";
    panel.SetState(state);
    context->Update();

    auto text = [&](const char* id) {
        Rml::Element* element = panel.Document()->GetElementById(id);
        REQUIRE(element != nullptr);
        return element->GetInnerRML();
    };
    CHECK(text("vm-current-0").find("撞墙车况 -32%") != std::string::npos);
    CHECK(text("vm-candidate-1").find("行李架") != std::string::npos);
    CHECK(text("vm-candidate-1").find("后备箱 +24kg") != std::string::npos);
    CHECK(text("vehicle-mods-summary").find("后备箱 64kg") !=
          std::string::npos);

    auto click = [&](const char* id) {
        Rml::Element* element = panel.Document()->GetElementById(id);
        REQUIRE(element != nullptr);
        const Rml::Vector2f offset =
            element->GetAbsoluteOffset(Rml::BoxArea::Border);
        context->ProcessMouseMove(static_cast<int>(offset.x) + 4,
                                  static_cast<int>(offset.y) + 4, 0);
        context->ProcessMouseButtonDown(0, 0);
        context->ProcessMouseButtonUp(0, 0);
        context->Update();
    };
    click("vm-install-1");
    REQUIRE(actions.size() == 1u);
    CHECK(actions[0].type == zl::ui::VehicleModPanelActionType::Install);
    CHECK(actions[0].slot == 1);
    click("vm-remove-0");
    REQUIRE(actions.size() == 2u);
    CHECK(actions[1].type == zl::ui::VehicleModPanelActionType::Remove);
    CHECK(actions[1].slot == 0);

    state.status = "材料不足：木材 0/4";
    state.statusTone = "fail";
    state.readOnly = true;
    panel.SetState(state);
    context->Update();
    CHECK(text("vehicle-mods-status").find("材料不足") != std::string::npos);
    Rml::Element* status = panel.Document()->GetElementById(
        "vehicle-mods-status");
    REQUIRE(status != nullptr);
    CHECK(status->GetComputedValues().color() == Rml::Colourb(0xef, 0x53, 0x50));
    Rml::Element* install = panel.Document()->GetElementById("vm-install-1");
    Rml::Element* remove = panel.Document()->GetElementById("vm-remove-0");
    REQUIRE(install != nullptr);
    REQUIRE(remove != nullptr);
    CHECK(install->GetComputedValues().display() == Rml::Style::Display::None);
    CHECK(remove->GetComputedValues().display() == Rml::Style::Display::None);
    click("vm-install-1");
    CHECK(actions.size() == 2u);

    click("vehicle-mods-close");
    REQUIRE(actions.size() == 3u);
    CHECK(actions[2].type == zl::ui::VehicleModPanelActionType::Close);

    panel.Shutdown();
    Rml::RemoveContext("vehicle-mods");
    Rml::Shutdown();
}
TEST_CASE("M33-F4-T4 后备箱改装入口与车辆校验") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());
    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));

    Rml::Context* context =
        Rml::CreateContext("vehicle-mod-entry", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    zl::ui::InventoryPanel panel;
    REQUIRE(panel.Init(*context, std::string(ZL_SOURCE_DIR) +
                                    "/assets/ui/inventory.rml"));
    std::vector<zl::ui::InventoryAction> actions;
    panel.SetActionHandler(
        [&actions](const zl::ui::InventoryAction& action) {
            actions.push_back(action);
        });

    zl::ui::InventoryState state;
    state.playerValid = true;
    state.lootIsStorage = true;
    state.lootIsVehicle = true;
    state.vehicleModsEnabled = true;
    state.lootEntityId = 9001;
    state.lootTitle = "后备箱：皮卡（0/64 kg）";
    panel.SetState(state);
    panel.Show();
    context->Update();

    Rml::Element* entry = panel.Document()->GetElementById(
        "storage-vehicle-mods");
    REQUIRE(entry != nullptr);
    CHECK(entry->GetComputedValues().display() != Rml::Style::Display::None);
    CHECK(entry->GetInnerRML().find("车辆改装") != std::string::npos);
    const Rml::Vector2f offset = entry->GetAbsoluteOffset(Rml::BoxArea::Border);
    context->ProcessMouseMove(static_cast<int>(offset.x) + 4,
                              static_cast<int>(offset.y) + 4, 0);
    context->ProcessMouseButtonDown(0, 0);
    context->ProcessMouseButtonUp(0, 0);
    context->Update();
    REQUIRE(actions.size() == 1u);
    CHECK(actions[0].type == zl::ui::InventoryActionType::OpenVehicleMods);
    CHECK(actions[0].fromEntityId == 9001);

    state.lootIsVehicle = false;
    state.lootTitle = "仓库：木箱";
    panel.SetState(state);
    context->Update();
    CHECK(entry->GetComputedValues().display() == Rml::Style::Display::None);

    state.lootIsVehicle = true;
    state.vehicleModsEnabled = false;
    panel.SetState(state);
    context->Update();
    CHECK(entry->GetComputedValues().display() == Rml::Style::Display::None);

    panel.Shutdown();
    Rml::RemoveContext("vehicle-mod-entry");
    Rml::Shutdown();
}