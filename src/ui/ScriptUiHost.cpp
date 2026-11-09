#include "ui/ScriptUiHost.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>

#include "core/EventBus.h"
#include "core/Log.h"

namespace zl::ui {
namespace {

constexpr const char* kModule = "ui";

constexpr const char* kEventTypes[] = {
    "click", "dblclick", "mousedown", "mouseup", "keydown", "keyup",
    "change", "submit", "focus", "blur", "dragstart", "dragover",
    "dragout", "dragdrop", "dragend",
};

Rml::Element* FindIdentifiedElement(Rml::Element* element) {
    while (element != nullptr && element->GetId().empty()) {
        element = element->GetParentNode();
    }
    return element;
}

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

}  // namespace

class ScriptUiDocumentListener final : public Rml::EventListener {
public:
    ScriptUiDocumentListener(ScriptUiHost& host, std::string owner, std::string documentId)
        : m_host(host), m_owner(std::move(owner)), m_documentId(std::move(documentId)) {}

    void ProcessEvent(Rml::Event& event) override {
        m_host.OnRmlEvent(m_owner, m_documentId, event);
    }

private:
    ScriptUiHost& m_host;
    std::string m_owner;
    std::string m_documentId;
};

ScriptUiHost::ScriptUiHost(Rml::Context& context) : m_context(&context) {}

ScriptUiHost::~ScriptUiHost() { UnloadAll(); }

void ScriptUiHost::SetEventBus(core::EventBus* events) { m_events = events; }

ScriptUiHost::OwnerToken ScriptUiHost::RegisterOwner(const std::string& owner) {
    if (owner.empty()) {
        return 0;
    }
    const OwnerToken token = m_nextOwnerToken++;
    m_ownerTokens[owner] = token;
    return token;
}

void ScriptUiHost::ReleaseOwner(const std::string& owner, OwnerToken token) {
    const auto it = m_ownerTokens.find(owner);
    if (it == m_ownerTokens.end() || token == 0 || it->second != token) {
        return;  // 旧脚本实例析构不能清理新实例的 owner
    }
    m_ownerTokens.erase(it);
    UnloadOwner(owner);
}

void ScriptUiHost::ReleaseOwner(const std::string& owner) {
    if (m_ownerTokens.erase(owner) > 0) {
        UnloadOwner(owner);
    }
}

bool ScriptUiHost::HasOwner(const std::string& owner) const {
    return m_ownerTokens.find(owner) != m_ownerTokens.end();
}

void ScriptUiHost::SetGroupVisible(const std::string& group, bool visible) {
    for (auto& [owner, documents] : m_documents) {
        (void)owner;
        for (auto& [id, document] : documents) {
            (void)id;
            if (document->group != group) {
                continue;
            }
            document->visible = visible;
            ApplyVisibility(*document);
        }
    }
}

bool ScriptUiHost::ResolvePath(const std::filesystem::path& root,
                               const std::string& relativePath,
                               std::filesystem::path& out) {
    if (root.empty() || relativePath.empty()) {
        return false;
    }
    const std::filesystem::path relative(relativePath);
    if (relative.is_absolute()) {
        return false;
    }
    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            return false;
        }
    }

    std::error_code error;
    const std::filesystem::path base = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return false;
    }
    const std::filesystem::path full =
        std::filesystem::weakly_canonical(base / relative, error);
    if (error) {
        return false;
    }
    const std::filesystem::path inside = std::filesystem::relative(full, base, error);
    if (error || inside.empty()) {
        return false;
    }
    const auto first = inside.begin();
    if (first != inside.end() && *first == "..") {
        return false;
    }
    if (LowerExtension(full) != ".rml") {
        return false;
    }
    out = full;
    return true;
}

bool ScriptUiHost::LoadDocument(const std::string& owner, const std::filesystem::path& root,
                                const std::string& documentId,
                                const std::string& relativePath, bool show, bool modal,
                                const std::string& group) {
    if (m_context == nullptr || owner.empty() || documentId.empty()) {
        return false;
    }
    if (FindDocument(owner, documentId) != nullptr) {
        ZL_WARN(kModule, "脚本 UI 文档重复加载：[{}] {}", owner, documentId);
        return false;
    }

    std::filesystem::path path;
    if (!ResolvePath(root, relativePath, path)) {
        ZL_WARN(kModule, "脚本 UI 路径非法：[{}] {}（必须位于 mod 根目录内）", owner,
                relativePath);
        return false;
    }

    Rml::ElementDocument* document = m_context->LoadDocument(path.generic_string());
    if (document == nullptr) {
        ZL_WARN(kModule, "脚本 UI 文档加载失败：[{}] {}", owner, path.generic_string());
        return false;
    }

    auto entry = std::make_unique<Document>();
    entry->owner = owner;
    entry->id = documentId;
    entry->group = group;
    entry->document = document;
    entry->modal = modal;
    entry->visible = false;
    AttachListeners(*entry);

    Document& stored = *entry;
    m_documents[owner].insert_or_assign(documentId, std::move(entry));
    if (show) {
        ShowDocument(owner, documentId, true);
    } else {
        stored.document->Hide();
    }
    ZL_INFO(kModule, "脚本 UI 已加载：[{}] {} -> {}", owner, documentId,
            path.generic_string());
    return true;
}

bool ScriptUiHost::OpenDocument(const std::string& owner, const std::filesystem::path& root,
                                const std::string& documentId,
                                const std::string& relativePath, bool modal,
                                const std::string& group) {
    if (Document* existing = FindDocument(owner, documentId); existing != nullptr) {
        existing->modal = modal;
        return ShowDocument(owner, documentId, true);
    }
    return LoadDocument(owner, root, documentId, relativePath, true, modal, group);
}

bool ScriptUiHost::UnloadDocument(const std::string& owner, const std::string& documentId) {
    auto ownerIt = m_documents.find(owner);
    if (ownerIt == m_documents.end()) {
        return false;
    }
    auto documentIt = ownerIt->second.find(documentId);
    if (documentIt == ownerIt->second.end()) {
        return false;
    }
    Document& doc = *documentIt->second;
    DetachListeners(doc);
    if (doc.document != nullptr) {
        m_context->UnloadDocument(doc.document);
    }
    ownerIt->second.erase(documentIt);
    if (ownerIt->second.empty()) {
        m_documents.erase(ownerIt);
    }
    ZL_INFO(kModule, "脚本 UI 已卸载：[{}] {}", owner, documentId);
    return true;
}

void ScriptUiHost::UnloadGroup(const std::string& owner, const std::string& group) {
    auto ownerIt = m_documents.find(owner);
    if (ownerIt == m_documents.end()) {
        return;
    }
    std::vector<std::string> ids;
    for (const auto& [id, document] : ownerIt->second) {
        if (document->group == group) {
            ids.push_back(id);
        }
    }
    for (const std::string& id : ids) {
        UnloadDocument(owner, id);
    }
}

void ScriptUiHost::UnloadOwner(const std::string& owner) {
    auto ownerIt = m_documents.find(owner);
    if (ownerIt == m_documents.end()) {
        return;
    }
    for (const auto& [id, document] : ownerIt->second) {
        (void)id;
        if (document->document != nullptr) {
            DetachListeners(*document);
            m_context->UnloadDocument(document->document);
        }
    }
    m_documents.erase(ownerIt);
}

void ScriptUiHost::UnloadAll() {
    for (auto& [owner, documents] : m_documents) {
        (void)owner;
        for (auto& [id, document] : documents) {
            (void)id;
            if (document->document != nullptr) {
                DetachListeners(*document);
                m_context->UnloadDocument(document->document);
            }
        }
    }
    m_documents.clear();
    m_ownerTokens.clear();
    m_nextOwnerToken = 1;
    m_pendingEvents.clear();
    m_inputConsumed = false;
}

bool ScriptUiHost::ShowDocument(const std::string& owner, const std::string& documentId,
                                bool visible) {
    Document* document = FindDocument(owner, documentId);
    if (document == nullptr) {
        return false;
    }
    document->visible = visible;
    ApplyVisibility(*document);
    return true;
}

bool ScriptUiHost::SetModal(const std::string& owner, const std::string& documentId,
                            bool modal) {
    Document* document = FindDocument(owner, documentId);
    if (document == nullptr) {
        return false;
    }
    document->modal = modal;
    if (document->visible) {
        ApplyVisibility(*document);
    }
    return true;
}

bool ScriptUiHost::IsLoaded(const std::string& owner, const std::string& documentId) const {
    return FindDocument(owner, documentId) != nullptr;
}

bool ScriptUiHost::IsVisible(const std::string& owner, const std::string& documentId) const {
    const Document* document = FindDocument(owner, documentId);
    return document != nullptr && document->visible;
}

bool ScriptUiHost::IsInputCaptured() const {
    for (const auto& [owner, documents] : m_documents) {
        (void)owner;
        for (const auto& [id, document] : documents) {
            (void)id;
            if (document->visible && document->modal) {
                return true;
            }
        }
    }
    return false;
}

bool ScriptUiHost::HasVisibleDocuments() const {
    for (const auto& [owner, documents] : m_documents) {
        (void)owner;
        for (const auto& [id, document] : documents) {
            (void)id;
            if (document->visible) {
                return true;
            }
        }
    }
    return false;
}

bool ScriptUiHost::SetText(const std::string& owner, const std::string& documentId,
                           const std::string& elementId, const std::string& text) {
    Rml::Element* element = FindElement(owner, documentId, elementId);
    if (element == nullptr) {
        return false;
    }
    element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
    return true;
}

bool ScriptUiHost::SetAttribute(const std::string& owner, const std::string& documentId,
                                const std::string& elementId, const std::string& name,
                                const std::string& value) {
    Rml::Element* element = FindElement(owner, documentId, elementId);
    if (element == nullptr) {
        return false;
    }
    element->SetAttribute(name, value);
    return true;
}

bool ScriptUiHost::RemoveAttribute(const std::string& owner, const std::string& documentId,
                                   const std::string& elementId, const std::string& name) {
    Rml::Element* element = FindElement(owner, documentId, elementId);
    if (element == nullptr) {
        return false;
    }
    element->RemoveAttribute(name);
    return true;
}

bool ScriptUiHost::SetClass(const std::string& owner, const std::string& documentId,
                            const std::string& elementId, const std::string& className,
                            bool enabled) {
    Rml::Element* element = FindElement(owner, documentId, elementId);
    if (element == nullptr) {
        return false;
    }
    element->SetClass(className, enabled);
    return true;
}

bool ScriptUiHost::SetProperty(const std::string& owner, const std::string& documentId,
                               const std::string& elementId, const std::string& property,
                               const std::string& value) {
    Rml::Element* element = FindElement(owner, documentId, elementId);
    if (element == nullptr) {
        return false;
    }
    element->SetProperty(property, value);
    return true;
}

std::string ScriptUiHost::GetValue(const std::string& owner, const std::string& documentId,
                                   const std::string& elementId) const {
    const Rml::Element* element = FindElement(owner, documentId, elementId);
    if (element == nullptr) {
        return {};
    }
    if (const auto* control = dynamic_cast<const Rml::ElementFormControl*>(element);
        control != nullptr) {
        return control->GetValue();
    }
    return {};
}

bool ScriptUiHost::SetValue(const std::string& owner, const std::string& documentId,
                            const std::string& elementId, const std::string& value) {
    Rml::Element* element = FindElement(owner, documentId, elementId);
    if (element == nullptr) {
        return false;
    }
    if (auto* control = dynamic_cast<Rml::ElementFormControl*>(element); control != nullptr) {
        control->SetValue(value);
        return true;
    }
    return false;
}

bool ScriptUiHost::Focus(const std::string& owner, const std::string& documentId,
                         const std::string& elementId) {
    Rml::Element* element = FindElement(owner, documentId, elementId);
    return element != nullptr && element->Focus();
}

void ScriptUiHost::BeginFrame() { m_inputConsumed = false; }

void ScriptUiHost::EndFrame() {
    // UI 事件必须在暂停（timeScale=0、没有 tick）时也能送达脚本，因此这里同步派发；
    // 此时已离开 RmlUi Context::Update，脚本回调中修改文档是安全的。
    if (m_events != nullptr) {
        for (const core::UiEvent& event : m_pendingEvents) {
            m_events->Emit(core::kOnUiEvent, event);
        }
    }
    m_pendingEvents.clear();
}

ScriptUiHost::Document* ScriptUiHost::FindDocument(const std::string& owner,
                                                   const std::string& documentId) {
    auto ownerIt = m_documents.find(owner);
    if (ownerIt == m_documents.end()) {
        return nullptr;
    }
    auto documentIt = ownerIt->second.find(documentId);
    if (documentIt == ownerIt->second.end()) {
        return nullptr;
    }
    return documentIt->second.get();
}

const ScriptUiHost::Document* ScriptUiHost::FindDocument(
    const std::string& owner, const std::string& documentId) const {
    auto ownerIt = m_documents.find(owner);
    if (ownerIt == m_documents.end()) {
        return nullptr;
    }
    auto documentIt = ownerIt->second.find(documentId);
    if (documentIt == ownerIt->second.end()) {
        return nullptr;
    }
    return documentIt->second.get();
}

Rml::Element* ScriptUiHost::FindElement(const std::string& owner,
                                        const std::string& documentId,
                                        const std::string& elementId) const {
    const Document* document = FindDocument(owner, documentId);
    if (document == nullptr || document->document == nullptr) {
        return nullptr;
    }
    return document->document->GetElementById(elementId);
}

void ScriptUiHost::AttachListeners(Document& doc) {
    for (const char* eventType : kEventTypes) {
        auto listener = std::make_unique<ScriptUiDocumentListener>(*this, doc.owner, doc.id);
        doc.document->AddEventListener(eventType, listener.get());
        doc.listeners.push_back(Listener{eventType, std::move(listener)});
    }
}

void ScriptUiHost::DetachListeners(Document& doc) {
    for (Listener& listener : doc.listeners) {
        if (doc.document != nullptr) {
            doc.document->RemoveEventListener(listener.event, listener.listener.get());
        }
    }
    doc.listeners.clear();
}

void ScriptUiHost::OnRmlEvent(const std::string& owner, const std::string& documentId,
                              Rml::Event& event) {
    Rml::Element* target = event.GetTargetElement();
    if (target == nullptr) {
        return;
    }
    Rml::Element* identified = FindIdentifiedElement(target);
    core::UiEvent uiEvent;
    uiEvent.modId = owner;
    uiEvent.documentId = documentId;
    uiEvent.elementId = identified != nullptr ? identified->GetId() : std::string();
    uiEvent.type = event.GetType();
    uiEvent.value = event.GetParameter<Rml::String>("value", {});
    uiEvent.key = event.GetParameter<int>("key_identifier", 0);
    uiEvent.button = event.GetParameter<int>("button", -1);
    uiEvent.mouseX = event.GetParameter<float>("mouse_x", 0.0f);
    uiEvent.mouseY = event.GetParameter<float>("mouse_y", 0.0f);

    if (const auto* control = dynamic_cast<const Rml::ElementFormControl*>(target);
        control != nullptr) {
        uiEvent.value = control->GetValue();
    }

    if (uiEvent.type == "click" || uiEvent.type == "dblclick" ||
        uiEvent.type == "mousedown" || uiEvent.type == "mouseup") {
        // 全屏 HUD 的 body/文档背景不是可交互控件：点击世界时 RmlUi 会命中它们，
        // 不能把这类点击算作“UI 已消费”，否则单机点地移动/建造会被吞掉。
        const bool background =
            target->GetTagName() == "body" ||
            dynamic_cast<Rml::ElementDocument*>(target) != nullptr;
        m_inputConsumed = !background && !uiEvent.elementId.empty();
    }
    QueueEvent(std::move(uiEvent));
}

void ScriptUiHost::ApplyVisibility(Document& doc) {
    if (doc.document == nullptr) {
        return;
    }
    if (!doc.visible) {
        doc.document->Hide();
        return;
    }
    doc.document->Show(doc.modal ? Rml::ModalFlag::Modal : Rml::ModalFlag::None,
                       Rml::FocusFlag::Auto, Rml::ScrollFlag::None);
    doc.document->PullToFront();
}

void ScriptUiHost::QueueEvent(core::UiEvent event) {
    m_pendingEvents.push_back(std::move(event));
}

}  // namespace zl::ui
