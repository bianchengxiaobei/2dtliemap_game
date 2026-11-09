#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zl::script {

// M7-T6/X3-T1：mod 文件轮询器（docs/03 §8）。
// - 后台线程按 intervalSeconds 扫描各 mod 根目录，按 scripts/metadata、content、assets、other
//   分类签名（文件数 + 最大 mtime + 总字节 + 小文件内容指纹）；只收集通知，从不触碰运行时；
// - scripts/metadata/other 变化立即列入脚本重载队列；content/assets 变化合并后等待
//   contentDebounceSeconds 静默期再通知，避免编辑器分多次写文件时重复触发；
// - 主线程取走通知后自行执行 ScriptManager::ReloadMod 或后续内容事务重载。
// 说明：只监控启动时发现的 mod；本版本不动态加载新 mod 目录。
class ModWatcher {
public:
    struct Entry {
        std::string modId;
        std::filesystem::path root;
    };

    struct ContentChange {
        std::string modId;
        bool content = false;
        bool assets = false;
    };

    ModWatcher(std::vector<Entry> mods, double intervalSeconds = 1.0,
               double contentDebounceSeconds = 0.0);
    ~ModWatcher();

    ModWatcher(const ModWatcher&) = delete;
    ModWatcher& operator=(const ModWatcher&) = delete;

    std::size_t WatchedCount() const { return m_entries.size(); }

    // 主线程调用：取走脚本/metadata/other 变更 mod id（去重；无变更 → 空）
    std::vector<std::string> PollChanges();

    // 主线程调用：取走防抖后的 content/assets 变更；同一 mod 的 content/assets 位合并。
    std::vector<ContentChange> PollContentChanges();

private:
    // 目录签名：文件数 + 最新 mtime + 总字节 + 小文件内容指纹。
    // mtime 在同一文件系统时间粒度内可能不变，内容指纹保证快速覆盖也能触发。
    struct Signature {
        int fileCount = 0;
        std::filesystem::file_time_type latest{};
        std::uint64_t totalBytes = 0;
        std::uint64_t contentHash = 0;
    };

    struct ModSignature {
        Signature scripts;
        Signature content;
        Signature assets;
        Signature other;
        bool readable = true;
    };

    struct PendingContent {
        bool content = false;
        bool assets = false;
        std::chrono::steady_clock::time_point changedAt{};
    };

    static ModSignature ComputeSignature(const std::filesystem::path& root);
    void ThreadMain();

    std::vector<Entry> m_entries;
    std::vector<ModSignature> m_baseline;  // 仅轮询线程访问（构造时初始化）
    std::vector<PendingContent> m_pendingContentState;  // 仅轮询线程访问
    double m_intervalSeconds = 1.0;
    double m_contentDebounceSeconds = 0.0;

    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::vector<std::string> m_pending;  // 受 m_mutex 保护
    std::vector<ContentChange> m_pendingContent;  // 受 m_mutex 保护
};

}  // namespace zl::script
