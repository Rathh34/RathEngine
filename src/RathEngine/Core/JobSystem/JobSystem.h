#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

namespace Rath::JobSystem {

    using Job = std::function<void()>;

    void Initialize();

    void Shutdown();

    void Execute(const Job& job, std::atomic<uint32_t>& counter);

    void Dispatch(uint32_t jobCount,
                  uint32_t groupSize,
                  const std::function<void(uint32_t start, uint32_t end)>& fn,
                  std::atomic<uint32_t>& counter);


    void Wait(const std::atomic<uint32_t>& counter);
}
