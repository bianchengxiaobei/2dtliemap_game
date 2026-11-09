#include "save/SaveService.h"

#include <exception>
#include <iterator>
#include <utility>

#include "core/Log.h"
#include "save/SaveManager.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zl::save {
namespace {

void NameWorkerThread() {
#ifdef _WIN32
    SetThreadDescription(GetCurrentThread(), L"zl-save");
#endif
}

}  // namespace

SaveService::SaveService(SaveManager& manager) : m_manager(manager) {
    m_thread = std::thread([this] { WorkerLoop(); });
    ZL_INFO("save", "后台存档线程启动：zl-save");
}

SaveService::~SaveService() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    ZL_INFO("save", "后台存档线程已停止");
}

bool SaveService::Request(SaveRequest request, std::string* error) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) {
            if (error != nullptr) {
                *error = "后台存档线程正在停止";
            }
            return false;
        }
        m_requests.push_back(std::move(request));
    }
    m_cv.notify_one();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::vector<SaveResult> SaveService::PollResults() {
    std::deque<SaveResult> results;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        results.swap(m_results);
    }
    return std::vector<SaveResult>(std::make_move_iterator(results.begin()),
                                   std::make_move_iterator(results.end()));
}

bool SaveService::Busy() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active || !m_requests.empty();
}

std::size_t SaveService::PendingRequests() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_requests.size();
}

void SaveService::WaitIdle() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return !m_active && m_requests.empty(); });
}

void SaveService::WorkerLoop() {
    NameWorkerThread();
    for (;;) {
        SaveRequest request;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock,
                      [this] { return m_stopping || !m_requests.empty(); });
            if (m_requests.empty()) {
                if (m_stopping) {
                    return;
                }
                continue;
            }
            request = std::move(m_requests.front());
            m_requests.pop_front();
            m_active = true;
        }

        const std::string requestId = request.id;
        SaveResult result;
        try {
            result = Execute(std::move(request));
        } catch (const std::exception& e) {
            result.id = requestId;
            result.error = std::string("后台存档异常: ") + e.what();
            ZL_ERROR("save", "后台存档异常 '{}'：{}", requestId, e.what());
        } catch (...) {
            result.id = requestId;
            result.error = "后台存档未知异常";
            ZL_ERROR("save", "后台存档未知异常 '{}'", requestId);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_active = false;
            m_results.push_back(std::move(result));
        }
        m_cv.notify_all();
    }
}

SaveResult SaveService::Execute(SaveRequest request) {
    SaveResult result;
    if (request.id.empty()) {
        SaveManager::CreateOptions options;
        options.name = request.name.empty() ? "存档" : request.name;
        options.mapId = request.mapId;
        options.gameVersion = request.gameVersion;
        options.gameMinutes = request.gameMinutes.value_or(0.0);
        options.players = std::move(request.players);
        options.thumbnail = std::move(request.thumbnail);
        options.entities = &request.entities;
        options.chunks = std::move(request.chunks);
        result.created = true;
        result.ok = m_manager.Create(options, &result.id, &result.error);
        if (result.ok) {
            ZL_INFO("save", "后台新建存档完成：{}", result.id);
        } else {
            ZL_ERROR("save", "后台新建存档失败：{}", result.error);
        }
        return result;
    }

    result.id = request.id;
    SaveManager::SaveOptions options;
    options.entities = &request.entities;
    options.thumbnail =
        request.thumbnail.Valid() ? &request.thumbnail : nullptr;
    options.chunks = &request.chunks;
    options.gameMinutes = request.gameMinutes;
    result.ok = m_manager.Save(request.id, options, &result.error);
    if (result.ok) {
        ZL_INFO("save", "后台存档完成：{}", result.id);
    } else {
        ZL_ERROR("save", "后台存档失败 '{}'：{}", result.id, result.error);
    }
    return result;
}

}  // namespace zl::save
