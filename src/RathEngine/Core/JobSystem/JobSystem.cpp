#include "RathEngine/Core/JobSystem/JobSystem.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>

namespace Rath::JobSystem {

    namespace {

        template <size_t Capacity>
        class JobQueue {
        public:
            bool push(const Job& job) {
                std::lock_guard<std::mutex> guard(m_Mutex);
                size_t next = (m_Head + 1) % Capacity;
                if (next == m_Tail) return false;
                m_Buffer[m_Head] = job;
                m_Head = next;
                return true;
            }

            bool pop(Job& job) {
                std::lock_guard<std::mutex> guard(m_Mutex);
                if (m_Head == m_Tail) return false;
                job = m_Buffer[m_Tail];
                m_Tail = (m_Tail + 1) % Capacity;
                return true;
            }

        private:
            Job    m_Buffer[Capacity]{};
            size_t m_Head = 0;
            size_t m_Tail = 0;
            std::mutex m_Mutex;
        };

        constexpr size_t JOB_QUEUE_CAPACITY = 1024;

        JobQueue<JOB_QUEUE_CAPACITY> g_Queue;
        std::condition_variable      g_WakeCondition;
        std::mutex                   g_WakeMutex;
        std::vector<std::thread>     g_Workers;
        std::atomic<bool>            g_Running{false};

        void WorkerLoop() {
            while (g_Running.load(std::memory_order_acquire)) {
                Job job;
                if (g_Queue.pop(job)) {
                    job();
                } else {
                    std::unique_lock<std::mutex> lock(g_WakeMutex);
                    g_WakeCondition.wait(lock);
                }
            }
        }

    } // anonymous namespace

    void Initialize() {
        if (g_Running.exchange(true, std::memory_order_acq_rel)) return;

        unsigned int hw          = std::thread::hardware_concurrency();
        unsigned int threadCount = hw > 1 ? hw - 1 : 1;

        g_Workers.reserve(threadCount);
        for (unsigned int i = 0; i < threadCount; ++i) {
            g_Workers.emplace_back(WorkerLoop);
        }
    }

    void Shutdown() {
        if (!g_Running.exchange(false, std::memory_order_acq_rel)) return;

        {
            std::lock_guard<std::mutex> lock(g_WakeMutex);
            g_WakeCondition.notify_all();
        }

        for (std::thread& t : g_Workers) {
            if (t.joinable()) t.join();
        }
        g_Workers.clear();
    }

    void Execute(const Job& job, std::atomic<uint32_t>& counter) {
        counter.fetch_add(1, std::memory_order_relaxed);

        Job wrapped = [job, &counter]() {
            job();
            counter.fetch_sub(1, std::memory_order_release);
        };

        while (!g_Queue.push(wrapped)) {
            g_WakeCondition.notify_one();
            wrapped();
            return;
        }
        g_WakeCondition.notify_one();
    }

    void Dispatch(uint32_t jobCount,
                  uint32_t groupSize,
                  const std::function<void(uint32_t, uint32_t)>& fn,
                  std::atomic<uint32_t>& counter) {
        if (jobCount == 0 || groupSize == 0) return;

        uint32_t groupCount = (jobCount + groupSize - 1) / groupSize;

        for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
            Job job = [groupIndex, groupSize, jobCount, fn]() {
                uint32_t start = groupIndex * groupSize;
                uint32_t end   = std::min(start + groupSize, jobCount);
                fn(start, end);
            };
            Execute(job, counter);
        }
    }

    void Wait(const std::atomic<uint32_t>& counter) {
        while (counter.load(std::memory_order_acquire) > 0) {
            Job job;
            if (g_Queue.pop(job)) {
                job();
            } else {
                std::this_thread::yield();
            }
        }
    }

} // namespace Rath::JobSystem
