#include "ui/WorldMapUi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <utility>

#include <RmlUi/Core.h>

#include "core/Log.h"

namespace zl::ui {
namespace {

class FunctionListener final : public Rml::EventListener {
public:
    explicit FunctionListener(std::function<void(Rml::Event&)> action)
        : m_action(std::move(action)) {}

    void ProcessEvent(Rml::Event& event) override {
        if (m_action) {
            m_action(event);
        }
    }

private:
    std::function<void(Rml::Event&)> m_action;
};

void SetText(Rml::Element* element, const std::string& text) {
    if (element != nullptr) {
        element->SetInnerRML(text);
    }
}

void SetDisplay(Rml::Element* element, bool visible) {
    if (element != nullptr) {
        element->SetProperty("display", visible ? "block" : "none");
    }
}

std::string Percent(float value, float total) {
    if (!(total > 0.0f) || !std::isfinite(value)) {
        return "0%";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4f%%",
                  std::clamp(value / total * 100.0f, -100.0f, 200.0f));
    return buffer;
}

void Place(Rml::Element* element, float x, float y, float width, float height,
           float worldWidth, float worldHeight) {
    if (element == nullptr) {
        return;
    }
    element->SetProperty("left", Percent(x, worldWidth));
    element->SetProperty("top", Percent(y, worldHeight));
    if (width >= 0.0f) {
        element->SetProperty("width", Percent(width, worldWidth));
    }
    if (height >= 0.0f) {
        element->SetProperty("height", Percent(height, worldHeight));
    }
}

std::string RouteKey(const game::WorldMapRoute& route) {
    if (!route.valid) {
        return "none";
    }
    return std::to_string(route.points.size()) + ":" +
           std::to_string(static_cast<int>(route.distance));
}

std::string RegionName(const game::WorldMapState& state,
                       const std::string& id) {
    const auto found = std::find_if(
        state.regions.begin(), state.regions.end(),
        [&](const game::WorldMapRegion& region) { return region.id == id; });
    if (found != state.regions.end() && !found->name.empty()) {
        return found->name;
    }
    return game::WorldMapRegionName(id);
}

std::string TravelSeconds(float seconds) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.0f 秒",
                  static_cast<double>(seconds));
    return buffer;
}

std::string TravelHours(float hours) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f 游戏时",
                  static_cast<double>(hours));
    return buffer;
}

std::string WarningHours(int minutes) {
    if (minutes <= 0) {
        return "已到期";
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "剩余 %d 小时",
                  (minutes + 59) / 60);
    return buffer;
}

std::string EventAge(std::int64_t now, std::int64_t then) {
    const std::int64_t diff = std::max<std::int64_t>(0, now - then);
    if (diff < 60) {
        return "刚刚";
    }
    if (diff < 24 * 60) {
        return std::to_string(diff / 60) + " 分钟前";
    }
    return std::to_string(diff / (24 * 60)) + " 天前";
}

std::string DeltaText(const game::StrategicResources& delta) {
    std::string text;
    const auto append = [&](const char* label, int value) {
        if (value == 0) {
            return;
        }
        if (!text.empty()) {
            text += " ";
        }
        text += label;
        text += value > 0 ? "+" : "";
        text += std::to_string(value);
    };
    append("人", delta.people);
    append("补", delta.supplies);
    append("弹", delta.ammo);
    append("油", delta.fuel);
    return text;
}

const char* OwnerClass(game::StrategicOwner owner) {
    switch (owner) {
        case game::StrategicOwner::Human:
            return "owner-human";
        case game::StrategicOwner::Contested:
            return "owner-contested";
        case game::StrategicOwner::Infested:
            return "owner-infested";
        case game::StrategicOwner::Neutral:
            return "owner-neutral";
    }
    return "owner-neutral";
}

std::string EventLine(const game::WorldMapState& state,
                      const game::StrategicMapEvent& event) {
    std::string text = EventAge(state.strategic.gameMinute,
                                event.gameMinute);
    text += " · ";
    text += RegionName(state, event.regionId);
    text += " · ";
    text += game::StrategicEventKindName(event.kind);
    if (event.kind == game::StrategicEventKind::Siege) {
        if (event.ownerAfter == game::StrategicOwner::Human) {
            text += " · 守住";
        } else if (event.ownerAfter == game::StrategicOwner::Contested) {
            text += " · 核心失守转争夺";
        } else {
            text += " · 失守";
        }
    } else if (event.kind == game::StrategicEventKind::Owner) {
        text += " → ";
        text += game::StrategicOwnerLabel(event.ownerAfter);
    } else if (event.kind == game::StrategicEventKind::Refugee) {
        text += " · 平民撤入";
    } else if (event.kind == game::StrategicEventKind::Recovery) {
        text += " · ";
        text += game::StrategicOwnerLabel(event.ownerAfter);
    }
    const std::string delta = DeltaText(event.delta);
    if (!delta.empty()) {
        text += " · ";
        text += delta;
    }
    return text;
}

const char* EventToneClass(game::StrategicEventKind kind) {
    switch (kind) {
        case game::StrategicEventKind::Warning:
            return "warning";
        case game::StrategicEventKind::Siege:
            return "siege";
        case game::StrategicEventKind::Recovery:
            return "recovery";
        case game::StrategicEventKind::Refugee:
            return "warning";
        default:
            return "";
    }
}

std::string StrategicSidebarKey(const game::WorldMapState& state) {
    std::string key;
    key += state.strategic.enabled ? "1" : "0";
    key += "|" + std::to_string(state.strategic.gameMinute);
    key += "|" + std::to_string(state.strategic.resources.people) + "," +
           std::to_string(state.strategic.resources.supplies) + "," +
           std::to_string(state.strategic.resources.ammo) + "," +
           std::to_string(state.strategic.resources.fuel);
    key += "|" + std::to_string(
                     static_cast<int>(state.travel.gameHoursPerRealSecond *
                                      1000.0f));
    for (const game::StrategicMapRegion& region :
         state.strategic.regions) {
        key += "|" + region.id + ":" +
               std::to_string(static_cast<int>(region.owner)) + "," +
               std::to_string(
                   static_cast<int>(region.contestedFaction)) + "," +
               std::to_string(static_cast<int>(region.siegePhase)) + "," +
               std::to_string(static_cast<int>(region.pressure * 10.0f)) + "," +
               std::to_string(static_cast<int>(region.defense)) + "," +
               std::to_string(static_cast<int>(region.supply)) + "," +
               std::to_string(region.population) + "," +
               std::to_string(region.garrison) + "," +
               std::to_string(region.fortification) + "," +
               std::to_string(static_cast<int>(region.recoveryStage)) + "," +
               std::to_string(region.facilityDamage) + "," +
               std::to_string(region.refugees) + "," +
               std::to_string(region.infectionCores) + "," +
               std::to_string(region.recoveryReadyMinute) + "," +
               std::to_string(region.warning.valid ? 1 : 0) + "," +
               std::to_string(region.warning.remainingMinutes) + "," +
               std::to_string(region.intelLevel) + "," +
               std::to_string(region.warning.reportedEnemyStrength) + "," +
               std::to_string(
                   static_cast<int>(region.coreSafe ? 1 : 0));
    }
    for (const game::StrategicMapEvent& event : state.strategic.events) {
        key += "|" + event.id + ":" + std::to_string(
                                            static_cast<int>(event.kind));
    }
    return key;
}

}  // namespace

WorldMapPanel::WorldMapPanel() = default;

WorldMapPanel::~WorldMapPanel() { Shutdown(); }

bool WorldMapPanel::Init(Rml::Context& context, const std::string& path) {
    m_context = &context;
    m_document = context.LoadDocument(path);
    if (m_document == nullptr) {
        ZL_WARN("ui", "世界地图文档加载失败：{}", path);
        m_context = nullptr;
        return false;
    }
    m_overlay = m_document->GetElementById("world-map");
    m_regionLayer = m_document->GetElementById("map-region-layer");
    m_strategicRegionLayer =
        m_document->GetElementById("map-strategic-layer");
    m_roadLayer = m_document->GetElementById("map-road-layer");
    m_waterLayer = m_document->GetElementById("map-water-layer");
    m_fogLayer = m_document->GetElementById("map-fog-layer");
    m_landmarkLayer = m_document->GetElementById("map-landmark-layer");
    m_settlementLayer =
        m_document->GetElementById("map-settlement-layer");
    m_poiLayer = m_document->GetElementById("map-poi-layer");
    m_eventLayer = m_document->GetElementById("map-event-layer");
    m_routeLayer = m_document->GetElementById("map-route-layer");
    m_labelLayer = m_document->GetElementById("map-label-layer");
    m_settlementList = m_document->GetElementById("map-settlement-list");
    m_strategicResources =
        m_document->GetElementById("map-strategic-resources");
    m_strategicAlert = m_document->GetElementById("map-strategic-alert");
    m_strategicList = m_document->GetElementById("map-strategic-list");
    m_strategicLog = m_document->GetElementById("map-strategic-log");
    m_strategicActions =
        m_document->GetElementById("map-strategic-actions");
    m_strategicSelected =
        m_document->GetElementById("map-strategic-selected");
    m_actionTravel = m_document->GetElementById("map-action-travel");
    m_actionSupport = m_document->GetElementById("map-action-support");
    m_actionDonate = m_document->GetElementById("map-action-donate");
    m_actionClear = m_document->GetElementById("map-action-clear");
    m_actionRepair = m_document->GetElementById("map-action-repair");
    m_actionReclaim = m_document->GetElementById("map-action-reclaim");
    if (m_overlay == nullptr || m_regionLayer == nullptr ||
        m_strategicRegionLayer == nullptr || m_roadLayer == nullptr ||
        m_waterLayer == nullptr ||
        m_fogLayer == nullptr || m_landmarkLayer == nullptr ||
        m_settlementLayer == nullptr || m_poiLayer == nullptr ||
        m_eventLayer == nullptr || m_routeLayer == nullptr ||
        m_labelLayer == nullptr || m_settlementList == nullptr ||
        m_strategicResources == nullptr ||
        m_strategicAlert == nullptr || m_strategicList == nullptr ||
        m_strategicLog == nullptr || m_strategicActions == nullptr ||
        m_strategicSelected == nullptr || m_actionTravel == nullptr ||
        m_actionSupport == nullptr || m_actionDonate == nullptr ||
        m_actionClear == nullptr || m_actionRepair == nullptr ||
        m_actionReclaim == nullptr) {
        ZL_WARN("ui", "世界地图文档缺少图层元素");
    }
    AddEventListener(m_document->GetElementById("world-map-close"), "click",
                    [this](Rml::Event&) {
                        Hide();
                        if (m_closeHandler) {
                            m_closeHandler();
                        }
                    });
    AddEventListener(m_strategicList, "click", [this](Rml::Event& event) {
        Rml::Element* target = event.GetTargetElement();
        while (target != nullptr && target != m_strategicList) {
            const Rml::String id =
                target->GetAttribute<Rml::String>("data-strategic-region", "");
            if (!id.empty()) {
                SelectStrategicRegion(id);
                return;
            }
            target = target->GetParentNode();
        }
    });
    const auto bindAction = [this](Rml::Element* button,
                                   game::WorldMapStrategicAction action) {
        AddEventListener(button, "click", [this, action](Rml::Event&) {
            if (m_selectedStrategicRegion.empty() ||
                !IsStrategicActionEnabled(action) ||
                !m_strategicActionHandler) {
                return;
            }
            m_strategicActionHandler(m_selectedStrategicRegion, action);
        });
    };
    bindAction(m_actionTravel, game::WorldMapStrategicAction::Travel);
    bindAction(m_actionSupport, game::WorldMapStrategicAction::Support);
    bindAction(m_actionDonate, game::WorldMapStrategicAction::Donate);
    bindAction(m_actionClear, game::WorldMapStrategicAction::ClearCore);
    bindAction(m_actionRepair, game::WorldMapStrategicAction::Repair);
    bindAction(m_actionReclaim, game::WorldMapStrategicAction::Reclaim);
    Hide();
    return true;
}

void WorldMapPanel::Shutdown() {
    for (ListenerBinding& binding : m_listeners) {
        if (binding.element != nullptr) {
            binding.element->RemoveEventListener(binding.event,
                                                 binding.listener.get());
        }
    }
    m_listeners.clear();
    for (ListenerBinding& binding : m_dynamicListeners) {
        if (binding.element != nullptr) {
            binding.element->RemoveEventListener(binding.event,
                                                 binding.listener.get());
        }
    }
    m_dynamicListeners.clear();
    m_document = nullptr;
    m_overlay = nullptr;
    m_regionLayer = nullptr;
    m_strategicRegionLayer = nullptr;
    m_roadLayer = nullptr;
    m_waterLayer = nullptr;
    m_fogLayer = nullptr;
    m_landmarkLayer = nullptr;
    m_settlementLayer = nullptr;
    m_poiLayer = nullptr;
    m_eventLayer = nullptr;
    m_routeLayer = nullptr;
    m_labelLayer = nullptr;
    m_player = nullptr;
    m_playerLabel = nullptr;
    m_objective = nullptr;
    m_objectiveLabel = nullptr;
    m_settlementList = nullptr;
    m_strategicResources = nullptr;
    m_strategicAlert = nullptr;
    m_strategicList = nullptr;
    m_strategicLog = nullptr;
    m_strategicActions = nullptr;
    m_strategicSelected = nullptr;
    m_actionTravel = nullptr;
    m_actionSupport = nullptr;
    m_actionDonate = nullptr;
    m_actionClear = nullptr;
    m_actionRepair = nullptr;
    m_actionReclaim = nullptr;
    m_selectedStrategicRegion.clear();
    m_regions.clear();
    m_strategicRegions.clear();
    m_landmarks.clear();
    m_pois.clear();
    m_events.clear();
    m_worldEvents.clear();
    m_staticKey.clear();
    m_routeKey.clear();
    m_strategicKey.clear();
    m_built = false;
    m_open = false;
    m_context = nullptr;
}

void WorldMapPanel::SetCloseHandler(std::function<void()> handler) {
    m_closeHandler = std::move(handler);
}

void WorldMapPanel::SetSelectHandler(
    std::function<void(const std::string&)> handler) {
    m_selectHandler = std::move(handler);
}

void WorldMapPanel::SetStrategicActionHandler(
    std::function<void(const std::string&,
                       game::WorldMapStrategicAction)> handler) {
    m_strategicActionHandler = std::move(handler);
}

void WorldMapPanel::SelectStrategicRegion(const std::string& regionId) {
    if (m_state.strategic.FindRegion(regionId) == nullptr) {
        return;
    }
    m_selectedStrategicRegion = regionId;
    UpdateStrategicActions();
    UpdateStrategicSelection();
}

bool WorldMapPanel::IsStrategicActionEnabled(
    game::WorldMapStrategicAction action) const {
    const game::StrategicMapRegion* region = m_state.strategic.FindRegion(
        m_selectedStrategicRegion);
    if (region == nullptr) {
        return false;
    }
    const game::StrategicResources& resources = m_state.strategic.resources;
    switch (action) {
        case game::WorldMapStrategicAction::Travel:
            return true;
        case game::WorldMapStrategicAction::Support:
            return (region->owner == game::StrategicOwner::Human ||
                    region->owner == game::StrategicOwner::Contested) &&
                   region->supply < 100.0f && resources.supplies >= 20;
        case game::WorldMapStrategicAction::Donate:
            return (region->owner == game::StrategicOwner::Human ||
                    region->owner == game::StrategicOwner::Contested) &&
                   region->supply < 100.0f && resources.supplies >= 40;
        case game::WorldMapStrategicAction::ClearCore:
            return region->owner == game::StrategicOwner::Infested &&
                   region->recoveryStage ==
                       game::StrategicRecoveryStage::ClearCore;
        case game::WorldMapStrategicAction::Repair:
            return region->owner == game::StrategicOwner::Infested &&
                   region->recoveryStage ==
                       game::StrategicRecoveryStage::Repair;
        case game::WorldMapStrategicAction::Reclaim:
            return region->owner == game::StrategicOwner::Infested &&
                   region->recoveryStage ==
                       game::StrategicRecoveryStage::Counterattack;
    }
    return false;
}

void WorldMapPanel::UpdateStrategicSelection() {
    for (StrategicRegionElements& region : m_strategicRegions) {
        if (region.row != nullptr) {
            region.row->SetClass("selected",
                                 region.id == m_selectedStrategicRegion);
        }
    }
}

void WorldMapPanel::UpdateStrategicActions() {
    if (m_strategicActions == nullptr || m_strategicSelected == nullptr) {
        return;
    }
    const game::StrategicMapRegion* region = m_state.strategic.FindRegion(
        m_selectedStrategicRegion);
    if (region == nullptr && m_state.strategic.enabled) {
        const game::StrategicMapRegion* fallback = nullptr;
        for (const game::StrategicMapRegion& candidate :
             m_state.strategic.regions) {
            if (candidate.warning.valid) {
                fallback = &candidate;
                break;
            }
        }
        if (fallback == nullptr) {
            for (const game::StrategicMapRegion& candidate :
                 m_state.strategic.regions) {
                if (candidate.siegePhase ==
                    game::StrategicSiegePhase::Active) {
                    fallback = &candidate;
                    break;
                }
            }
        }
        if (fallback == nullptr) {
            fallback = m_state.strategic.FindRegion(m_state.currentRegion);
        }
        if (fallback == nullptr && !m_state.strategic.regions.empty()) {
            fallback = &m_state.strategic.regions.front();
        }
        if (fallback != nullptr) {
            m_selectedStrategicRegion = fallback->id;
            region = fallback;
        }
    }
    SetDisplay(m_strategicActions, region != nullptr);
    if (region == nullptr) {
        SetText(m_strategicSelected, "选择一个战略区域");
        return;
    }
    SetText(m_strategicSelected,
            RegionName(m_state, region->id) + " · 选择行动");
    const auto setDisabled = [this](Rml::Element* button,
                                    game::WorldMapStrategicAction action) {
        if (button != nullptr) {
            button->SetClass("disabled", !IsStrategicActionEnabled(action));
        }
    };
    setDisabled(m_actionTravel, game::WorldMapStrategicAction::Travel);
    setDisabled(m_actionSupport, game::WorldMapStrategicAction::Support);
    setDisabled(m_actionDonate, game::WorldMapStrategicAction::Donate);
    setDisabled(m_actionClear, game::WorldMapStrategicAction::ClearCore);
    setDisabled(m_actionRepair, game::WorldMapStrategicAction::Repair);
    setDisabled(m_actionReclaim, game::WorldMapStrategicAction::Reclaim);
}

void WorldMapPanel::Show() {
    if (m_document != nullptr) {
        m_document->Show();
    }
    m_open = true;
}

void WorldMapPanel::Hide() {
    if (m_document != nullptr) {
        m_document->Hide();
    }
    m_open = false;
}

void WorldMapPanel::AddEventListener(
    Rml::Element* element, const char* event,
    std::function<void(Rml::Event&)> action) {
    if (element == nullptr) {
        return;
    }
    auto listener = std::make_unique<FunctionListener>(std::move(action));
    element->AddEventListener(event, listener.get());
    m_listeners.push_back({element, event, std::move(listener)});
}

void WorldMapPanel::AddDynamicEventListener(
    Rml::Element* element, const char* event,
    std::function<void(Rml::Event&)> action) {
    if (element == nullptr) {
        return;
    }
    auto listener = std::make_unique<FunctionListener>(std::move(action));
    element->AddEventListener(event, listener.get());
    m_dynamicListeners.push_back({element, event, std::move(listener)});
}

void WorldMapPanel::ClearLayer(Rml::Element* layer) {
    if (layer == nullptr) {
        return;
    }
    while (layer->GetNumChildren() > 0) {
        Rml::ElementPtr child = layer->RemoveChild(layer->GetChild(0));
        (void)child;
    }
}

Rml::Element* WorldMapPanel::MakeElement(const char* tag,
                                         const char* className,
                                         Rml::Element* parent) {
    if (m_document == nullptr || parent == nullptr) {
        return nullptr;
    }
    Rml::ElementPtr element = m_document->CreateElement(tag);
    if (className != nullptr && className[0] != '\0') {
        element->SetAttribute("class", className);
    }
    Rml::Element* raw = element.get();
    parent->AppendChild(std::move(element));
    return raw;
}

void WorldMapPanel::BuildStatic(const game::WorldMapState& state) {
    for (ListenerBinding& binding : m_dynamicListeners) {
        if (binding.element != nullptr) {
            binding.element->RemoveEventListener(binding.event,
                                                 binding.listener.get());
        }
    }
    m_dynamicListeners.clear();
    ClearLayer(m_regionLayer);
    ClearLayer(m_strategicRegionLayer);
    ClearLayer(m_roadLayer);
    ClearLayer(m_waterLayer);
    ClearLayer(m_fogLayer);
    ClearLayer(m_landmarkLayer);
    ClearLayer(m_settlementLayer);
    ClearLayer(m_poiLayer);
    ClearLayer(m_eventLayer);
    ClearLayer(m_routeLayer);
    ClearLayer(m_labelLayer);
    m_regions.clear();
    m_strategicRegions.clear();
    m_settlements.clear();
    m_landmarks.clear();
    m_pois.clear();
    m_events.clear();
    m_worldEvents.clear();

    for (const game::WorldMapRegion& region : state.regions) {
        RegionElements entry;
        entry.id = region.id;
        entry.box = MakeElement("div", "map-region", m_regionLayer);
        if (entry.box != nullptr) {
            entry.box->SetAttribute("data-region", region.id);
            entry.box->SetClass("biome-" + region.biome, true);
            Place(entry.box, region.x, region.y, region.width, region.height,
                  state.width, state.height);
        }
        entry.fog = MakeElement("div", "map-fog", m_fogLayer);
        if (entry.fog != nullptr) {
            Place(entry.fog, region.x, region.y, region.width, region.height,
                  state.width, state.height);
        }
        entry.label = MakeElement("div", "map-region-label", m_labelLayer);
        if (entry.label != nullptr) {
            entry.label->SetProperty("left", Percent(region.x + region.width / 2.0f,
                                                     state.width));
            entry.label->SetProperty("top", Percent(region.y + region.height / 2.0f,
                                                    state.height));
            entry.label->SetInnerRML(region.name);
        }
        m_regions.push_back(std::move(entry));
    }

    for (const game::StrategicMapRegion& region : state.strategic.regions) {
        const auto found = std::find_if(
            state.regions.begin(), state.regions.end(),
            [&](const game::WorldMapRegion& item) {
                return item.id == region.id;
            });
        if (found == state.regions.end()) {
            continue;
        }
        StrategicRegionElements entry;
        entry.id = region.id;
        entry.box =
            MakeElement("div", "map-strategic-region", m_strategicRegionLayer);
        if (entry.box != nullptr) {
            entry.box->SetAttribute("data-strategic-region", region.id);
            entry.box->SetClass(OwnerClass(region.owner), true);
            Place(entry.box, found->x, found->y, found->width,
                  found->height, state.width, state.height);
            entry.tag = MakeElement("div", "map-strategic-tag", entry.box);
            AddDynamicEventListener(
                entry.box, "click", [this, id = region.id](Rml::Event&) {
                    if (m_selectHandler) {
                        m_selectHandler(id);
                    }
                });
        }
        m_strategicRegions.push_back(std::move(entry));
    }

    for (const game::WorldMapWater& water : state.waters) {
        Rml::Element* element = MakeElement("div", "map-water", m_waterLayer);
        Place(element, water.x, water.y, water.width, water.height, state.width,
              state.height);
    }

    for (const game::WorldMapRoad& road : state.roads) {
        const float dx = road.x1 - road.x0;
        const float dy = road.y1 - road.y0;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (!(length > 0.0f)) {
            continue;
        }
        const int steps =
            std::clamp(static_cast<int>(std::ceil(length / 1.5f)), 1, 300);
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            Rml::Element* dot =
                MakeElement("div", "map-road-dot", m_roadLayer);
            if (dot == nullptr) {
                continue;
            }
            if (road.bridge) {
                dot->SetClass("bridge", true);
            }
            if (!road.vehiclePassable) {
                dot->SetClass("no-vehicle", true);
            }
            dot->SetProperty("left", Percent(road.x0 + dx * t, state.width));
            dot->SetProperty("top", Percent(road.y0 + dy * t, state.height));
        }
    }

    for (const game::WorldMapSettlement& settlement : state.settlements) {
        Rml::Element* marker =
            MakeElement("div", "map-settlement", m_settlementLayer);
        if (marker != nullptr) {
            marker->SetAttribute("data-settlement", settlement.id);
            marker->SetProperty("left", Percent(settlement.x, state.width));
            marker->SetProperty("top", Percent(settlement.y, state.height));
            Rml::Element* dot =
                MakeElement("div", "map-settlement-dot", marker);
            (void)dot;
        }
        Rml::Element* label =
            MakeElement("div", "map-settlement-label", m_labelLayer);
        if (label != nullptr) {
            label->SetProperty("left", Percent(settlement.x, state.width));
            label->SetProperty("top", Percent(settlement.y, state.height));
        }
        m_settlements.push_back({settlement.id, marker, label});
    }

    for (const game::WorldMapLandmark& landmark : state.landmarks) {
        Rml::Element* marker =
            MakeElement("div", "map-landmark", m_landmarkLayer);
        if (marker != nullptr) {
            marker->SetAttribute("data-landmark", landmark.id);
            marker->SetProperty("left", Percent(landmark.x, state.width));
            marker->SetProperty("top", Percent(landmark.y, state.height));
        }
        const bool hasPoiLabel =
            std::any_of(state.pois.begin(), state.pois.end(),
                        [&](const game::WorldMapPoi& poi) {
                            return poi.id == landmark.id;
                        });
        Rml::Element* label = nullptr;
        if (!hasPoiLabel && landmark.kind != "junction") {
            label = MakeElement("div", "map-landmark-label", m_labelLayer);
            if (label != nullptr) {
                label->SetProperty("left", Percent(landmark.x, state.width));
                label->SetProperty("top", Percent(landmark.y, state.height));
            }
        }
        m_landmarks.push_back({landmark.id, marker, label});
    }

    for (const game::WorldMapPoi& poi : state.pois) {
        Rml::Element* marker = MakeElement("div", "map-poi", m_poiLayer);
        if (marker != nullptr) {
            marker->SetAttribute("data-poi", poi.id);
            marker->SetProperty(
                "left", Percent(poi.staged ? poi.stageX : poi.x, state.width));
            marker->SetProperty(
                "top", Percent(poi.staged ? poi.stageY : poi.y, state.height));
            Rml::Element* dot = MakeElement("div", "map-poi-dot", marker);
            (void)dot;
            AddDynamicEventListener(marker, "click", [this, id = poi.id](Rml::Event&) {
                ZL_INFO("map", "地图标记点击：{}", id);
                if (m_selectHandler) {
                    m_selectHandler(id);
                }
            });
        }
        Rml::Element* label = MakeElement("div", "map-poi-label", m_labelLayer);
        if (label != nullptr) {
            label->SetProperty("left", Percent(poi.x, state.width));
            label->SetProperty("top", Percent(poi.y, state.height));
        }
        m_pois.push_back({poi.id, marker, label});
    }

    for (const game::WorldMapEventMarker& event : state.events) {
        Rml::Element* marker = MakeElement("div", "map-event", m_eventLayer);
        if (marker != nullptr) {
            marker->SetAttribute("data-event", event.id);
            marker->SetProperty("left", Percent(event.x, state.width));
            marker->SetProperty("top", Percent(event.y, state.height));
            Rml::Element* dot = MakeElement("div", "map-event-dot", marker);
            (void)dot;
        }
        Rml::Element* label = MakeElement("div", "map-event-label", m_labelLayer);
        if (label != nullptr) {
            label->SetProperty("left", Percent(event.x, state.width));
            label->SetProperty("top", Percent(event.y, state.height));
        }
        m_events.push_back({event.id, marker, label});
    }

    for (const game::WorldMapWorldEventMarker& event : state.worldEvents) {
        Rml::Element* marker =
            MakeElement("div", "map-world-event", m_eventLayer);
        if (marker != nullptr) {
            marker->SetAttribute("data-world-event", event.id);
            marker->SetProperty("left", Percent(event.x, state.width));
            marker->SetProperty("top", Percent(event.y, state.height));
            Rml::Element* dot =
                MakeElement("div", "map-world-event-dot", marker);
            (void)dot;
            AddDynamicEventListener(
                marker, "click", [this, id = event.id](Rml::Event&) {
                    if (m_selectHandler) {
                        m_selectHandler(id);
                    }
                });
        }
        Rml::Element* label =
            MakeElement("div", "map-world-event-label", m_labelLayer);
        if (label != nullptr) {
            label->SetProperty("left", Percent(event.x, state.width));
            label->SetProperty("top", Percent(event.y, state.height));
        }
        m_worldEvents.push_back({event.id, marker, label});
    }

    m_player = MakeElement("div", "map-player", m_poiLayer);
    if (m_player != nullptr) {
        Rml::Element* dot = MakeElement("div", "map-player-dot", m_player);
        (void)dot;
    }
    m_playerLabel = MakeElement("div", "map-player-label", m_labelLayer);
    m_objective = MakeElement("div", "map-objective", m_poiLayer);
    m_objectiveLabel = MakeElement("div", "map-objective-label", m_labelLayer);

    m_routeKey.clear();
    m_built = true;
}

void WorldMapPanel::BuildRoute(const game::WorldMapState& state) {
    ClearLayer(m_routeLayer);
    if (!state.route.valid || state.route.points.size() < 2u) {
        m_routeKey = "none";
        return;
    }
    for (std::size_t i = 1; i < state.route.points.size(); ++i) {
        const game::WorldMapPoint& a = state.route.points[i - 1];
        const game::WorldMapPoint& b = state.route.points[i];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (!(length > 0.0f)) {
            continue;
        }
        const int steps =
            std::clamp(static_cast<int>(std::ceil(length / 1.2f)), 1, 300);
        for (int step = 0; step <= steps; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(steps);
            Rml::Element* dot =
                MakeElement("div", "map-route-dot", m_routeLayer);
            if (dot == nullptr) {
                continue;
            }
            dot->SetProperty("left", Percent(a.x + dx * t, state.width));
            dot->SetProperty("top", Percent(a.y + dy * t, state.height));
        }
    }
    m_routeKey = RouteKey(state.route);
}

void WorldMapPanel::UpdateDynamic(const game::WorldMapState& state) {
    for (SettlementElements& settlement : m_settlements) {
        const auto found = std::find_if(
            state.settlements.begin(), state.settlements.end(),
            [&](const game::WorldMapSettlement& item) {
                return item.id == settlement.id;
            });
        if (found == state.settlements.end() || !found->discovered) {
            SetDisplay(settlement.marker, false);
            SetDisplay(settlement.label, false);
            continue;
        }
        SetDisplay(settlement.marker, true);
        SetDisplay(settlement.label, true);
        if (settlement.marker != nullptr) {
            settlement.marker->SetClass(
                "rep-hostile", found->reputationTier == "hostile");
            settlement.marker->SetClass("rep-wary",
                                        found->reputationTier == "wary");
            settlement.marker->SetClass(
                "rep-neutral", found->reputationTier.empty() ||
                                   found->reputationTier == "neutral");
            settlement.marker->SetClass("rep-friendly",
                                        found->reputationTier == "friendly");
            settlement.marker->SetClass("rep-trusted",
                                        found->reputationTier == "trusted");
            settlement.marker->SetClass("fallen", found->fallen);
            settlement.marker->SetClass("raid", found->raidActive);
        }
        if (settlement.label != nullptr) {
            std::string text = found->name;
            text += " · ";
            text += found->reputationName.empty() ? "中立"
                                                  : found->reputationName;
            if (found->fallen) {
                text += " · 已失守";
            } else if (found->raidActive) {
                text += " · 夜袭中";
            } else if (found->defenseEnabled) {
                text += " · 防守正常";
            } else {
                text += " · 无防御";
            }
            SetText(settlement.label, text);
            settlement.label->SetClass("fallen", found->fallen);
            settlement.label->SetClass("raid", found->raidActive);
        }
    }

    for (RegionElements& region : m_regions) {
        const auto found = std::find_if(
            state.regions.begin(), state.regions.end(),
            [&](const game::WorldMapRegion& item) { return item.id == region.id; });
        if (found == state.regions.end()) {
            continue;
        }
        SetDisplay(region.box, true);
        SetDisplay(region.fog, !found->discovered);
        if (region.label != nullptr) {
            SetDisplay(region.label, found->discovered);
            region.label->SetClass("fogged", !found->discovered);
            region.label->SetInnerRML(found->name);
            region.label->SetProperty(
                "left", Percent(found->x + found->width / 2.0f, state.width));
            region.label->SetProperty(
                "top", Percent(found->y + found->height / 2.0f, state.height));
        }
    }

    for (StrategicRegionElements& region : m_strategicRegions) {
        const game::StrategicMapRegion* strategic =
            state.strategic.FindRegion(region.id);
        const auto worldRegion = std::find_if(
            state.regions.begin(), state.regions.end(),
            [&](const game::WorldMapRegion& item) {
                return item.id == region.id;
            });
        const bool urgent =
            strategic != nullptr &&
            (strategic->warning.valid ||
             strategic->siegePhase == game::StrategicSiegePhase::Active);
        if (strategic == nullptr || worldRegion == state.regions.end() ||
            (!worldRegion->discovered && !urgent)) {
            SetDisplay(region.box, false);
            continue;
        }
        SetDisplay(region.box, true);
        if (region.box != nullptr) {
            region.box->SetClass("owner-human",
                                 strategic->owner ==
                                     game::StrategicOwner::Human);
            region.box->SetClass("owner-contested",
                                 strategic->owner ==
                                     game::StrategicOwner::Contested);
            region.box->SetClass("owner-infested",
                                 strategic->owner ==
                                     game::StrategicOwner::Infested);
            region.box->SetClass("owner-neutral",
                                 strategic->owner ==
                                     game::StrategicOwner::Neutral);
            region.box->SetClass(
                "warning", strategic->warning.valid);
            region.box->SetClass(
                "active", strategic->siegePhase ==
                              game::StrategicSiegePhase::Active);
        }
        if (region.tag != nullptr && strategic->warning.valid) {
            region.tag->SetInnerRML(
                "预警 " +
                std::to_string(
                    (strategic->warning.remainingMinutes + 59) / 60) +
                "h");
        } else if (region.tag != nullptr &&
                   strategic->siegePhase ==
                       game::StrategicSiegePhase::Active) {
            region.tag->SetInnerRML("围攻");
        }
    }

    for (LandmarkElements& landmark : m_landmarks) {
        const auto found = std::find_if(
            state.landmarks.begin(), state.landmarks.end(),
            [&](const game::WorldMapLandmark& item) {
                return item.id == landmark.id;
            });
        if (found == state.landmarks.end()) {
            SetDisplay(landmark.marker, false);
            SetDisplay(landmark.label, false);
            continue;
        }
        SetDisplay(landmark.marker, true);
        landmark.marker->SetProperty("left", Percent(found->x, state.width));
        landmark.marker->SetProperty("top", Percent(found->y, state.height));
        landmark.marker->SetClass("discovered", found->discovered);
        if (landmark.label != nullptr) {
            SetDisplay(landmark.label, found->discovered);
            landmark.label->SetInnerRML(found->name);
        }
    }

    std::vector<std::pair<float, std::string>> labelCandidates;
    for (const game::WorldMapPoi& poi : state.pois) {
        if (!poi.discovered) {
            continue;
        }
        const float dx = poi.x - state.playerX;
        const float dy = poi.y - state.playerY;
        labelCandidates.push_back({dx * dx + dy * dy, poi.id});
    }
    std::sort(labelCandidates.begin(), labelCandidates.end(),
              [](const auto& a, const auto& b) {
                  return a.first < b.first;
              });
    std::unordered_set<std::string> labeledPois;
    for (std::size_t i = 0;
         i < labelCandidates.size() && i < 3u; ++i) {
        labeledPois.insert(labelCandidates[i].second);
    }

    std::size_t poiIndex = 0;
    for (PoiElements& poi : m_pois) {
        const auto found = std::find_if(
            state.pois.begin(), state.pois.end(),
            [&](const game::WorldMapPoi& item) { return item.id == poi.id; });
        if (found == state.pois.end() || poi.marker == nullptr) {
            SetDisplay(poi.marker, false);
            SetDisplay(poi.label, false);
            continue;
        }
        SetDisplay(poi.marker, true);
        SetDisplay(poi.label, true);
        const float markerX = found->staged ? found->stageX : found->x;
        const float markerY = found->staged ? found->stageY : found->y;
        poi.marker->SetClass("discovered", found->discovered);
        poi.marker->SetClass("completed", found->completed);
        poi.marker->SetClass("disabled", found->disabled);
        poi.marker->SetClass("staged", found->staged);
        poi.marker->SetClass("stage-failed", found->stageFailed);
        poi.marker->SetClass("stage-choice", found->stageAwaitingChoice);
        poi.marker->SetProperty("left", Percent(markerX, state.width));
        poi.marker->SetProperty("top", Percent(markerY, state.height));
        const std::string label = game::WorldMapPoiLabel(*found);
        if (poi.label != nullptr) {
            SetDisplay(poi.label, found->discovered &&
                                    labeledPois.contains(found->id));
            poi.label->SetInnerRML(label);
            poi.label->SetClass("discovered", found->discovered);
            poi.label->SetClass("completed", found->completed);
            poi.label->SetClass("disabled", found->disabled);
            poi.label->SetClass("staged", found->staged);
            poi.label->SetClass("stage-failed", found->stageFailed);
            poi.label->SetClass("stage-choice", found->stageAwaitingChoice);
            poi.label->SetProperty("left", Percent(markerX, state.width));
            poi.label->SetProperty("top", Percent(markerY, state.height));
            poi.label->SetClass("on-right", markerX < state.width * 0.82f);
            const int labelOffset =
                -9 + static_cast<int>(poiIndex % 4u) * 18;
            poi.label->SetProperty("margin-top",
                                   std::to_string(labelOffset) + "px");
        }
        ++poiIndex;
    }

    for (EventElements& event : m_events) {
        const auto found = std::find_if(
            state.events.begin(), state.events.end(),
            [&](const game::WorldMapEventMarker& item) {
                return item.id == event.id;
            });
        if (found == state.events.end()) {
            SetDisplay(event.marker, false);
            SetDisplay(event.label, false);
            continue;
        }
        SetDisplay(event.marker, true);
        SetDisplay(event.label, false);
        event.marker->SetClass("active", found->active);
        event.marker->SetProperty("left", Percent(found->x, state.width));
        event.marker->SetProperty("top", Percent(found->y, state.height));
        if (event.label != nullptr) {
            event.label->SetInnerRML(found->kind + " · " + found->status);
            event.label->SetClass("active", found->active);
            event.label->SetProperty("left", Percent(found->x, state.width));
            event.label->SetProperty("top", Percent(found->y, state.height));
            event.label->SetClass("on-right", found->x < state.width * 0.82f);
        }
    }

    for (WorldEventElements& event : m_worldEvents) {
        const auto found = std::find_if(
            state.worldEvents.begin(), state.worldEvents.end(),
            [&](const game::WorldMapWorldEventMarker& item) {
                return item.id == event.id;
            });
        if (found == state.worldEvents.end()) {
            SetDisplay(event.marker, false);
            SetDisplay(event.label, false);
            continue;
        }
        SetDisplay(event.marker, true);
        SetDisplay(event.label, true);
        if (event.marker != nullptr) {
            event.marker->SetClass("active", found->active);
            event.marker->SetClass("warning", found->warning);
            event.marker->SetProperty("left",
                                      Percent(found->x, state.width));
            event.marker->SetProperty("top",
                                      Percent(found->y, state.height));
        }
        if (event.label != nullptr) {
            event.label->SetInnerRML(found->name + " · " + found->status);
            event.label->SetClass("active", found->active);
            event.label->SetClass("warning", found->warning);
            event.label->SetProperty("left",
                                     Percent(found->x, state.width));
            event.label->SetProperty("top",
                                     Percent(found->y, state.height));
            event.label->SetClass("on-right",
                                  found->x < state.width * 0.82f);
        }
    }

    if (m_player != nullptr) {
        SetDisplay(m_player, state.playerValid);
        if (state.playerValid) {
            m_player->SetProperty("left", Percent(state.playerX, state.width));
            m_player->SetProperty("top", Percent(state.playerY, state.height));
        }
    }
    if (m_playerLabel != nullptr) {
        SetDisplay(m_playerLabel, state.playerValid);
        if (state.playerValid) {
            m_playerLabel->SetInnerRML("你");
            m_playerLabel->SetProperty("left", Percent(state.playerX, state.width));
            m_playerLabel->SetProperty("top", Percent(state.playerY, state.height));
            m_playerLabel->SetClass("on-right", state.playerX < state.width * 0.82f);
        }
    }
    if (m_objective != nullptr) {
        SetDisplay(m_objective, state.objective.valid);
        if (state.objective.valid) {
            m_objective->SetProperty("left", Percent(state.objective.x, state.width));
            m_objective->SetProperty("top", Percent(state.objective.y, state.height));
        }
    }
    if (m_objectiveLabel != nullptr) {
        SetDisplay(m_objectiveLabel, false);
        if (state.objective.valid) {
            m_objectiveLabel->SetInnerRML("目标 · " + state.objective.title);
            m_objectiveLabel->SetProperty(
                "left", Percent(state.objective.x, state.width));
            m_objectiveLabel->SetProperty(
                "top", Percent(state.objective.y, state.height));
            m_objectiveLabel->SetClass(
                "on-right", state.objective.x < state.width * 0.82f);
        }
    }
}

void WorldMapPanel::UpdateStrategicSidebar(
    const game::WorldMapState& state) {
    if (m_strategicResources == nullptr || m_strategicAlert == nullptr ||
        m_strategicList == nullptr || m_strategicLog == nullptr) {
        return;
    }
    ClearLayer(m_strategicList);
    ClearLayer(m_strategicLog);

    if (!state.strategic.enabled) {
        SetText(m_strategicResources, "战略层未启用");
        SetText(m_strategicAlert, "无战略数据");
        m_strategicAlert->SetClass("clear", true);
        m_strategicAlert->SetClass("warning", false);
        SetText(m_strategicLog, "暂无战略事件");
        return;
    }

    const game::StrategicResources& resources = state.strategic.resources;
    SetText(m_strategicResources,
            "战略资源 · 人 " + std::to_string(resources.people) + " · 补 " +
                std::to_string(resources.supplies) + " · 弹 " +
                std::to_string(resources.ammo) + " · 油 " +
                std::to_string(resources.fuel));

    const game::StrategicMapRegion* alert = nullptr;
    for (const game::StrategicMapRegion& region : state.strategic.regions) {
        if (region.warning.valid &&
            (alert == nullptr ||
             region.warning.remainingMinutes <
                 alert->warning.remainingMinutes)) {
            alert = &region;
        }
    }
    if (alert != nullptr) {
        const double gameHoursPerSecond = std::clamp(
            static_cast<double>(state.travel.gameHoursPerRealSecond), 0.05,
            10.0);
        const int realSeconds = static_cast<int>(std::lround(
            static_cast<double>(alert->warning.remainingMinutes) / 60.0 /
            gameHoursPerSecond));
        SetText(m_strategicAlert,
                "最高预警 · " + RegionName(state, alert->id) + " · " +
                    WarningHours(alert->warning.remainingMinutes) + "（约 " +
                    std::to_string(std::max(0, realSeconds)) + " 秒）");
        m_strategicAlert->SetClass("clear", false);
        m_strategicAlert->SetClass("warning", true);
    } else {
        const auto active = std::find_if(
            state.strategic.regions.begin(), state.strategic.regions.end(),
            [](const game::StrategicMapRegion& region) {
                return region.siegePhase ==
                       game::StrategicSiegePhase::Active;
            });
        if (active != state.strategic.regions.end()) {
            SetText(m_strategicAlert,
                    "围攻进行中 · " + RegionName(state, active->id));
            m_strategicAlert->SetClass("clear", false);
            m_strategicAlert->SetClass("warning", true);
        } else {
            SetText(m_strategicAlert,
                    "暂无战略预警 · 关注压力与补给");
            m_strategicAlert->SetClass("clear", true);
            m_strategicAlert->SetClass("warning", false);
        }
    }

    std::vector<const game::StrategicMapRegion*> sorted;
    sorted.reserve(state.strategic.regions.size());
    for (const game::StrategicMapRegion& region : state.strategic.regions) {
        sorted.push_back(&region);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const game::StrategicMapRegion* a,
                 const game::StrategicMapRegion* b) {
                  const int aPriority =
                      a->warning.valid
                          ? 0
                          : (a->siegePhase ==
                                     game::StrategicSiegePhase::Active
                                 ? 1
                                 : 2);
                  const int bPriority =
                      b->warning.valid
                          ? 0
                          : (b->siegePhase ==
                                     game::StrategicSiegePhase::Active
                                 ? 1
                                 : 2);
                  if (aPriority != bPriority) {
                      return aPriority < bPriority;
                  }
                  if (a->warning.valid && b->warning.valid &&
                      a->warning.remainingMinutes !=
                          b->warning.remainingMinutes) {
                      return a->warning.remainingMinutes <
                             b->warning.remainingMinutes;
                  }
                  if (a->strategicValue != b->strategicValue) {
                      return a->strategicValue > b->strategicValue;
                  }
                  return a->id < b->id;
              });

    for (const game::StrategicMapRegion* region : sorted) {
        Rml::Element* row =
            MakeElement("div", "map-strategic-row", m_strategicList);
        if (row == nullptr) {
            continue;
        }
        row->SetAttribute("data-strategic-region", region->id);
        row->SetClass(OwnerClass(region->owner), true);
        row->SetClass("warning", region->warning.valid);
        for (StrategicRegionElements& entry : m_strategicRegions) {
            if (entry.id == region->id) {
                entry.row = row;
                break;
            }
        }

        Rml::Element* head =
            MakeElement("div", "map-strategic-head", row);
        MakeElement("span", "map-strategic-dot", head);
        Rml::Element* name =
            MakeElement("span", "map-strategic-name", head);
        SetText(name, RegionName(state, region->id));
        Rml::Element* owner =
            MakeElement("span", "map-strategic-owner", head);
        std::string ownerText = game::StrategicOwnerLabel(region->owner);
        if (region->owner == game::StrategicOwner::Contested) {
            ownerText += " · ";
            ownerText +=
                game::ContestedFactionLabel(region->contestedFaction);
        }
        SetText(owner, ownerText);
        Rml::Element* value =
            MakeElement("span", "map-strategic-value", head);
        SetText(value, "战略值 " + std::to_string(region->strategicValue));

        Rml::Element* stats =
            MakeElement("div", "map-strategic-stats", row);
        SetText(MakeElement("span", "stat-pressure", stats),
                "压力 " + std::to_string(static_cast<int>(
                              std::lround(region->pressure))));
        SetText(MakeElement("span", "stat-defense", stats),
                "防御 " + std::to_string(static_cast<int>(
                             std::lround(region->defense))));
        SetText(MakeElement("span", "stat-supply", stats),
                "补给 " + std::to_string(static_cast<int>(
                            std::lround(region->supply))));
        SetText(MakeElement("span", "stat-population", stats),
                "人口 " + std::to_string(region->population));
        SetText(MakeElement("span", "stat-garrison", stats),
                "守军 " + std::to_string(region->garrison));
        SetText(MakeElement("span", "stat-fortification", stats),
                "工事 " + std::to_string(region->fortification));

        std::string warning;
        if (region->warning.valid) {
            warning = "预警 · ";
            warning += WarningHours(region->warning.remainingMinutes);
            warning += " · 情报";
            warning += game::StrategicIntelName(region->warning.intelLevel);
            const int errorPercent =
                game::StrategicIntelErrorPercent(region->warning.intelLevel);
            if (errorPercent > 0) {
                warning += " ±" + std::to_string(errorPercent) + "%";
            }
            warning += " · 约 " +
                       std::to_string(region->warning.reportedEnemyStrength) +
                       " 敌";
            warning += " · 防御 " +
                       std::to_string(region->warning.defenseScore);
        } else if (region->siegePhase != game::StrategicSiegePhase::None) {
            warning = "状态 · ";
            warning += game::StrategicSiegePhaseLabel(region->siegePhase);
        } else if (region->recoveryStage !=
                   game::StrategicRecoveryStage::None) {
            warning = "反攻 · ";
            warning +=
                game::StrategicRecoveryStageLabel(region->recoveryStage);
            if (region->facilityDamage > 0) {
                warning += " · 设施损伤 " +
                           std::to_string(region->facilityDamage) + "%";
            }
            if (region->refugees > 0) {
                warning += " · 难民 " + std::to_string(region->refugees);
            }
        } else if (region->refugees > 0) {
            warning = "难民压力 · " + std::to_string(region->refugees) +
                      " 人 · 补给下降";
        } else if (region->owner == game::StrategicOwner::Contested) {
            warning = "争夺中 · ";
            warning +=
                game::ContestedFactionLabel(region->contestedFaction);
            warning += " · 守住后归人类，失守将沦陷";
        } else if (region->coreSafe) {
            warning = "核心安全区 · 不会被一次失败抹除";
        }
        if (!warning.empty()) {
            Rml::Element* line =
                MakeElement("div", "map-strategic-warning-line", row);
            SetText(line, warning);
        }
    }

    std::size_t logCount = 0;
    for (const game::StrategicMapEvent& event : state.strategic.events) {
        if (logCount >= 3u) {
            break;
        }
        Rml::Element* row =
            MakeElement("div", "map-strategic-log-row", m_strategicLog);
        if (row == nullptr) {
            continue;
        }
        row->SetClass(EventToneClass(event.kind), true);
        SetText(row, EventLine(state, event));
        ++logCount;
    }
    if (logCount == 0u) {
        SetText(MakeElement("div", "map-strategic-empty", m_strategicLog),
                "暂无战略事件");
    }
}

void WorldMapPanel::UpdateText() {
    const game::WorldMapState& state = m_state;
    if (m_document == nullptr) {
        return;
    }
    std::string subtitle =
        state.mapId + " · " + std::to_string(static_cast<int>(state.width)) +
        "×" + std::to_string(static_cast<int>(state.height)) + " 格";
    if (state.strategic.enabled) {
        subtitle += " · 战略区 " +
                    std::to_string(state.strategic.regions.size());
    }
    SetText(m_document->GetElementById("world-map-subtitle"), subtitle);
    std::string regionText = "当前区域：" + state.currentRegionName;
    if (!state.nearestLandmarkName.empty()) {
        regionText += " · 最近路标：" + state.nearestLandmarkName;
    }
    SetText(m_document->GetElementById("map-region"), regionText);
    char position[128];
    std::snprintf(position, sizeof(position), "位置 (%.0f, %.0f) · 已探索区域 %d/%d",
                  static_cast<double>(state.playerX),
                  static_cast<double>(state.playerY), state.discoveredRegionCount,
                  state.regionCount);
    SetText(m_document->GetElementById("map-position"), position);
    char progress[128];
    std::snprintf(progress, sizeof(progress), "已发现 POI %d/%d · 道路 %d 段",
                  state.discoveredPoiCount, state.poiCount,
                  static_cast<int>(state.roads.size()));
    SetText(m_document->GetElementById("map-progress"), progress);
    std::string settlementList = "聚落：";
    std::size_t settlementCount = 0;
    for (const game::WorldMapSettlement& settlement : state.settlements) {
        if (!settlement.discovered) {
            continue;
        }
        if (settlementCount > 0u) {
            settlementList += " · ";
        }
        settlementList += settlement.name + " " +
                          (settlement.reputationName.empty()
                               ? "中立"
                               : settlement.reputationName) +
                          "（" + std::to_string(settlement.reputation) + "）";
        if (settlement.fallen) {
            settlementList += "(失守)";
        } else if (settlement.raidActive) {
            settlementList += "(夜袭)";
        }
        ++settlementCount;
    }
    if (settlementCount == 0u) {
        settlementList += "未发现";
    }
    SetText(m_document->GetElementById("map-settlement-list"),
            settlementList);
    struct DiscoveredPoi {
        float distance = 0.0f;
        std::string name;
        std::string stage;
    };
    std::vector<DiscoveredPoi> discovered;
    for (const game::WorldMapPoi& poi : state.pois) {
        if (!poi.discovered) {
            continue;
        }
        discovered.push_back(
            {std::sqrt((poi.x - state.playerX) * (poi.x - state.playerX) +
                       (poi.y - state.playerY) * (poi.y - state.playerY)),
             poi.name, poi.staged ? poi.stageLabel : std::string()});
    }
    std::sort(discovered.begin(), discovered.end(),
              [](const DiscoveredPoi& a, const DiscoveredPoi& b) {
                  return a.distance < b.distance;
              });
    std::string poiList = "已发现地点：";
    if (discovered.empty()) {
        poiList += "无（靠近地点后揭示）";
    } else {
        const std::size_t count = std::min<std::size_t>(discovered.size(), 3);
        for (std::size_t i = 0; i < count; ++i) {
            if (i > 0) {
                poiList += " · ";
            }
            poiList += discovered[i].name;
            if (!discovered[i].stage.empty()) {
                poiList += " · " + discovered[i].stage;
            }
            poiList += " " +
                       std::to_string(static_cast<int>(
                           std::lround(discovered[i].distance))) +
                       "格";
        }
    }
    SetText(m_document->GetElementById("map-poi-list"), poiList);
    if (state.objective.valid) {
        SetText(m_document->GetElementById("map-objective"),
                "任务目标：" + state.objective.title + " · " +
                    std::to_string(static_cast<int>(std::lround(
                        std::sqrt((state.objective.x - state.playerX) *
                                      (state.objective.x - state.playerX) +
                                  (state.objective.y - state.playerY) *
                                      (state.objective.y - state.playerY))))) +
                    " 格");
    } else {
        SetText(m_document->GetElementById("map-objective"), "任务目标：无");
    }
    if (state.route.valid) {
        SetText(m_document->GetElementById("map-route"),
                "导航：" + state.route.label + " · 道路 " +
                    std::to_string(
                        static_cast<int>(std::lround(state.route.distance))) +
                    " 格 · 道路路线");
    } else {
        SetText(m_document->GetElementById("map-route"),
                "导航：未设置 · 点击 POI/战略区域规划路线");
    }

    Rml::Element* routeCost =
        m_document->GetElementById("map-route-cost");
    if (routeCost != nullptr) {
        routeCost->SetClass("ok", false);
        routeCost->SetClass("late", false);
    }
    if (state.route.valid && state.routeCost.valid) {
        const game::WorldMapRouteCost& cost = state.routeCost;
        char text[512];
        std::snprintf(
            text, sizeof(text),
            "含绕路 %.0f 格 · 步行 %s / %s · 载具 %s / %s · 需油 %.0f%%",
            static_cast<double>(cost.travelDistance),
            TravelSeconds(cost.walkSeconds).c_str(),
            TravelHours(cost.walkGameHours).c_str(),
            TravelSeconds(cost.vehicleSeconds).c_str(),
            TravelHours(cost.vehicleGameHours).c_str(),
            static_cast<double>(cost.vehicleFuel * 100.0f));
        std::string costText = text;
        if (!state.travel.vehicleAvailable) {
            costText += " · 当前无可用载具（载具成本为估算）";
        } else if (!cost.fuelEnough) {
            char fuel[64];
            std::snprintf(fuel, sizeof(fuel), " · 油量不足（油箱 %.0f%%）",
                          static_cast<double>(state.travel.currentFuel *
                                              100.0f));
            costText += fuel;
        } else {
            char fuel[64];
            std::snprintf(fuel, sizeof(fuel), " · 油箱 %.0f%%",
                          static_cast<double>(state.travel.currentFuel *
                                              100.0f));
            costText += fuel;
        }

        const game::StrategicMapRegion* target =
            state.strategic.FindRegion(state.route.targetRegion);
        if (target != nullptr) {
            const float tripHours =
                state.travel.vehicleAvailable && cost.fuelEnough
                    ? cost.vehicleGameHours
                    : cost.walkGameHours;
            if (target->warning.valid) {
                const float remainingHours =
                    static_cast<float>(
                        target->warning.remainingMinutes) /
                    60.0f;
                const float margin = remainingHours - tripHours;
                char warning[96];
                if (margin >= 0.0f) {
                    std::snprintf(warning, sizeof(warning),
                                  " · 可提前 %.1f 小时抵达",
                                  static_cast<double>(margin));
                    routeCost->SetClass("ok", true);
                } else {
                    std::snprintf(warning, sizeof(warning),
                                  " · 预计迟到 %.1f 小时",
                                  static_cast<double>(-margin));
                    routeCost->SetClass("late", true);
                }
                costText += warning;
            } else if (target->siegePhase ==
                       game::StrategicSiegePhase::Active) {
                costText += " · 目标区域围攻中";
                routeCost->SetClass("late", true);
            }
        }
        SetText(routeCost, costText);
    } else {
        SetText(routeCost, "");
    }
    SetText(m_document->GetElementById("map-hint"),
            "点击 POI/世界事件/战略区域导航 · 点击战略行选行动 · M/ESC 关闭 · 无单位编队");
}

void WorldMapPanel::SetState(const game::WorldMapState& state, bool open) {
    m_state = state;
    if (!open) {
        Hide();
        return;
    }
    Show();
    std::string staticKey =
        state.mapId + "|" +
        std::to_string(static_cast<int>(state.width)) + "x" +
        std::to_string(static_cast<int>(state.height)) + "|" +
        std::to_string(state.regions.size()) + "|" +
                            std::to_string(state.roads.size()) + "|" +
                            std::to_string(state.waters.size()) + "|" +
                            std::to_string(state.landmarks.size()) + "|" +
                            std::to_string(state.pois.size()) + "|" +
                            std::to_string(state.settlements.size()) + "|" +
                            std::to_string(state.events.size()) + "|" +
                            std::to_string(state.worldEvents.size()) + "|" +
                            std::to_string(state.strategic.regions.size()) +
                            "|" + (state.strategic.enabled ? "1" : "0");
    for (const game::WorldMapWorldEventMarker& event : state.worldEvents) {
        staticKey += "|we:" + event.id + ":" +
                     std::to_string(static_cast<int>(event.x)) + "," +
                     std::to_string(static_cast<int>(event.y));
    }
    if (!m_built || staticKey != m_staticKey) {
        BuildStatic(state);
        m_staticKey = std::move(staticKey);
    }
    if (m_routeKey != RouteKey(state.route)) {
        BuildRoute(state);
    }
    UpdateDynamic(state);
    const std::string strategicKey = StrategicSidebarKey(state);
    if (strategicKey != m_strategicKey) {
        UpdateStrategicSidebar(state);
        m_strategicKey = strategicKey;
    }
    UpdateStrategicActions();
    UpdateStrategicSelection();
    UpdateText();
}

}  // namespace zl::ui
