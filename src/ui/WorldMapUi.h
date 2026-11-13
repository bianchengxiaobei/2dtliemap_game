#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/WorldMap.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
class Event;
class EventListener;
}  // namespace Rml

namespace zl::ui {

class WorldMapPanel {
public:
    WorldMapPanel();
    ~WorldMapPanel();

    WorldMapPanel(const WorldMapPanel&) = delete;
    WorldMapPanel& operator=(const WorldMapPanel&) = delete;

    bool Init(Rml::Context& context, const std::string& path);
    void Shutdown();

    void SetState(const game::WorldMapState& state, bool open);
    void SetCloseHandler(std::function<void()> handler);
    void SetSelectHandler(std::function<void(const std::string&)> handler);
    void SetStrategicActionHandler(
        std::function<void(const std::string&,
                           game::WorldMapStrategicAction)> handler);

    void Show();
    void Hide();
    bool IsOpen() const { return m_open; }
    Rml::ElementDocument* Document() const { return m_document; }

private:
    void AddEventListener(Rml::Element* element, const char* event,
                          std::function<void(Rml::Event&)> action);
    void AddDynamicEventListener(Rml::Element* element, const char* event,
                                 std::function<void(Rml::Event&)> action);
    void ClearLayer(Rml::Element* layer);
    Rml::Element* MakeElement(const char* tag, const char* className,
                              Rml::Element* parent);
    void BuildStatic(const game::WorldMapState& state);
    void BuildRoute(const game::WorldMapState& state);
    void UpdateDynamic(const game::WorldMapState& state);
    void UpdateStrategicSidebar(const game::WorldMapState& state);
    void UpdateStrategicSelection();
    void UpdateStrategicActions();
    void SelectStrategicRegion(const std::string& regionId);
    bool IsStrategicActionEnabled(game::WorldMapStrategicAction action) const;
    void UpdateText();

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::Element* m_overlay = nullptr;
    Rml::Element* m_regionLayer = nullptr;
    Rml::Element* m_strategicRegionLayer = nullptr;
    Rml::Element* m_roadLayer = nullptr;
    Rml::Element* m_waterLayer = nullptr;
    Rml::Element* m_fogLayer = nullptr;
    Rml::Element* m_poiLayer = nullptr;
    Rml::Element* m_settlementLayer = nullptr;
    Rml::Element* m_landmarkLayer = nullptr;
    Rml::Element* m_eventLayer = nullptr;
    Rml::Element* m_routeLayer = nullptr;
    Rml::Element* m_labelLayer = nullptr;
    Rml::Element* m_player = nullptr;
    Rml::Element* m_playerLabel = nullptr;
    Rml::Element* m_objective = nullptr;
    Rml::Element* m_objectiveLabel = nullptr;
    Rml::Element* m_settlementList = nullptr;
    Rml::Element* m_strategicResources = nullptr;
    Rml::Element* m_strategicAlert = nullptr;
    Rml::Element* m_strategicList = nullptr;
    Rml::Element* m_strategicLog = nullptr;
    Rml::Element* m_strategicActions = nullptr;
    Rml::Element* m_strategicSelected = nullptr;
    Rml::Element* m_actionTravel = nullptr;
    Rml::Element* m_actionSupport = nullptr;
    Rml::Element* m_actionDonate = nullptr;
    Rml::Element* m_actionClear = nullptr;
    Rml::Element* m_actionRepair = nullptr;
    Rml::Element* m_actionReclaim = nullptr;

    struct RegionElements {
        std::string id;
        Rml::Element* box = nullptr;
        Rml::Element* fog = nullptr;
        Rml::Element* label = nullptr;
    };
    struct StrategicRegionElements {
        std::string id;
        Rml::Element* box = nullptr;
        Rml::Element* tag = nullptr;
        Rml::Element* row = nullptr;
    };
    struct SettlementElements {
        std::string id;
        Rml::Element* marker = nullptr;
        Rml::Element* label = nullptr;
    };
    struct PoiElements {
        std::string id;
        Rml::Element* marker = nullptr;
        Rml::Element* label = nullptr;
    };
    struct LandmarkElements {
        std::string id;
        Rml::Element* marker = nullptr;
        Rml::Element* label = nullptr;
    };
    struct EventElements {
        std::string id;
        Rml::Element* marker = nullptr;
        Rml::Element* label = nullptr;
    };
    struct WorldEventElements {
        std::string id;
        Rml::Element* marker = nullptr;
        Rml::Element* label = nullptr;
    };
    std::vector<RegionElements> m_regions;
    std::vector<StrategicRegionElements> m_strategicRegions;
    std::vector<SettlementElements> m_settlements;
    std::vector<PoiElements> m_pois;
    std::vector<LandmarkElements> m_landmarks;
    std::vector<EventElements> m_events;
    std::vector<WorldEventElements> m_worldEvents;

    struct ListenerBinding {
        Rml::Element* element = nullptr;
        std::string event;
        std::unique_ptr<Rml::EventListener> listener;
    };
    std::vector<ListenerBinding> m_listeners;
    std::vector<ListenerBinding> m_dynamicListeners;

    std::function<void()> m_closeHandler;
    std::function<void(const std::string&)> m_selectHandler;
    std::function<void(const std::string&, game::WorldMapStrategicAction)>
        m_strategicActionHandler;
    game::WorldMapState m_state;
    std::string m_selectedStrategicRegion;
    std::string m_staticKey;
    std::string m_routeKey;
    std::string m_strategicKey;
    bool m_built = false;
    bool m_open = false;
};

}  // namespace zl::ui
