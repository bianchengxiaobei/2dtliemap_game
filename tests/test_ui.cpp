#include <string>

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <doctest/doctest.h>

#include "core/Input.h"
#include "ui/UiInput.h"

namespace {

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
