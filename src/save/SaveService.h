#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "save/ChunkDiff.h"
#include "save/EntityStore.h"
#include "save/SaveTypes.h"

namespace zl::save {

class SaveManager;

// M10-T5：后台存档请求。主线程只负责生成纯数据快照（实体/缩略图/chunk diff），
// zl-save 线程负责编码/原子落盘；禁止后台线程访问 ECS registry 或世界对象。
struct SaveRequest {
    std::string id;  // 空 = 新建存档
    std::string name;
    std::string mapId;
    std::string gameVersion;
    std::optional<double> gameMinutes;
    std::vector<SavePlayer> players;
    EntitySnapshot entities;
    ThumbnailImage thumbnail;
    std::vector<ChunkDiff> chunks;
};

struct SaveResult {
    bool ok = false;
    bool created = false;
    std::string id;
    std::string error;
};

class SaveService {
public:
    explicit SaveService(SaveManager& manager);
    ~SaveService();

    SaveService(const SaveService&) = delete;
    SaveService& operator=(const SaveService&) = delete;

    bool Request(SaveRequest request, std::string* error = nullptr);

    // 主线程每帧调用：取走已完成结果（FIFO）
    std::vector<SaveResult> PollResults();

    bool Busy() const;
    std::size_t PendingRequests() const;

    // 测试/退出用：等待队列与当前写盘全部完成（结果仍留在结果队列）
    void WaitIdle();

private:
    void WorkerLoop();
    SaveResult Execute(SaveRequest request);

    SaveManager& m_manager;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<SaveRequest> m_requests;
    std::deque<SaveResult> m_results;
    std::thread m_thread;
    bool m_stopping = false;
    bool m_active = false;
};

}  // namespace zl::save
