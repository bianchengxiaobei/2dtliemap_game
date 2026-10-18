#include "ui/InventoryPanel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <RmlUi/Core.h>

#include "core/Log.h"

namespace zl::ui {
namespace {

class EventFunctionListener final : public Rml::EventListener {
public:
    explicit EventFunctionListener(std::function<void(Rml::Event&)> action)
        : m_action(std::move(action)) {}

    void ProcessEvent(Rml::Event& event) override {
        if (m_action) {
            m_action(event);
        }
    }

private:
    std::function<void(Rml::Event&)> m_action;
};

bool IsDescendantOf(const Rml::Element* element, const Rml::Element* ancestor) {
    while (element != nullptr) {
        if (element == ancestor) {
            return true;
        }
        element = element->GetParentNode();
    }
    return false;
}

int ParseInt(const Rml::String& text, int fallback) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    return end != text.c_str() ? static_cast<int>(value) : fallback;
}

std::uint64_t ParseUInt64(const Rml::String& text) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    return end != text.c_str() ? static_cast<std::uint64_t>(value) : 0;
}

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

}  // namespace

InventoryPanel::InventoryPanel() = default;

InventoryPanel::~InventoryPanel() { Shutdown(); }

bool InventoryPanel::Init(Rml::Context& context, const std::string& path) {
    m_context = &context;
    m_document = context.LoadDocument(path);
    if (m_document == nullptr) {
        ZL_WARN("ui", "背包文档加载失败：{}", path);
        m_context = nullptr;
        return false;
    }

    m_overlay = m_document->GetElementById("inventory-overlay");
    m_menu = m_document->GetElementById("context-menu");
    m_weight = m_document->GetElementById("inv-weight");
    m_bagStacks = m_document->GetElementById("bag-stack-count");
    m_lootStacks = m_document->GetElementById("loot-stack-count");
    m_status = m_document->GetElementById("inv-status");
    m_contextButtons[0] = m_document->GetElementById("ctx-use");
    m_contextButtons[1] = m_document->GetElementById("ctx-equip");
    m_contextButtons[2] = m_document->GetElementById("ctx-unequip");
    m_contextButtons[3] = m_document->GetElementById("ctx-drop");
    m_contextButtons[4] = m_document->GetElementById("ctx-pickup");

    for (int i = 0; i < kInventoryBagSlots; ++i) {
        const std::string suffix = std::to_string(i);
        m_bagSlots[static_cast<std::size_t>(i)] =
            m_document->GetElementById("bag-slot-" + suffix);
        m_bagNames[static_cast<std::size_t>(i)] =
            m_document->GetElementById("bag-name-" + suffix);
        m_bagCounts[static_cast<std::size_t>(i)] =
            m_document->GetElementById("bag-count-" + suffix);
    }
    for (int i = 0; i < kInventoryEquipmentSlots; ++i) {
        const std::string suffix = std::to_string(i);
        m_equipmentSlots[static_cast<std::size_t>(i)] =
            m_document->GetElementById("equip-slot-" + suffix);
        m_equipmentNames[static_cast<std::size_t>(i)] =
            m_document->GetElementById("equip-name-" + suffix);
        m_equipmentCounts[static_cast<std::size_t>(i)] =
            m_document->GetElementById("equip-count-" + suffix);
    }
    for (int i = 0; i < kInventoryLootSlots; ++i) {
        const std::string suffix = std::to_string(i);
        m_lootSlots[static_cast<std::size_t>(i)] =
            m_document->GetElementById("loot-slot-" + suffix);
        m_lootNames[static_cast<std::size_t>(i)] =
            m_document->GetElementById("loot-name-" + suffix);
        m_lootCounts[static_cast<std::size_t>(i)] =
            m_document->GetElementById("loot-count-" + suffix);
        if (m_lootSlots[static_cast<std::size_t>(i)] == nullptr ||
            m_lootNames[static_cast<std::size_t>(i)] == nullptr ||
            m_lootCounts[static_cast<std::size_t>(i)] == nullptr) {
            ZL_WARN("ui", "背包附近地面槽位缺失：{}", i);
        }
    }

    if (m_overlay == nullptr || m_menu == nullptr) {
        ZL_WARN("ui", "背包文档缺少 overlay/context-menu");
    }

    AddClickListener("inventory-close", [this] { Hide(); });
    if (m_overlay != nullptr) {
        AddEventListener(m_overlay, "click",
                        [this](Rml::Event& event) { OnOverlayClick(event); });
    }
    if (m_document != nullptr) {
        AddEventListener(m_document, "mousedown",
                        [this](Rml::Event& event) { OnDocumentMouseDown(event); });
    }

    AddClickListener("ctx-use",
                     [this] { TriggerContextAction(InventoryActionType::Use); });
    AddClickListener("ctx-equip",
                     [this] { TriggerContextAction(InventoryActionType::Equip); });
    AddClickListener("ctx-unequip",
                     [this] { TriggerContextAction(InventoryActionType::Unequip); });
    AddClickListener("ctx-drop",
                     [this] { TriggerContextAction(InventoryActionType::Drop); });
    AddClickListener("ctx-pickup",
                     [this] { TriggerContextAction(InventoryActionType::Pickup); });

    auto addSlotEvents = [this](Rml::Element* slot) {
        if (slot == nullptr) {
            return;
        }
        AddEventListener(slot, "mousedown",
                        [this](Rml::Event& event) { OnSlotMouseDown(event); });
        AddEventListener(slot, "dragstart",
                        [this](Rml::Event& event) { OnDragStart(event); });
        AddEventListener(slot, "dragover",
                        [this](Rml::Event& event) { OnDragOver(event); });
        AddEventListener(slot, "dragout",
                        [this](Rml::Event& event) { OnDragOut(event); });
        AddEventListener(slot, "dragdrop",
                        [this](Rml::Event& event) { OnDragDrop(event); });
        AddEventListener(slot, "dragend",
                        [this](Rml::Event& event) { OnDragEnd(event); });
    };
    for (Rml::Element* slot : m_bagSlots) {
        addSlotEvents(slot);
    }
    for (Rml::Element* slot : m_equipmentSlots) {
        addSlotEvents(slot);
    }
    for (Rml::Element* slot : m_lootSlots) {
        addSlotEvents(slot);
    }

    HideContextMenu();
    m_document->Hide();
    m_open = false;
    return true;
}

void InventoryPanel::Shutdown() {
    for (ListenerBinding& binding : m_listeners) {
        if (binding.element != nullptr) {
            binding.element->RemoveEventListener(binding.event, binding.listener.get());
        }
    }
    m_listeners.clear();
    m_document = nullptr;
    m_context = nullptr;
    m_overlay = nullptr;
    m_menu = nullptr;
    m_weight = nullptr;
    m_bagStacks = nullptr;
    m_lootStacks = nullptr;
    m_status = nullptr;
    m_bagSlots.fill(nullptr);
    m_bagNames.fill(nullptr);
    m_bagCounts.fill(nullptr);
    m_equipmentSlots.fill(nullptr);
    m_equipmentNames.fill(nullptr);
    m_equipmentCounts.fill(nullptr);
    m_lootSlots.fill(nullptr);
    m_lootNames.fill(nullptr);
    m_lootCounts.fill(nullptr);
    m_contextButtons.fill(nullptr);
    m_actionHandler = nullptr;
    m_soundHandler = nullptr;
    m_hasState = false;
    m_state = InventoryState{};
    m_open = false;
    m_menuOpen = false;
    m_dragActive = false;
    m_dragSource = nullptr;
    m_dragHover = nullptr;
    m_dragIndex = -1;
    m_dragEntity = 0;
    m_contextIndex = -1;
    m_contextEntity = 0;
}

void InventoryPanel::SetActionHandler(InventoryActionHandler handler) {
    m_actionHandler = std::move(handler);
}

void InventoryPanel::SetSoundHandler(std::function<void()> handler) {
    m_soundHandler = std::move(handler);
}

void InventoryPanel::SetState(const InventoryState& state) {
    if (m_document == nullptr) {
        return;
    }
    const InventoryState previous = m_state;
    const bool first = !m_hasState;
    m_state = state;

    auto changedSlot = [&](InventoryArea area, int index) {
        if (first) {
            return true;
        }
        const InventorySlot& current = area == InventoryArea::Backpack
                                           ? state.bag[static_cast<std::size_t>(index)]
                                       : area == InventoryArea::Equipment
                                           ? state.equipment[static_cast<std::size_t>(index)]
                                           : state.loot[static_cast<std::size_t>(index)];
        const InventorySlot& old = area == InventoryArea::Backpack
                                       ? previous.bag[static_cast<std::size_t>(index)]
                                   : area == InventoryArea::Equipment
                                       ? previous.equipment[static_cast<std::size_t>(index)]
                                       : previous.loot[static_cast<std::size_t>(index)];
        return current.filled != old.filled || current.label != old.label ||
               current.count != old.count || current.usable != old.usable ||
               current.equippable != old.equippable ||
               current.entityId != old.entityId;
    };

    for (int i = 0; i < kInventoryBagSlots; ++i) {
        if (changedSlot(InventoryArea::Backpack, i)) {
            RefreshSlot(InventoryArea::Backpack, i, state.bag[static_cast<std::size_t>(i)]);
        }
    }
    for (int i = 0; i < kInventoryEquipmentSlots; ++i) {
        if (changedSlot(InventoryArea::Equipment, i)) {
            RefreshSlot(InventoryArea::Equipment, i,
                        state.equipment[static_cast<std::size_t>(i)]);
        }
    }
    for (int i = 0; i < kInventoryLootSlots; ++i) {
        if (changedSlot(InventoryArea::Loot, i)) {
            RefreshSlot(InventoryArea::Loot, i, state.loot[static_cast<std::size_t>(i)]);
        }
    }

    if (first || state.playerValid != previous.playerValid || state.weight != previous.weight ||
        state.capacity != previous.capacity || state.bagStacks != previous.bagStacks ||
        state.lootStacks != previous.lootStacks || state.status != previous.status ||
        state.statusTone != previous.statusTone) {
        RefreshStaticText(state);
    }
    m_hasState = true;

    if (m_menuOpen) {
        bool valid = false;
        if (m_contextArea == InventoryArea::Backpack &&
            m_contextIndex >= 0 && m_contextIndex < kInventoryBagSlots) {
            valid = state.bag[static_cast<std::size_t>(m_contextIndex)].filled;
        } else if (m_contextArea == InventoryArea::Equipment &&
                   m_contextIndex >= 0 && m_contextIndex < kInventoryEquipmentSlots) {
            valid = state.equipment[static_cast<std::size_t>(m_contextIndex)].filled;
        } else if (m_contextArea == InventoryArea::Loot && m_contextIndex >= 0 &&
                   m_contextIndex < kInventoryLootSlots) {
            valid = state.loot[static_cast<std::size_t>(m_contextIndex)].filled;
        }
        if (!valid) {
            HideContextMenu();
        }
    }
}

void InventoryPanel::RefreshSlot(InventoryArea area, int index, const InventorySlot& slot) {
    Rml::Element* element = nullptr;
    Rml::Element* name = nullptr;
    Rml::Element* count = nullptr;
    if (area == InventoryArea::Backpack) {
        element = m_bagSlots[static_cast<std::size_t>(index)];
        name = m_bagNames[static_cast<std::size_t>(index)];
        count = m_bagCounts[static_cast<std::size_t>(index)];
    } else if (area == InventoryArea::Equipment) {
        element = m_equipmentSlots[static_cast<std::size_t>(index)];
        name = m_equipmentNames[static_cast<std::size_t>(index)];
        count = m_equipmentCounts[static_cast<std::size_t>(index)];
    } else {
        element = m_lootSlots[static_cast<std::size_t>(index)];
        name = m_lootNames[static_cast<std::size_t>(index)];
        count = m_lootCounts[static_cast<std::size_t>(index)];
    }
    if (element == nullptr) {
        return;
    }
    element->SetClass("filled", slot.filled);
    element->SetClass("usable", slot.filled && slot.usable);
    element->SetClass("equippable", slot.filled && slot.equippable);
    element->SetAttribute("data-entity", std::to_string(slot.entityId));
    element->SetAttribute("data-count", std::to_string(slot.count));

    if (name != nullptr) {
        if (slot.filled) {
            SetText(name, slot.label);
        } else {
            SetText(name, area == InventoryArea::Loot ? "无" : "空");
        }
    }
    if (count != nullptr) {
        SetText(count, slot.filled && slot.count > 1 ? std::to_string(slot.count) : "");
    }
}

void InventoryPanel::RefreshStaticText(const InventoryState& state) {
    char weight[64];
    std::snprintf(weight, sizeof(weight), "%.1f / %.1f kg", state.weight, state.capacity);
    SetText(m_weight, state.playerValid ? weight : "无玩家");
    SetText(m_bagStacks, std::to_string(state.bagStacks) + " 堆");
    SetText(m_lootStacks, std::to_string(state.lootStacks) + " 堆");
    SetText(m_status, state.status);
    if (m_status != nullptr) {
        m_status->SetClassNames("inv-status status-" + state.statusTone);
    }
}

void InventoryPanel::Show() {
    if (m_document == nullptr) {
        return;
    }
    m_document->Show();
    m_document->PullToFront();
    m_open = true;
    HideContextMenu();
    ClearDragState();
}

void InventoryPanel::Hide() {
    if (m_context != nullptr) {
        if (Rml::Element* focus = m_context->GetFocusElement(); focus != nullptr) {
            focus->Blur();
        }
    }
    if (m_document != nullptr) {
        m_document->Hide();
    }
    m_open = false;
    HideContextMenu();
    ClearDragState();
}

void InventoryPanel::AddEventListener(Rml::Element* element, const char* event,
                                      std::function<void(Rml::Event&)> action) {
    if (element == nullptr) {
        return;
    }
    auto listener = std::make_unique<EventFunctionListener>(std::move(action));
    element->AddEventListener(event, listener.get());
    m_listeners.push_back(ListenerBinding{element, event, std::move(listener)});
}

void InventoryPanel::AddClickListener(const char* elementId, std::function<void()> action) {
    if (m_document == nullptr) {
        return;
    }
    Rml::Element* element = m_document->GetElementById(elementId);
    if (element == nullptr) {
        ZL_WARN("ui", "背包元素不存在：{}", elementId);
        return;
    }
    AddEventListener(element, "click",
                     [this, action = std::move(action)](Rml::Event&) {
                         if (m_soundHandler) {
                             m_soundHandler();
                         }
                         action();
                     });
}

void InventoryPanel::OnSlotMouseDown(Rml::Event& event) {
    const int button = event.GetParameter<int>("button", -1);
    if (button != 1) {
        if (button == 0) {
            HideContextMenu();
        }
        return;
    }
    Rml::Element* slot = FindSlot(event.GetTargetElement());
    if (slot == nullptr) {
        return;
    }
    const float mouseX = event.GetParameter<float>("mouse_x", 0.0f);
    const float mouseY = event.GetParameter<float>("mouse_y", 0.0f);
    OpenContextMenu(slot, mouseX, mouseY);
}

void InventoryPanel::OnDragStart(Rml::Event& event) {
    Rml::Element* slot = FindSlot(event.GetTargetElement());
    if (slot == nullptr || !slot->IsClassSet("filled")) {
        return;
    }
    HideContextMenu();
    ClearDragState();
    m_dragActive = true;
    m_dragSource = slot;
    m_dragArea = AreaOf(slot);
    m_dragIndex = IndexOf(slot);
    m_dragEntity = EntityOf(slot);
    slot->SetClass("is-dragging", true);
}

void InventoryPanel::OnDragOver(Rml::Event& event) {
    if (!m_dragActive) {
        return;
    }
    Rml::Element* slot = FindSlot(event.GetTargetElement());
    if (slot == nullptr || slot == m_dragSource) {
        return;
    }
    if (m_dragHover != slot) {
        if (m_dragHover != nullptr) {
            m_dragHover->SetClass("drop-target", false);
        }
        m_dragHover = slot;
        slot->SetClass("drop-target", true);
    }
}

void InventoryPanel::OnDragOut(Rml::Event& event) {
    Rml::Element* slot = FindSlot(event.GetTargetElement());
    if (slot != nullptr && slot == m_dragHover) {
        slot->SetClass("drop-target", false);
        m_dragHover = nullptr;
    }
}

void InventoryPanel::OnDragDrop(Rml::Event& event) {
    if (!m_dragActive) {
        return;
    }
    Rml::Element* slot = FindSlot(event.GetTargetElement());
    if (slot == nullptr) {
        ClearDragState();
        return;
    }
    InventoryAction action;
    action.type = InventoryActionType::Transfer;
    action.from = m_dragArea;
    action.fromIndex = m_dragIndex;
    action.fromEntityId = m_dragEntity;
    action.to = AreaOf(slot);
    action.toIndex = IndexOf(slot);
    const bool sameSlot = action.from == action.to && action.fromIndex == action.toIndex;
    ClearDragState();
    if (!sameSlot && m_actionHandler) {
        m_actionHandler(action);
    }
}

void InventoryPanel::OnDragEnd(Rml::Event& event) {
    (void)event;
    ClearDragState();
}

void InventoryPanel::ClearDragState() {
    if (m_dragSource != nullptr) {
        m_dragSource->SetClass("is-dragging", false);
    }
    if (m_dragHover != nullptr) {
        m_dragHover->SetClass("drop-target", false);
    }
    m_dragActive = false;
    m_dragSource = nullptr;
    m_dragHover = nullptr;
    m_dragArea = InventoryArea::Backpack;
    m_dragIndex = -1;
    m_dragEntity = 0;
}

void InventoryPanel::OnDocumentMouseDown(Rml::Event& event) {
    if (!m_menuOpen) {
        return;
    }
    const int button = event.GetParameter<int>("button", -1);
    if (button != 0) {
        return;
    }
    Rml::Element* target = event.GetTargetElement();
    if (target == nullptr || !IsDescendantOf(target, m_menu)) {
        HideContextMenu();
    }
}

void InventoryPanel::OnOverlayClick(Rml::Event& event) {
    if (m_menuOpen) {
        HideContextMenu();
        return;
    }
    if (event.GetTargetElement() == m_overlay) {
        Hide();
    }
}

void InventoryPanel::OpenContextMenu(Rml::Element* slot, float mouseX, float mouseY) {
    if (slot == nullptr || m_menu == nullptr) {
        return;
    }
    InventoryArea area = AreaOf(slot);
    const int index = IndexOf(slot);
    const InventorySlot* state = nullptr;
    if (area == InventoryArea::Backpack && index >= 0 && index < kInventoryBagSlots) {
        state = &m_state.bag[static_cast<std::size_t>(index)];
    } else if (area == InventoryArea::Equipment && index >= 0 &&
               index < kInventoryEquipmentSlots) {
        state = &m_state.equipment[static_cast<std::size_t>(index)];
    } else if (area == InventoryArea::Loot && index >= 0 && index < kInventoryLootSlots) {
        state = &m_state.loot[static_cast<std::size_t>(index)];
    }
    if (state == nullptr || !state->filled) {
        HideContextMenu();
        return;
    }

    m_contextArea = area;
    m_contextIndex = index;
    m_contextEntity = state->entityId;

    bool use = false;
    bool equip = false;
    bool unequip = false;
    bool drop = false;
    bool pickup = false;
    if (area == InventoryArea::Backpack) {
        use = state->usable;
        equip = state->equippable;
        drop = true;
    } else if (area == InventoryArea::Equipment) {
        unequip = true;
        drop = true;
    } else {
        pickup = true;
    }

    SetDisplay(m_contextButtons[0], use);
    SetDisplay(m_contextButtons[1], equip);
    SetDisplay(m_contextButtons[2], unequip);
    SetDisplay(m_contextButtons[3], drop);
    SetDisplay(m_contextButtons[4], pickup);

    const int visible = static_cast<int>(use) + static_cast<int>(equip) +
                        static_cast<int>(unequip) + static_cast<int>(drop) +
                        static_cast<int>(pickup);
    const float menuHeight = static_cast<float>(visible * 30 + 10);
    const Rml::Vector2i dimensions = m_context->GetDimensions();
    const float x = std::clamp(mouseX, 0.0f,
                               std::max(0.0f, static_cast<float>(dimensions.x) - 150.0f));
    const float y = std::clamp(mouseY, 0.0f,
                               std::max(0.0f, static_cast<float>(dimensions.y) - menuHeight));
    char left[32];
    char top[32];
    std::snprintf(left, sizeof(left), "%.0fpx", x);
    std::snprintf(top, sizeof(top), "%.0fpx", y);
    m_menu->SetProperty("left", left);
    m_menu->SetProperty("top", top);
    m_menu->SetProperty("display", "block");
    m_menuOpen = true;
}

void InventoryPanel::HideContextMenu() {
    if (m_menu != nullptr) {
        m_menu->SetProperty("display", "none");
    }
    m_menuOpen = false;
    m_contextIndex = -1;
    m_contextEntity = 0;
}

void InventoryPanel::TriggerContextAction(InventoryActionType type) {
    if (!m_menuOpen || !m_actionHandler) {
        HideContextMenu();
        return;
    }
    InventoryAction action;
    action.type = type;
    action.from = m_contextArea;
    action.fromIndex = m_contextIndex;
    action.fromEntityId = m_contextEntity;
    action.to = type == InventoryActionType::Drop    ? InventoryArea::Loot
                : type == InventoryActionType::Pickup ? InventoryArea::Backpack
                                                      : InventoryArea::Backpack;
    action.toIndex = m_contextIndex;
    HideContextMenu();
    m_actionHandler(action);
}

Rml::Element* InventoryPanel::FindSlot(Rml::Element* element) const {
    while (element != nullptr) {
        if (element->HasAttribute("data-area") && element->HasAttribute("data-index")) {
            return element;
        }
        element = element->GetParentNode();
    }
    return nullptr;
}

InventoryArea InventoryPanel::AreaOf(const Rml::Element* slot) const {
    if (slot == nullptr) {
        return InventoryArea::Backpack;
    }
    const Rml::String area = slot->GetAttribute<Rml::String>("data-area", "");
    if (area == "equipment") {
        return InventoryArea::Equipment;
    }
    if (area == "loot") {
        return InventoryArea::Loot;
    }
    return InventoryArea::Backpack;
}

int InventoryPanel::IndexOf(const Rml::Element* slot) const {
    if (slot == nullptr) {
        return -1;
    }
    return ParseInt(slot->GetAttribute<Rml::String>("data-index", "-1"), -1);
}

std::uint64_t InventoryPanel::EntityOf(const Rml::Element* slot) const {
    if (slot == nullptr) {
        return 0;
    }
    return ParseUInt64(slot->GetAttribute<Rml::String>("data-entity", "0"));
}

}  // namespace zl::ui
