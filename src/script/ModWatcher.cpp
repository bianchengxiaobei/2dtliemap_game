#include "script/ModWatcher.h"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

namespace zl::script {

ModWatcher::ModWatcher(std::vector<Entry> mods, double intervalSeconds)
    : m_entries(std::move(mods)), m_intervalSeconds(intervalSeconds > 0.01 ? intervalSeconds : 0.01) {
    m_baseline.reserve(m_entries.size());
    for (const Entry& entry : m_entries) {
        m_baseline.push_back(ComputeSignature(entry.root));  // 基准 = 加载时刻的磁盘状态
    }
    m_thread = std::thread([this] { ThreadMain(); });
}

ModWatcher::~ModWatcher() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::vector<std::string> ModWatcher::PollChanges() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> out;
    out.swap(m_pending);
    return out;
}

ModWatcher::Signature ModWatcher::ComputeSignature(const std::filesystem::path& root) {
    Signature signature;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(root, ec);
    if (ec) {
        return signature;  // 目录不可读：空签名（与基准不同才会触发）
    }
    const std::filesystem::recursive_directory_iterator end;
    while (it != end) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) {
            ++signature.fileCount;
            const std::filesystem::file_time_type time = it->last_write_time(fileEc);
            if (!fileEc && time > signature.latest) {
                signature.latest = time;
            }
        }
        it.increment(ec);
        if (ec) {
            break;  // 扫描中断：以已收集部分为准（保守）
        }
    }
    return signature;
}

void ModWatcher::ThreadMain() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_wake.wait_for(lock, std::chrono::duration<double>(m_intervalSeconds),
                                [this] { return m_stop; })) {
                return;  // 停止信号
            }
        }

        // 扫描在锁外进行（m_baseline 仅本线程访问；文件系统操作可能较慢）
        std::vector<std::string> changed;
        for (std::size_t i = 0; i < m_entries.size(); ++i) {
            const Signature signature = ComputeSignature(m_entries[i].root);
            const Signature& baseline = m_baseline[i];
            if (signature.fileCount != baseline.fileCount ||
                signature.latest != baseline.latest) {
                m_baseline[i] = signature;  // 立即推进基准：同一变更只通知一次
                changed.push_back(m_entries[i].modId);
            }
        }
        if (changed.empty()) {
            continue;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        for (std::string& modId : changed) {
            if (std::find(m_pending.begin(), m_pending.end(), modId) == m_pending.end()) {
                m_pending.push_back(std::move(modId));
            }
        }
    }
}

}  // namespace zl::script
