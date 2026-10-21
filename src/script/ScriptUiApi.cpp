#include "script/ScriptUiApi.h"

#include <utility>

#include "ui/ScriptUiHost.h"

namespace zl::script {

ScriptUiApi::ScriptUiApi(ui::ScriptUiHost* host, std::string owner,
                         std::filesystem::path root)
    : m_host(host), m_owner(std::move(owner)), m_root(std::move(root)) {
    if (m_host != nullptr) {
        m_ownerToken = m_host->RegisterOwner(m_owner);
    }
}

ScriptUiApi::~ScriptUiApi() {
    if (m_host != nullptr) {
        m_host->ReleaseOwner(m_owner, m_ownerToken);
    }
}

bool ScriptUiApi::LoadDocument(const std::string& documentId, const std::string& path,
                               bool show, bool modal, const std::string& group) const {
    return m_host != nullptr &&
           m_host->LoadDocument(m_owner, m_root, documentId, path, show, modal, group);
}

bool ScriptUiApi::OpenDocument(const std::string& documentId, const std::string& path,
                               bool modal, const std::string& group) const {
    return m_host != nullptr &&
           m_host->OpenDocument(m_owner, m_root, documentId, path, modal, group);
}

bool ScriptUiApi::CloseDocument(const std::string& documentId, bool unload) const {
    if (m_host == nullptr) {
        return false;
    }
    if (unload) {
        return m_host->UnloadDocument(m_owner, documentId);
    }
    if (!m_host->SetModal(m_owner, documentId, false)) {
        return false;
    }
    return m_host->ShowDocument(m_owner, documentId, false);
}

bool ScriptUiApi::UnloadDocument(const std::string& documentId) const {
    return m_host != nullptr && m_host->UnloadDocument(m_owner, documentId);
}

void ScriptUiApi::UnloadGroup(const std::string& group) const {
    if (m_host != nullptr) {
        m_host->UnloadGroup(m_owner, group);
    }
}

bool ScriptUiApi::Show(const std::string& documentId, bool visible) const {
    return m_host != nullptr && m_host->ShowDocument(m_owner, documentId, visible);
}

bool ScriptUiApi::IsLoaded(const std::string& documentId) const {
    return m_host != nullptr && m_host->IsLoaded(m_owner, documentId);
}

bool ScriptUiApi::IsVisible(const std::string& documentId) const {
    return m_host != nullptr && m_host->IsVisible(m_owner, documentId);
}

bool ScriptUiApi::SetModal(const std::string& documentId, bool modal) const {
    return m_host != nullptr && m_host->SetModal(m_owner, documentId, modal);
}

bool ScriptUiApi::IsInputCaptured() const {
    return m_host != nullptr && m_host->IsInputCaptured();
}

bool ScriptUiApi::SetText(const std::string& documentId, const std::string& elementId,
                          const std::string& text) const {
    return m_host != nullptr &&
           m_host->SetText(m_owner, documentId, elementId, text);
}

bool ScriptUiApi::SetAttribute(const std::string& documentId, const std::string& elementId,
                               const std::string& name, const std::string& value) const {
    return m_host != nullptr &&
           m_host->SetAttribute(m_owner, documentId, elementId, name, value);
}

bool ScriptUiApi::RemoveAttribute(const std::string& documentId,
                                  const std::string& elementId,
                                  const std::string& name) const {
    return m_host != nullptr &&
           m_host->RemoveAttribute(m_owner, documentId, elementId, name);
}

bool ScriptUiApi::SetClass(const std::string& documentId, const std::string& elementId,
                           const std::string& className, bool enabled) const {
    return m_host != nullptr &&
           m_host->SetClass(m_owner, documentId, elementId, className, enabled);
}

bool ScriptUiApi::SetProperty(const std::string& documentId, const std::string& elementId,
                              const std::string& property,
                              const std::string& value) const {
    return m_host != nullptr &&
           m_host->SetProperty(m_owner, documentId, elementId, property, value);
}

std::string ScriptUiApi::GetValue(const std::string& documentId,
                                  const std::string& elementId) const {
    return m_host != nullptr ? m_host->GetValue(m_owner, documentId, elementId)
                             : std::string();
}

bool ScriptUiApi::SetValue(const std::string& documentId, const std::string& elementId,
                           const std::string& value) const {
    return m_host != nullptr &&
           m_host->SetValue(m_owner, documentId, elementId, value);
}

bool ScriptUiApi::Focus(const std::string& documentId, const std::string& elementId) const {
    return m_host != nullptr && m_host->Focus(m_owner, documentId, elementId);
}

}  // namespace zl::script
