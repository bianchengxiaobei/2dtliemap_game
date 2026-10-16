#pragma once

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zl::script {

// M7-T6：mod 热重载轮询器（docs/03 §8）。
// - 后台线程按 intervalSeconds 扫描各 mod 根目录全部文件的 mtime 签名
//   （最大 mtime + 文件数）；只收集通知，从不触碰脚本运行时（Lua 只归主线程）；
// - 签名变化 → 列入待处理并立即推进基准（同一变更不反复通知；修复文件再存盘会再次触发）；
// - 主线程每帧 PollChanges() 取走待处理 mod id，自行执行 ScriptManager::ReloadMod。
// 说明：只监控启动时已加载的 mod（本版本无运行时动态加载 mod 的入口）。
class ModWatcher {
public:
    struct Entry {
        std::string modId;
        std::filesystem::path root;
    };

    ModWatcher(std::vector<Entry> mods, double intervalSeconds = 1.0);
    ~ModWatcher();

    ModWatcher(const ModWatcher&) = delete;
    ModWatcher& operator=(const ModWatcher&) = delete;

    std::size_t WatchedCount() const { return m_entries.size(); }

    // 主线程调用：取走自上次调用以来的变更 mod id（去重；无变更 → 空）
    std::vector<std::string> PollChanges();

private:
    // 目录签名：文件数 + 最新 mtime（文件增删改都会反映）
    struct Signature {
        int fileCount = 0;
        std::filesystem::file_time_type latest{};
    };

    static Signature ComputeSignature(const std::filesystem::path& root);
    void ThreadMain();

    std::vector<Entry> m_entries;
    std::vector<Signature> m_baseline;  // 仅轮询线程访问（构造时初始化）
    double m_intervalSeconds = 1.0;

    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::vector<std::string> m_pending;  // 受 m_mutex 保护
};

}  // namespace zl::script
