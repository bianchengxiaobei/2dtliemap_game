#include "script/ModWatcher.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace zl::script {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::uintmax_t kMaxHashedFileBytes = 256ull * 1024ull;

std::uint64_t HashBytes(std::uint64_t hash, const char* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint8_t>(data[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t HashText(std::string_view text) {
    return HashBytes(kFnvOffset, text.data(), text.size());
}

std::uint64_t Mix(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31U);
}

std::uint64_t HashFileContents(const std::filesystem::path& path,
                               std::uintmax_t size, bool* complete) {
    *complete = false;
    if (size > kMaxHashedFileBytes) {
        return 0;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return 0;
    }
    std::uint64_t hash = kFnvOffset;
    std::array<char, 8192> buffer{};
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = file.gcount();
        if (read > 0) {
            hash = HashBytes(hash, buffer.data(), static_cast<std::size_t>(read));
        }
    }
    *complete = file.eof();
    return hash;
}

}  // namespace

ModWatcher::ModWatcher(std::vector<Entry> mods, double intervalSeconds,
                       double contentDebounceSeconds)
    : m_entries(std::move(mods)),
      m_intervalSeconds(intervalSeconds > 0.01 ? intervalSeconds : 0.01),
      m_contentDebounceSeconds(std::max(0.0, contentDebounceSeconds)) {
    m_baseline.reserve(m_entries.size());
    m_pendingContentState.resize(m_entries.size());
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

std::vector<ModWatcher::ContentChange> ModWatcher::PollContentChanges() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ContentChange> out;
    out.swap(m_pendingContent);
    return out;
}

ModWatcher::ModSignature ModWatcher::ComputeSignature(
    const std::filesystem::path& root) {
    ModSignature signature;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        signature.readable = false;
        return signature;
    }
    std::filesystem::recursive_directory_iterator it(root, ec);
    if (ec) {
        signature.readable = false;
        return signature;
    }
    const std::filesystem::recursive_directory_iterator end;
    while (it != end) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) {
            const std::filesystem::path relative =
                it->path().lexically_relative(root);
            Signature* bucket = &signature.other;
            if (relative == "mod.json") {
                bucket = &signature.scripts;
            } else if (!relative.empty()) {
                const std::filesystem::path first = *relative.begin();
                if (first == "scripts") {
                    bucket = &signature.scripts;
                } else if (first == "content") {
                    bucket = &signature.content;
                } else if (first == "assets") {
                    bucket = &signature.assets;
                }
            }

            ++bucket->fileCount;
            const std::filesystem::file_time_type time =
                it->last_write_time(fileEc);
            if (!fileEc && time > bucket->latest) {
                bucket->latest = time;
            }
            std::error_code sizeEc;
            const std::uintmax_t size = it->file_size(sizeEc);
            const std::uintmax_t safeSize = sizeEc ? 0 : size;
            bucket->totalBytes += static_cast<std::uint64_t>(safeSize);
            bool hashed = false;
            const std::uint64_t contentHash =
                HashFileContents(it->path(), safeSize, &hashed);
            const std::uint64_t pathHash =
                HashText(relative.generic_string());
            const std::uint64_t fileHash =
                pathHash ^ (hashed ? contentHash : 0) ^
                Mix(safeSize) ^
                Mix(static_cast<std::uint64_t>(
                    time.time_since_epoch().count()));
            bucket->contentHash ^= Mix(fileHash);
        }
        it.increment(ec);
        if (ec) {
            signature.readable = false;
            return signature;
        }
    }
    return signature;
}

void ModWatcher::ThreadMain() {
    auto same = [](const Signature& a, const Signature& b) {
        return a.fileCount == b.fileCount && a.totalBytes == b.totalBytes &&
               a.latest == b.latest && a.contentHash == b.contentHash;
    };
    const auto debounce =
        std::chrono::duration<double>(m_contentDebounceSeconds);

    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_wake.wait_for(
                    lock, std::chrono::duration<double>(m_intervalSeconds),
                    [this] { return m_stop; })) {
                return;  // 停止信号
            }
        }

        // 扫描在锁外进行（baseline/pending 仅本线程访问；文件系统操作可能较慢）
        const auto now = std::chrono::steady_clock::now();
        std::vector<std::string> changedScripts;
        std::vector<ContentChange> changedContent;
        for (std::size_t i = 0; i < m_entries.size(); ++i) {
            const ModSignature signature = ComputeSignature(m_entries[i].root);
            if (!signature.readable) {
                continue;  // 目录缺失/扫描中断：保留旧基线，等恢复后再比较
            }
            ModSignature& baseline = m_baseline[i];
            const bool scriptsChanged =
                !same(signature.scripts, baseline.scripts) ||
                !same(signature.other, baseline.other);
            const bool contentChanged =
                !same(signature.content, baseline.content);
            const bool assetsChanged =
                !same(signature.assets, baseline.assets);

            if (scriptsChanged) {
                baseline.scripts = signature.scripts;
                baseline.other = signature.other;
                changedScripts.push_back(m_entries[i].modId);
            }
            if (contentChanged || assetsChanged) {
                baseline.content = signature.content;
                baseline.assets = signature.assets;
                PendingContent& pending = m_pendingContentState[i];
                pending.content = pending.content || contentChanged;
                pending.assets = pending.assets || assetsChanged;
                pending.changedAt = now;
            }

            PendingContent& pending = m_pendingContentState[i];
            if ((pending.content || pending.assets) &&
                now - pending.changedAt >= debounce) {
                ContentChange change;
                change.modId = m_entries[i].modId;
                change.content = pending.content;
                change.assets = pending.assets;
                changedContent.push_back(std::move(change));
                pending = {};
            }
        }
        if (changedScripts.empty() && changedContent.empty()) {
            continue;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        for (std::string& modId : changedScripts) {
            if (std::find(m_pending.begin(), m_pending.end(), modId) ==
                m_pending.end()) {
                m_pending.push_back(std::move(modId));
            }
        }
        for (ContentChange& change : changedContent) {
            const auto existing =
                std::find_if(m_pendingContent.begin(), m_pendingContent.end(),
                             [&change](const ContentChange& item) {
                                 return item.modId == change.modId;
                             });
            if (existing == m_pendingContent.end()) {
                m_pendingContent.push_back(std::move(change));
            } else {
                existing->content = existing->content || change.content;
                existing->assets = existing->assets || change.assets;
            }
        }
    }
}

}  // namespace zl::script
