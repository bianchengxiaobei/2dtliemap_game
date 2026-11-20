#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "ui/InventoryUi.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
class Event;
class EventListener;
}  // namespace Rml

namespace zl::ui {

class InventoryPanel {
public:
    InventoryPanel();
    ~InventoryPanel();

    InventoryPanel(const InventoryPanel&) = delete;
    InventoryPanel& operator=(const InventoryPanel&) = delete;

    bool Init(Rml::Context& context, const std::string& path);
    void Shutdown();

    void SetActionHandler(InventoryActionHandler handler);
    void SetSoundHandler(std::function<void()> handler);
    void SetState(const InventoryState& state);

    void Show();
    void Hide();
    bool IsOpen() const { return m_open; }

    Rml::ElementDocument* Document() const { return m_document; }

private:
    void AddEventListener(Rml::Element* element, const char* event,
                          std::function<void(Rml::Event&)> action);
    void AddClickListener(const char* elementId, std::function<void()> action);

    void RefreshSlot(InventoryArea area, int index, const InventorySlot& slot);
    void RefreshStaticText(const InventoryState& state);

    void OnSlotMouseDown(Rml::Event& event);
    void OnDragStart(Rml::Event& event);
    void OnDragOver(Rml::Event& event);
    void OnDragOut(Rml::Event& event);
    void OnDragDrop(Rml::Event& event);
    void OnDragEnd(Rml::Event& event);
    void OnDocumentMouseDown(Rml::Event& event);
    void OnOverlayClick(Rml::Event& event);
    void ClearDragState();

    void OpenContextMenu(Rml::Element* slot, float mouseX, float mouseY);
    void HideContextMenu();
    void TriggerContextAction(InventoryActionType type);
    void TriggerStorageAction(InventoryActionType type);

    Rml::Element* FindSlot(Rml::Element* element) const;
    InventoryArea AreaOf(const Rml::Element* slot) const;
    int IndexOf(const Rml::Element* slot) const;
    std::uint64_t EntityOf(const Rml::Element* slot) const;

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::Element* m_overlay = nullptr;
    Rml::Element* m_menu = nullptr;
    Rml::Element* m_weight = nullptr;
    Rml::Element* m_bagStacks = nullptr;
    Rml::Element* m_lootTitle = nullptr;
    Rml::Element* m_lootStacks = nullptr;
    Rml::Element* m_storageActions = nullptr;
    Rml::Element* m_storageVehicleMods = nullptr;
    Rml::Element* m_storageStoreAll = nullptr;
    Rml::Element* m_storageTakeAll = nullptr;
    Rml::Element* m_storageSort = nullptr;
    Rml::Element* m_status = nullptr;
    std::array<Rml::Element*, kInventoryBagSlots> m_bagSlots{};
    std::array<Rml::Element*, kInventoryBagSlots> m_bagNames{};
    std::array<Rml::Element*, kInventoryBagSlots> m_bagCounts{};
    std::array<Rml::Element*, kInventoryBagSlots> m_bagThrows{};
    std::array<Rml::Element*, kInventoryEquipmentSlots> m_equipmentSlots{};
    std::array<Rml::Element*, kInventoryEquipmentSlots> m_equipmentNames{};
    std::array<Rml::Element*, kInventoryEquipmentSlots> m_equipmentCounts{};
    std::array<Rml::Element*, kInventoryEquipmentSlots> m_equipmentThrows{};
    std::array<Rml::Element*, kInventoryLootSlots> m_lootSlots{};
    std::array<Rml::Element*, kInventoryLootSlots> m_lootNames{};
    std::array<Rml::Element*, kInventoryLootSlots> m_lootCounts{};
    std::array<Rml::Element*, kInventoryLootSlots> m_lootThrows{};
    std::array<Rml::Element*, 6> m_contextButtons{};
    struct ListenerBinding {
        Rml::Element* element = nullptr;
        std::string event;
        std::unique_ptr<Rml::EventListener> listener;
    };
    std::vector<ListenerBinding> m_listeners;

    InventoryActionHandler m_actionHandler;
    std::function<void()> m_soundHandler;
    InventoryState m_state;
    bool m_hasState = false;
    bool m_open = false;
    bool m_menuOpen = false;
    bool m_dragActive = false;
    InventoryArea m_dragArea = InventoryArea::Backpack;
    int m_dragIndex = -1;
    std::uint64_t m_dragEntity = 0;
    Rml::Element* m_dragSource = nullptr;
    Rml::Element* m_dragHover = nullptr;
    InventoryArea m_contextArea = InventoryArea::Backpack;
    int m_contextIndex = -1;
    std::uint64_t m_contextEntity = 0;
};

}  // namespace zl::ui
