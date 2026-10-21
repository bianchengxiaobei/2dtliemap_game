#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace zl::ui {
class ScriptUiHost;
}

namespace zl::script {

// 每个 mod 独立绑定的 UI 门面：owner 与 root 由 ScriptUiApi 固化，
// 脚本只能操作自己 mod 根目录下的 RML 文档。
class ScriptUiApi {
public:
    ScriptUiApi(ui::ScriptUiHost* host, std::string owner, std::filesystem::path root);
    ~ScriptUiApi();

    ScriptUiApi(const ScriptUiApi&) = delete;
    ScriptUiApi& operator=(const ScriptUiApi&) = delete;

    bool LoadDocument(const std::string& documentId, const std::string& path, bool show = true,
                      bool modal = false, const std::string& group = {}) const;
    bool OpenDocument(const std::string& documentId, const std::string& path,
                      bool modal = true, const std::string& group = {}) const;
    bool CloseDocument(const std::string& documentId, bool unload = false) const;
    bool UnloadDocument(const std::string& documentId) const;
    void UnloadGroup(const std::string& group) const;

    bool Show(const std::string& documentId, bool visible = true) const;
    bool IsLoaded(const std::string& documentId) const;
    bool IsVisible(const std::string& documentId) const;
    bool SetModal(const std::string& documentId, bool modal) const;
    bool IsInputCaptured() const;

    bool SetText(const std::string& documentId, const std::string& elementId,
                 const std::string& text) const;
    bool SetAttribute(const std::string& documentId, const std::string& elementId,
                      const std::string& name, const std::string& value) const;
    bool RemoveAttribute(const std::string& documentId, const std::string& elementId,
                         const std::string& name) const;
    bool SetClass(const std::string& documentId, const std::string& elementId,
                  const std::string& className, bool enabled = true) const;
    bool SetProperty(const std::string& documentId, const std::string& elementId,
                     const std::string& property, const std::string& value) const;
    std::string GetValue(const std::string& documentId, const std::string& elementId) const;
    bool SetValue(const std::string& documentId, const std::string& elementId,
                  const std::string& value) const;
    bool Focus(const std::string& documentId, const std::string& elementId) const;

private:
    ui::ScriptUiHost* m_host = nullptr;
    std::string m_owner;
    std::filesystem::path m_root;
    std::uint64_t m_ownerToken = 0;
};

}  // namespace zl::script
