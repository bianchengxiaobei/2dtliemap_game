#include "script/ScriptManager.h"

#include <algorithm>
#include <utility>

#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "core/Log.h"

namespace zl::script {
namespace {

constexpr const char* kModule = "script";

}  // namespace

ScriptManager::~ScriptManager() {
    ShutdownAll();
}

void ScriptManager::RegisterRuntime(std::unique_ptr<IScriptRuntime> runtime) {
    if (runtime == nullptr) {
        return;
    }
    const std::string_view name = runtime->Name();
    if (FindEntry(name) != nullptr) {
        ZL_ERROR(kModule, "脚本后端重复注册：{}（忽略）", name);
        return;
    }
    ZL_INFO(kModule, "注册脚本后端：{}（共 {} 个）", name, m_runtimes.size() + 1);
    m_runtimes.push_back(RuntimeEntry{std::move(runtime), false});
}

IScriptRuntime* ScriptManager::FindRuntime(std::string_view name) {
    RuntimeEntry* entry = FindEntry(name);
    return entry != nullptr ? entry->runtime.get() : nullptr;
}

bool ScriptManager::InitializeAll() {
    bool allOk = true;
    for (RuntimeEntry& entry : m_runtimes) {
        if (entry.initialized) {
            continue;
        }
        std::string error;
        if (entry.runtime->Initialize(error)) {
            entry.initialized = true;
            ZL_INFO(kModule, "脚本后端 {} 初始化完成", entry.runtime->Name());
        } else {
            allOk = false;
            ZL_ERROR(kModule, "脚本后端 {} 初始化失败：{}（该后端不可用）", entry.runtime->Name(),
                     error);
        }
    }
    return allOk;
}

void ScriptManager::ShutdownAll() {
    for (auto it = m_runtimes.rbegin(); it != m_runtimes.rend(); ++it) {
        if (!it->initialized) {
            continue;
        }
        it->runtime->Shutdown();
        it->initialized = false;
        ZL_INFO(kModule, "脚本后端 {} 已关闭", it->runtime->Name());
    }
    m_mods.clear();
    m_nextLoadSeq = 0;
}

bool ScriptManager::LoadMod(const ModInfo& mod, std::string& errorOut) {
    errorOut.clear();
    if (mod.id.empty()) {
        errorOut = "mod id 不能为空";
        ZL_ERROR(kModule, "mod 加载失败：{}", errorOut);
        return false;
    }
    if (IsModLoaded(mod.id)) {
        errorOut = "mod 已加载：" + mod.id;
        ZL_ERROR(kModule, "mod 加载失败：{}", errorOut);
        return false;
    }

    RuntimeEntry* entry = FindEntry(mod.backend);
    if (entry == nullptr) {
        errorOut = "未注册的脚本后端：" + mod.backend + "（mod " + mod.id + "）";
        ZL_ERROR(kModule, "mod 加载失败 [{}]：{}", mod.id, errorOut);
        return false;
    }
    if (!entry->initialized) {
        errorOut = "脚本后端 " + mod.backend + " 未就绪（mod " + mod.id + "，初始化失败或被关闭）";
        ZL_ERROR(kModule, "mod 加载失败 [{}]：{}", mod.id, errorOut);
        return false;
    }

    std::string runtimeError;
    if (!entry->runtime->LoadMod(mod, runtimeError)) {
        errorOut = runtimeError;
        ZL_ERROR(kModule, "mod 加载失败 [{}]：{}", mod.id, errorOut);
        return false;
    }

    ModRecord record;
    record.info = mod;
    record.runtime = entry->runtime.get();
    record.loadSeq = m_nextLoadSeq++;
    m_mods.push_back(std::move(record));
    std::stable_sort(m_mods.begin(), m_mods.end(),
                     [](const ModRecord& a, const ModRecord& b) {
                         if (a.info.loadOrder != b.info.loadOrder) {
                             return a.info.loadOrder < b.info.loadOrder;
                         }
                         return a.loadSeq < b.loadSeq;
                     });
    ZL_INFO(kModule, "mod 已加载：{} v{}（backend={}；共 {} 个）", mod.id, mod.version, mod.backend,
            m_mods.size());
    return true;
}

bool ScriptManager::ReloadMod(std::string_view modId, std::string& errorOut) {
    errorOut.clear();
    ModRecord* record = FindMod(modId);
    if (record == nullptr) {
        errorOut = "mod 未加载：" + std::string(modId);
        return false;
    }

    std::string runtimeError;
    if (!record->runtime->ReloadMod(record->info.id, runtimeError)) {
        errorOut = runtimeError;
        DisableMod(*record, "重载失败：" + runtimeError);  // 简单版策略（docs/03 §8）
        return false;
    }

    if (record->disabled) {
        record->disabled = false;
        record->disableReason.clear();
        ZL_INFO(kModule, "mod 重载成功，已解除禁用：[{}]", record->info.id);
    } else {
        ZL_INFO(kModule, "mod 重载成功：[{}]", record->info.id);
    }
    // M7-T4：通知脚本侧 OnModReload（排队派发：避免在事件派发内重载时的重入）
    if (m_events != nullptr) {
        m_events->EmitQueued(core::kOnModReload, record->info.id);
    }
    return true;
}

void ScriptManager::UnloadMod(std::string_view modId) {
    const auto it = std::find_if(m_mods.begin(), m_mods.end(), [modId](const ModRecord& record) {
        return record.info.id == modId;
    });
    if (it == m_mods.end()) {
        ZL_WARN(kModule, "卸载未加载的 mod：{}", modId);
        return;
    }
    it->runtime->UnloadMod(it->info.id);
    ZL_INFO(kModule, "mod 已卸载：{}", it->info.id);
    m_mods.erase(it);
}

bool ScriptManager::IsModLoaded(std::string_view modId) const {
    return FindMod(modId) != nullptr;
}

std::vector<std::string> ScriptManager::LoadedModIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_mods.size());
    for (const ModRecord& record : m_mods) {
        ids.push_back(record.info.id);
    }
    return ids;
}

void ScriptManager::DispatchEvent(const core::ScriptEvent& ev) {
    for (ModRecord& record : m_mods) {
        if (record.disabled) {
            continue;
        }
        const std::vector<ModError> errors = record.runtime->DispatchEventToMod(record.info.id, ev);
        if (errors.empty()) {
            continue;
        }
        for (const ModError& error : errors) {
            ZL_ERROR(kModule, "mod [{}] 脚本错误（事件 {}）：{}", record.info.id, ev.name,
                     error.message);
        }
        DisableMod(record, errors.front().message);
    }
}

void ScriptManager::SetEventBus(core::EventBus* events) {
    m_events = events;
}

bool ScriptManager::IsModDisabled(std::string_view modId) const {
    const ModRecord* record = FindMod(modId);
    return record != nullptr && record->disabled;
}

std::size_t ScriptManager::DisabledModCount() const {
    return static_cast<std::size_t>(
        std::count_if(m_mods.begin(), m_mods.end(),
                      [](const ModRecord& record) { return record.disabled; }));
}

ScriptManager::RuntimeEntry* ScriptManager::FindEntry(std::string_view name) {
    const auto it = std::find_if(m_runtimes.begin(), m_runtimes.end(),
                                 [name](const RuntimeEntry& entry) {
                                     return name == entry.runtime->Name();
                                 });
    return it != m_runtimes.end() ? &*it : nullptr;
}

ScriptManager::ModRecord* ScriptManager::FindMod(std::string_view modId) {
    const auto it = std::find_if(m_mods.begin(), m_mods.end(), [modId](const ModRecord& record) {
        return record.info.id == modId;
    });
    return it != m_mods.end() ? &*it : nullptr;
}

const ScriptManager::ModRecord* ScriptManager::FindMod(std::string_view modId) const {
    const auto it = std::find_if(m_mods.begin(), m_mods.end(), [modId](const ModRecord& record) {
        return record.info.id == modId;
    });
    return it != m_mods.end() ? &*it : nullptr;
}

void ScriptManager::DisableMod(ModRecord& record, std::string reason) {
    if (record.disabled) {
        return;
    }
    record.disabled = true;
    record.disableReason = std::move(reason);
    ZL_ERROR(kModule, "mod 已禁用：[{}]（原因：{}；热重载成功后恢复）", record.info.id,
             record.disableReason);
}

}  // namespace zl::script
