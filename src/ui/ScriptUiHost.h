#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/GameEvents.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
class Event;
class EventListener;
}  // namespace Rml

namespace zl::core {
class EventBus;
}

namespace zl::ui {

// 脚本 UI 文档宿主：只负责 RmlUi 文档生命周期、元素访问和输入事件，
// 不包含任何 HUD/背包/菜单业务语义。脚本身份（mod id/root）由 ScriptUiApi 绑定。
class ScriptUiHost {
public:
    explicit ScriptUiHost(Rml::Context& context);
    ~ScriptUiHost();

    ScriptUiHost(const ScriptUiHost&) = delete;
    ScriptUiHost& operator=(const ScriptUiHost&) = delete;

    void SetEventBus(core::EventBus* events);

    using OwnerToken = std::uint64_t;
    OwnerToken RegisterOwner(const std::string& owner);
    void ReleaseOwner(const std::string& owner, OwnerToken token);
    void ReleaseOwner(const std::string& owner);
    bool HasOwner(const std::string& owner) const;
    void SetGroupVisible(const std::string& group, bool visible);

    // path 相对 mod root；拒绝绝对路径、越界路径和非 .rml 文件。
    bool LoadDocument(const std::string& owner, const std::filesystem::path& root,
                      const std::string& documentId, const std::string& relativePath,
                      bool show, bool modal, const std::string& group);
    bool OpenDocument(const std::string& owner, const std::filesystem::path& root,
                      const std::string& documentId, const std::string& relativePath,
                      bool modal, const std::string& group);
    bool UnloadDocument(const std::string& owner, const std::string& documentId);
    void UnloadGroup(const std::string& owner, const std::string& group);
    void UnloadOwner(const std::string& owner);
    void UnloadAll();

    bool ShowDocument(const std::string& owner, const std::string& documentId, bool visible);
    bool SetModal(const std::string& owner, const std::string& documentId, bool modal);
    bool IsLoaded(const std::string& owner, const std::string& documentId) const;
    bool IsVisible(const std::string& owner, const std::string& documentId) const;
    bool IsInputCaptured() const;
    bool HasVisibleDocuments() const;

    bool SetText(const std::string& owner, const std::string& documentId,
                 const std::string& elementId, const std::string& text);
    bool SetAttribute(const std::string& owner, const std::string& documentId,
                      const std::string& elementId, const std::string& name,
                      const std::string& value);
    bool RemoveAttribute(const std::string& owner, const std::string& documentId,
                         const std::string& elementId, const std::string& name);
    bool SetClass(const std::string& owner, const std::string& documentId,
                  const std::string& elementId, const std::string& className, bool enabled);
    bool SetProperty(const std::string& owner, const std::string& documentId,
                     const std::string& elementId, const std::string& property,
                     const std::string& value);
    std::string GetValue(const std::string& owner, const std::string& documentId,
                         const std::string& elementId) const;
    bool SetValue(const std::string& owner, const std::string& documentId,
                  const std::string& elementId, const std::string& value);
    bool Focus(const std::string& owner, const std::string& documentId,
               const std::string& elementId);

    // UiSystem 每帧调用：BeginFrame 清输入消费标记，EndFrame 把事件排入 EventBus。
    void BeginFrame();
    void EndFrame();
    bool InputConsumedThisFrame() const { return m_inputConsumed; }

private:
    friend class ScriptUiDocumentListener;

    struct Document;
    struct Listener {
        std::string event;
        std::unique_ptr<Rml::EventListener> listener;
    };

    struct Document {
        std::string owner;
        std::string id;
        std::string group;
        Rml::ElementDocument* document = nullptr;
        bool visible = false;
        bool modal = false;
        std::vector<Listener> listeners;
    };

    Document* FindDocument(const std::string& owner, const std::string& documentId);
    const Document* FindDocument(const std::string& owner,
                                 const std::string& documentId) const;
    Rml::Element* FindElement(const std::string& owner, const std::string& documentId,
                              const std::string& elementId) const;
    static bool ResolvePath(const std::filesystem::path& root, const std::string& relativePath,
                            std::filesystem::path& out);
    void AttachListeners(Document& doc);
    void DetachListeners(Document& doc);
    void OnRmlEvent(const std::string& owner, const std::string& documentId, Rml::Event& event);
    void ApplyVisibility(Document& doc);
    void QueueEvent(core::UiEvent event);

    Rml::Context* m_context = nullptr;
    core::EventBus* m_events = nullptr;
    std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<Document>>>
        m_documents;
    std::unordered_map<std::string, OwnerToken> m_ownerTokens;
    OwnerToken m_nextOwnerToken = 1;
    std::vector<core::UiEvent> m_pendingEvents;
    bool m_inputConsumed = false;
};

}  // namespace zl::ui
