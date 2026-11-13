#include "save/SaveService.h"

#include <algorithm>
#include <chrono>
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

bool SaveService::RequestChunk(std::string id, ChunkDiff diff,
                               std::string* error) {
    if (id.empty()) {
        if (error != nullptr) {
            *error = "存档 id 为空";
        }
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) {
            if (error != nullptr) {
                *error = "后台存档线程正在停止";
            }
            return false;
        }
        m_chunkRequests.push_back(
            ChunkWriteRequest{std::move(id), std::move(diff), 0});
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
    return m_active || !m_requests.empty() || !m_chunkRequests.empty();
}

std::size_t SaveService::PendingRequests() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_requests.size();
}

std::size_t SaveService::PendingChunkWrites() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_chunkRequests.size();
}

std::uint64_t SaveService::FailedChunkWrites() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_failedChunkWrites;
}

SaveService::ChunkWriteStats SaveService::GetChunkWriteStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_chunkWriteStats;
}

void SaveService::WaitIdle() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] {
        return !m_active && m_requests.empty() && m_chunkRequests.empty();
    });
}

void SaveService::WorkerLoop() {
    NameWorkerThread();
    for (;;) {
        SaveRequest request;
        ChunkWriteRequest chunkRequest;
        bool isChunk = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return m_stopping || !m_requests.empty() ||
                       !m_chunkRequests.empty();
            });
            if (m_requests.empty() && m_chunkRequests.empty()) {
                if (m_stopping) {
                    return;
                }
                continue;
            }
            if (!m_chunkRequests.empty()) {
                chunkRequest = std::move(m_chunkRequests.front());
                m_chunkRequests.pop_front();
                isChunk = true;
            } else {
                request = std::move(m_requests.front());
                m_requests.pop_front();
            }
            m_active = true;
        }

        if (isChunk) {
            const auto chunkBegin = std::chrono::steady_clock::now();
            bool ok = false;
            try {
                ok = ExecuteChunkWrite(chunkRequest);
            } catch (const std::exception& e) {
                ZL_ERROR("save", "后台 chunk 写入异常 '{}'：{}", chunkRequest.id,
                         e.what());
            } catch (...) {
                ZL_ERROR("save", "鍚庡彴 chunk 鍐欏叆鏈煡寮傚父 '{}'", chunkRequest.id);
            }
            const double elapsedMs =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - chunkBegin)
                    .count();
            bool retry = false;
            bool failed = false;
            if (!ok) {
                if (chunkRequest.attempts < 2) {
                    ++chunkRequest.attempts;
                    retry = true;
                } else {
                    failed = true;
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_active = false;
                m_chunkWriteStats.totalMs += elapsedMs;
                m_chunkWriteStats.maxMs =
                    std::max(m_chunkWriteStats.maxMs, elapsedMs);
                if (retry) {
                    ++m_chunkWriteStats.retried;
                    m_chunkRequests.push_front(std::move(chunkRequest));
                }
                if (ok) {
                    ++m_chunkWriteStats.completed;
                }
                if (failed) {
                    ++m_chunkWriteStats.failed;
                    ++m_failedChunkWrites;
                }
            }
            if (failed) {
                ZL_ERROR("save", "chunk 后台写入连续失败，已放弃 '{}'",
                         chunkRequest.id);
            }
            m_cv.notify_all();
            continue;
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

bool SaveService::ExecuteChunkWrite(ChunkWriteRequest request) {
    std::string error;
    if (m_manager.SaveChunkDiff(request.id, request.diff, &error)) {
        return true;
    }
    ZL_WARN("save", "chunk 后台写入失败 '{}'（尝试 {}）：{}", request.id,
            request.attempts + 1, error);
    return false;
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
        options.manifest = request.hasManifest ? &request.manifest : nullptr;
        options.buckets = request.hasBuckets ? &request.buckets : nullptr;
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
    options.manifest = request.hasManifest ? &request.manifest : nullptr;
    options.buckets = request.hasBuckets ? &request.buckets : nullptr;
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
