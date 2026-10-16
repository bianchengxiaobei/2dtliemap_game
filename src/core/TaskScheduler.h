#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace zl::core {

// 任务线程池（docs/00 §3.2）：后台只处理纯数据，产出结果进队列，
// 主线程在 ProcessResults() 统一消费并挂接进世界。
class TaskScheduler {
public:
    using Task = std::function<void()>;

    // threadCount = 0 时取 max(1, 硬件线程数 - 2)
    explicit TaskScheduler(unsigned threadCount = 0);
    ~TaskScheduler();

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    // 丢给线程池执行，不关心结果
    void Submit(Task work);

    // work 在线程池执行；执行完毕后 onMainThread 进结果队列，由主线程按 FIFO 消费
    void SubmitResult(Task work, Task onMainThread);

    // 主线程调用：至多消费 maxCount 个结果
    std::size_t ProcessResults(std::size_t maxCount = static_cast<std::size_t>(-1));

    std::size_t PendingResults() const;
    unsigned ThreadCount() const { return static_cast<unsigned>(m_threads.size()); }

private:
    void WorkerLoop(unsigned index);

    std::vector<std::thread> m_threads;
    std::thread::id m_mainThreadId;

    std::deque<Task> m_tasks;
    mutable std::mutex m_tasksMutex;
    std::condition_variable m_tasksCv;
    bool m_stopping = false;

    std::deque<Task> m_results;
    mutable std::mutex m_resultsMutex;
};

}  // namespace zl::core
