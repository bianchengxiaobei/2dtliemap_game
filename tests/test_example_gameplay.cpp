#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <RmlUi/Core.h>
#include <nlohmann/json.hpp>

#include "ScriptTestHelpers.h"
#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "core/Config.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/BuildingRules.h"
#include "game/GameContext.h"
#include "game/ProgressionRules.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "net/Command.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "script/EventBridge.h"
#include "script/LuaRuntime.h"
#include "script/ModDataComponents.h"
#include "script/ModLoader.h"
#include "script/ScriptApi.h"
#include "script/ScriptManager.h"
#include "ui/HudState.h"
#include "ui/ScriptUiHost.h"

using namespace zl;
using zltest::LogCapture;

namespace {

constexpr const char* kModId = "example_scripted_gameplay";

std::filesystem::path ExampleModRoot() {
    return std::filesystem::path(ZL_SOURCE_DIR) / "mods" / kModId;
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
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

class NullSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return 0.0; }
    bool LogMessage(Rml::Log::Type, const Rml::String&) override { return true; }
};

struct Rect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

Rect BorderRect(Rml::Element* element) {
    const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
    return {offset.x, offset.y, offset.x + size.x, offset.y + size.y};
}

bool Overlaps(const Rect& a, const Rect& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom &&
           a.bottom > b.top;
}

void LoadUiFonts() {
    const std::string fontDir = std::string(ZL_SOURCE_DIR) + "/assets/fonts/";
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Regular.otf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Normal));
    REQUIRE(Rml::LoadFontFace(fontDir + "NotoSansSC-Bold.ttf", "zl_ui",
                              Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Bold));
}

std::string ElementText(Rml::ElementDocument* document, const std::string& id) {
    Rml::Element* element = document != nullptr
                                ? document->GetElementById(id)
                                : nullptr;
    return element != nullptr ? element->GetInnerRML() : std::string();
}

struct Harness {
    ecs::EcsWorld ecs;
    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    core::Config config;
    core::GameLoop loop{60.0};
    core::GameTime gameTime;
    game::ProgressionConfig progressionConfig = game::DefaultProgressionConfig();
    game::GameContext ctx;
    core::EventBus bus;
    script::ScriptApi api;
    script::ScriptManager manager;
    std::unique_ptr<script::EventBridge> bridge;
    ui::ScriptUiHost* uiHost = nullptr;
    ecs::Entity player = ecs::EcsWorld::kNull;
    std::uint64_t playerId = 0;

    std::vector<net::Command> commands;
    int buildPlaceCount = 0;
    int buildToggleCount = 0;
    int buildRepairCount = 0;
    int buildDemolishCount = 0;
    std::string builtDef;
    int builtX = 0;
    int builtY = 0;
    std::set<std::uint64_t> killedZombies;
    std::int64_t tick = 0;

    Harness() {
        std::string error;
        CHECK(config.LoadFile(std::filesystem::path(ZL_SOURCE_DIR) / "config" /
                              "game.json"));
        CHECK(items.LoadFile(std::filesystem::path(ZL_SOURCE_DIR) / "assets" /
                                 "items.json",
                             &error));
        CHECK(recipes.LoadFile(std::filesystem::path(ZL_SOURCE_DIR) / "assets" /
                                   "recipes.json",
                               items, &error));
        gameTime.Configure(1.0, core::DateTime{});

        ctx.config = &config;
        ctx.ecs = &ecs;
        ctx.items = &items;
        ctx.recipes = &recipes;
        ctx.loop = &loop;
        ctx.gameTime = &gameTime;
        ctx.progressionConfig = &progressionConfig;
        ctx.events = &bus;
        ctx.hudState.active = true;
        ctx.submitPlayerCommand = [this](const net::Command& command, bool& queued,
                                         std::string&) {
            commands.push_back(command);
            queued = false;
            return true;
        };
        ctx.buildCanPlace = [](const game::BuildableDef&, int, int, bool,
                               std::string&) { return true; };
        ctx.buildPlace = [this](const game::BuildableDef& def, int x, int y, bool,
                                std::string&) {
            ++buildPlaceCount;
            builtDef = def.id;
            builtX = x;
            builtY = y;
            return true;
        };
        ctx.buildToggleDoor = [this](int, int, std::string&) {
            ++buildToggleCount;
            return true;
        };
        ctx.buildRepair = [this](int, int, std::string&) {
            ++buildRepairCount;
            return true;
        };
        ctx.buildDemolish = [this](int, int, std::string&) {
            ++buildDemolishCount;
            return true;
        };
        api.Attach(&ctx);
        CreatePlayer();
    }

    void CreatePlayer() {
        player = ecs.Create();
        auto& registry = ecs.Registry();
        registry.emplace<ecs::PlayerTag>(player);
        registry.emplace<ecs::Position>(player, ecs::Position{3.0f, 4.0f, 0});
        registry.emplace<ecs::Velocity>(player);
        registry.emplace<ecs::Health>(player, ecs::Health{100.0f, 100.0f});
        registry.emplace<ecs::Needs>(player,
                                     ecs::Needs{0.2f, 0.2f, 0.1f, 1.0f, 1.0f, 0.0f, 0});
        registry.emplace<ecs::Progression>(player);
        ecs::Container bag;
        bag.capacityKg = 40.0f;
        bag.items.push_back(ecs::ItemInstance{"axe", 1, 1.0f, 0});
        bag.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f, 0});
        bag.items.push_back(ecs::ItemInstance{"rag", 4, 1.0f, 0});
        bag.items.push_back(ecs::ItemInstance{"wood", 4, 1.0f, 0});
        registry.emplace<ecs::Container>(player, std::move(bag));
        playerId = ecs.IdOf(player).id;
    }

    void LoadMod() {
        auto lua = std::make_unique<script::LuaRuntime>();
        lua->SetApi(&api);
        lua->SetUiHost(uiHost);
        manager.RegisterRuntime(std::move(lua));
        REQUIRE(manager.InitializeAll());
        manager.SetEventBus(&bus);
        bridge = std::make_unique<script::EventBridge>(bus, manager);

        const std::filesystem::path root = ExampleModRoot();
        REQUIRE(std::filesystem::exists(root / "mod.json"));
        std::string error;
        script::ModInfo info;
        REQUIRE(script::ModLoader::ParseModInfo(ReadText(root / "mod.json"), root,
                                                "lua", info, error));
        REQUIRE_MESSAGE(manager.LoadMod(info, error), error);
    }

    void StartWorld(bool isNewGame = true) {
        bus.EmitQueued(core::kOnWorldStart,
                       core::WorldStartEvent{"example_test", isNewGame, 1,
                                             playerId});
        bus.FlushQueued();
    }

    void KillNewZombies() {
        std::vector<std::uint64_t> ids;
        for (const entt::entity entity : ecs.Registry().view<ecs::AiState>()) {
            const std::uint64_t id = ecs.IdOf(entity).id;
            if (killedZombies.insert(id).second) {
                ids.push_back(id);
            }
        }
        for (const std::uint64_t id : ids) {
            bus.EmitQueued(core::kOnZombieDead,
                           core::ZombieEvent{id, playerId, 0.0f, 0.0f, 0});
        }
        bus.FlushQueued();
    }

    void Tick(int count) {
        for (int i = 0; i < count; ++i) {
            bus.EmitQueued(core::kOnTick, ++tick);
            bus.FlushQueued();
            KillNewZombies();
        }
    }

    void EmitCraftFinished() {
        bus.EmitQueued(core::kOnCraftFinished,
                       core::CraftFinishedEvent{playerId, "field_bandage",
                                                "bandage", 1, 1, false});
        bus.FlushQueued();
    }

    void EmitBuildPlaced() {
        bus.EmitQueued(core::kOnBuildPlaced,
                       core::BuildPlacedEvent{playerId, "door", builtX, builtY, 0,
                                              false});
        bus.FlushQueued();
    }

    void DamagePlayer() {
        auto& health = ecs.Registry().get<ecs::Health>(player);
        health.current = 20.0f;
        core::DamageEvent event;
        event.targetId = playerId;
        event.sourceId = 0;
        event.amount = 10.0f;
        event.damageType = "claw";
        event.cause = "claw";
        event.remainingHp = 10.0f;
        bus.EmitQueued(core::kOnDamage, event);
        bus.FlushQueued();
    }

    std::string Field(const std::string& id) const {
        ui::HudState state;
        (void)api.GetHudState(state);
        for (int i = 0; i < state.modFieldCount; ++i) {
            if (state.modFields[static_cast<std::size_t>(i)].id == id) {
                return state.modFields[static_cast<std::size_t>(i)].text;
            }
        }
        return {};
    }

    std::optional<std::string> Data(const std::string& key) const {
        return api.GetModData(kModId, key);
    }

    int RunScore() const {
        const auto text = Data("run");
        if (!text.has_value()) {
            return -1;
        }
        const nlohmann::json doc =
            nlohmann::json::parse(*text, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) {
            return -2;
        }
        return doc.value("score", -3);
    }

    int ActionCount(net::CommandType type) const {
        return static_cast<int>(std::count_if(
            commands.begin(), commands.end(),
            [type](const net::Command& command) { return command.type == type; }));
    }
};

}  // namespace

TEST_CASE("M35-F5-T1：example_scripted_gameplay 完整一局、HUD 与动作覆盖") {
    Harness f;
    LogCapture capture;
    f.LoadMod();
    REQUIRE(f.manager.IsModLoaded(kModId));
    CHECK(f.Field("challenge_state") == "等待世界");
    CHECK(f.Field("challenge_score").find("最佳 0") != std::string::npos);

    f.StartWorld(true);
    CHECK(f.Field("challenge_state").find("准备") != std::string::npos);
    CHECK(f.Field("challenge_score").find("本局 0") != std::string::npos);
    CHECK(f.buildPlaceCount == 1);
    CHECK(f.builtDef == "door");
    CHECK(f.ActionCount(net::CommandType::EquipBag) == 1);

    f.DamagePlayer();
    CHECK(f.ActionCount(net::CommandType::UseBag) == 1);

    f.Tick(260);
    REQUIRE(f.RunScore() > 0);
    const int beforeCraft = f.RunScore();
    f.EmitCraftFinished();
    CHECK(f.RunScore() == beforeCraft + 15);
    const int beforeBuild = f.RunScore();
    f.EmitBuildPlaced();
    CHECK(f.RunScore() == beforeBuild + 20);

    for (int i = 0; i < 6000 && !f.Data("best_score").has_value(); ++i) {
        f.Tick(1);
    }
    const auto best = f.Data("best_score");
    REQUIRE(best.has_value());
    CHECK(std::stoi(*best) > 0);
    CHECK(f.Data("best_wave").value() == "3");
    CHECK(f.Data("runs").value() == "1");
    CHECK(f.Data("last_score") == best);
    CHECK(f.Field("challenge_state").find("完成") != std::string::npos);
    CHECK(f.Field("challenge_score").find("最佳 " + *best) != std::string::npos);
    CHECK(f.buildToggleCount == 1);
    CHECK(f.buildRepairCount == 1);
    CHECK(f.buildDemolishCount == 1);
    CHECK(f.ActionCount(net::CommandType::InventoryAction) >= 1);
    CHECK(f.ecs.Registry().get<ecs::Progression>(f.player).xp > 0.0f);
    CHECK_FALSE(f.manager.IsModDisabled(kModId));
    CHECK(capture.Str().find("脚本错误") == std::string::npos);
}

TEST_CASE("M35-F5-T1：示例 mod 热重载与 Mod.Data 存档成绩恢复") {
    Harness f;
    LogCapture capture;
    f.LoadMod();
    f.StartWorld(true);
    for (int i = 0; i < 6000 && !f.Data("best_score").has_value(); ++i) {
        f.Tick(1);
    }
    const auto best = f.Data("best_score");
    REQUIRE(best.has_value());
    const int bestScore = std::stoi(*best);
    REQUIRE(bestScore > 0);
    REQUIRE(f.Data("run").has_value());

    save::ComponentRegistry components;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(components, &error));
    const ecs::Entity modData = ecs::FindModDataEntity(f.ecs);
    REQUIRE(modData != ecs::EcsWorld::kNull);
    save::SavedEntity saved;
    REQUIRE(save::CaptureEntity(f.ecs, modData, components, &saved, &error));
    save::EntitySnapshot snapshot;
    snapshot.entities.push_back(saved);
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    REQUIRE(decoded.entities.size() == 1u);

    Harness restored;
    const ecs::Entity loaded =
        restored.ecs.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], restored.ecs, loaded,
                              components, &error));
    LogCapture restoredCapture;
    restored.LoadMod();
    CHECK(restored.Field("challenge_score").find("最佳 " +
                                                  std::to_string(bestScore)) !=
          std::string::npos);
    restored.StartWorld(false);
    CHECK(restored.Field("challenge_state").find("完成") != std::string::npos);

    f.Tick(12 * 60 + 1);
    f.Tick(5 * 60 + 10);
    const int scoreBeforeReload = f.RunScore();
    REQUIRE(scoreBeforeReload > 0);
    REQUIRE(f.manager.ReloadMod(kModId, error));
    f.bus.FlushQueued();
    CHECK(f.RunScore() == scoreBeforeReload);
    CHECK(f.Field("challenge_state").find("波次") != std::string::npos);
    CHECK(f.Field("challenge_score").find("最佳 " + *best) != std::string::npos);
    CHECK_FALSE(f.manager.IsModDisabled(kModId));
    CHECK(restoredCapture.Str().find("脚本错误") == std::string::npos);
    CHECK(capture.Str().find("脚本错误") == std::string::npos);
}

TEST_CASE("M35-F5-T2：challenge.rml 在 1280/1920 不与内置 HUD 重叠") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());
    LoadUiFonts();

    const Rml::Vector2i sizes[] = {Rml::Vector2i(1280, 720),
                                    Rml::Vector2i(1920, 1080)};
    for (int index = 0; index < 2; ++index) {
        const std::string name = "m35-f5-t2-layout-" + std::to_string(index);
        Rml::Context* context = Rml::CreateContext(name, sizes[index]);
        REQUIRE(context != nullptr);

        Rml::ElementDocument* hud = context->LoadDocument(
            (std::filesystem::path(ZL_SOURCE_DIR) / "mods" / "core_ui" / "ui" /
             "hud.rml").generic_string());
        Rml::ElementDocument* challenge = context->LoadDocument(
            (std::filesystem::path(ZL_SOURCE_DIR) / "mods" /
             "example_scripted_gameplay" / "ui" / "challenge.rml")
                .generic_string());
        REQUIRE(hud != nullptr);
        REQUIRE(challenge != nullptr);
        hud->Show();
        challenge->Show();
        context->Update();

        Rml::Element* panel = challenge->GetElementById("challenge-panel");
        Rml::Element* hide = challenge->GetElementById("challenge-hide");
        REQUIRE(panel != nullptr);
        REQUIRE(hide != nullptr);
        const Rect panelRect = BorderRect(panel);
        CHECK(panelRect.left >= 0.0f);
        CHECK(panelRect.top >= 0.0f);
        CHECK(panelRect.right <= static_cast<float>(sizes[index].x) - 15.0f);
        CHECK(panelRect.bottom <= static_cast<float>(sizes[index].y) - 15.0f);
        CHECK(panel->GetClientHeight() > 0.0f);
        CHECK(panel->GetScrollHeight() <= panel->GetClientHeight() + 1.0f);
        CHECK(hide->GetClientWidth() > 0.0f);
        CHECK(hide->GetClientHeight() > 0.0f);

        const char* hudIds[] = {"hud-clock", "hud-right", "hud-left", "hotbar",
                                "hud-bag"};
        for (const char* id : hudIds) {
            Rml::Element* element = hud->GetElementById(id);
            REQUIRE(element != nullptr);
            CHECK_FALSE(Overlaps(panelRect, BorderRect(element)));
        }

        Rml::RemoveContext(name);
    }
    Rml::Shutdown();
}

TEST_CASE("M35-F5-T2：脚本面板更新且关闭后挑战继续") {
    NullRenderInterface renderInterface;
    NullSystemInterface systemInterface;
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetSystemInterface(&systemInterface);
    REQUIRE(Rml::Initialise());
    LoadUiFonts();

    Rml::Context* context = Rml::CreateContext("m35-f5-t2-ui", Rml::Vector2i(1280, 720));
    REQUIRE(context != nullptr);
    {
        ui::ScriptUiHost host(*context);
        Harness f;
        f.uiHost = &host;
        host.SetEventBus(&f.bus);
        LogCapture capture;
        f.LoadMod();

        f.bus.EmitQueued(core::kOnUiReady);
        f.bus.FlushQueued();
        REQUIRE(host.IsLoaded(kModId, "challenge"));
        CHECK(host.IsVisible(kModId, "challenge"));
        CHECK_FALSE(host.IsInputCaptured());

        Rml::ElementDocument* document = context->GetDocument(0);
        REQUIRE(document != nullptr);
        context->Update();
        CHECK(ElementText(document, "challenge-state") == "等待世界");
        CHECK(ElementText(document, "challenge-timer") == "--");
        CHECK(ElementText(document, "challenge-objective").find("进入世界") !=
              std::string::npos);

        f.StartWorld(true);
        context->Update();
        CHECK(ElementText(document, "challenge-state") == "准备");
        CHECK(ElementText(document, "challenge-wave") == "1/3");
        CHECK(ElementText(document, "challenge-timer") == "4s");
        CHECK(ElementText(document, "challenge-objective").find("第 1 波") !=
              std::string::npos);

        f.Tick(250);
        context->Update();
        CHECK(ElementText(document, "challenge-state") == "挑战中");
        CHECK(ElementText(document, "challenge-objective").find("击杀") !=
              std::string::npos);
        CHECK(std::stoi(ElementText(document, "challenge-score")) > 0);
        Rml::Element* panel = document->GetElementById("challenge-panel");
        REQUIRE(panel != nullptr);
        CHECK(panel->GetScrollHeight() <= panel->GetClientHeight() + 1.0f);

        Rml::Element* hide = document->GetElementById("challenge-hide");
        REQUIRE(hide != nullptr);
        context->Update();
        const Rect hideRect = BorderRect(hide);
        host.BeginFrame();
        context->ProcessMouseMove(
            static_cast<int>((hideRect.left + hideRect.right) * 0.5f),
            static_cast<int>((hideRect.top + hideRect.bottom) * 0.5f), 0);
        context->ProcessMouseButtonDown(0, 0);
        context->ProcessMouseButtonUp(0, 0);
        context->Update();
        host.EndFrame();
        CHECK_FALSE(host.IsLoaded(kModId, "challenge"));

        const int scoreBefore = f.RunScore();
        f.Tick(400);
        CHECK(f.RunScore() > scoreBefore);
        CHECK_FALSE(f.Field("challenge_state").empty());
        CHECK_FALSE(host.IsLoaded(kModId, "challenge"));
        CHECK_FALSE(f.manager.IsModDisabled(kModId));
        CHECK(capture.Str().find("脚本错误") == std::string::npos);
    }

    Rml::RemoveContext("m35-f5-t2-ui");
    Rml::Shutdown();
}
