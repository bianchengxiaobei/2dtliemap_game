#include "core/TaskScheduler.h"

#include <algorithm>
#include <exception>

#include "core/Log.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace zl::core {
namespace {

void SetCurrentThreadName(unsigned index) {
#ifdef _WIN32
    wchar_t name[32];
    swprintf(name, 32, L"zl-task-%u", index);
    SetThreadDescription(GetCurrentThread(), name);
#else
    (void)index;
#endif
}

unsigned DefaultThreadCount() {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 2 ? hw - 2 : 1;
}

}  // namespace

TaskScheduler::TaskScheduler(unsigned threadCount) : m_mainThreadId(std::this_thread::get_id()) {
    if (threadCount == 0) {
        threadCount = DefaultThreadCount();
    }
    m_threads.reserve(threadCount);
    for (unsigned i = 0; i < threadCount; ++i) {
        m_threads.emplace_back([this, i] { WorkerLoop(i); });
    }
    ZL_INFO("core", "任务线程池启动：{} 个工作线程", threadCount);
}

TaskScheduler::~TaskScheduler() {
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        m_stopping = true;
    }
    m_tasksCv.notify_all();
    for (auto& t : m_threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    ZL_INFO("core", "任务线程池已停止");
}

void TaskScheduler::Submit(Task work) {
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        m_tasks.push_back(std::move(work));
    }
    m_tasksCv.notify_one();
}

void TaskScheduler::SubmitResult(Task work, Task onMainThread) {
    Submit([this, work = std::move(work), onMainThread = std::move(onMainThread)] {
        try {
            work();
        } catch (const std::exception& e) {
            ZL_ERROR("core", "后台任务异常: {}", e.what());
        } catch (...) {
            ZL_ERROR("core", "后台任务未知异常");
        }
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results.push_back(onMainThread);
    });
}

std::size_t TaskScheduler::ProcessResults(std::size_t maxCount) {
    std::size_t processed = 0;
    while (processed < maxCount) {
        Task result;
        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            if (m_results.empty()) {
                break;
            }
            result = std::move(m_results.front());
            m_results.pop_front();
        }
        result();
        ++processed;
    }
    return processed;
}

std::size_t TaskScheduler::PendingResults() const {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_results.size();
}

void TaskScheduler::WorkerLoop(unsigned index) {
    SetCurrentThreadName(index);
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_tasksMutex);
            m_tasksCv.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
            if (m_tasks.empty()) {
                if (m_stopping) {
                    return;
                }
                continue;
            }
            task = std::move(m_tasks.front());
            m_tasks.pop_front();
        }
        task();
    }
}

}  // namespace zl::core
