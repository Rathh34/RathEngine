#pragma once
#include "RathEngine/Core/Memory/Allocator.h"
#include "RathEngine/Core/Types.h"
#include <cstddef>

namespace Rath {
    template <usize ObjectSize, usize Capacity>
    class PoolAllocator final : public IAllocator {
        static_assert(ObjectSize >= sizeof(usize), "ObjectSize must be >= sizeof(usize)");
        static_assert(Capacity > 0, "Capacity must be > 0");

    public:
        PoolAllocator() {
            for (usize i = 0; i < Capacity - 1; ++i)
                *reinterpret_cast<usize*>(&m_Memory[i * ObjectSize]) = i + 1;
            *reinterpret_cast<usize*>(&m_Memory[(Capacity - 1) * ObjectSize]) = k_Invalid;
            m_FreeHead = 0;
        }

        void* Alloc(usize, usize = 8) override {
            if (m_FreeHead == k_Invalid) return nullptr;
            void* ptr  = &m_Memory[m_FreeHead * ObjectSize];
            m_FreeHead = *reinterpret_cast<usize*>(ptr);
            return ptr;
        }

        void Free(void* ptr) override {
            usize idx = (static_cast<u8*>(ptr) - m_Memory) / ObjectSize;
            *reinterpret_cast<usize*>(ptr) = m_FreeHead;
            m_FreeHead = idx;
        }

        void Reset() override {}

    private:
        static constexpr usize k_Invalid = ~usize(0);
        alignas(alignof(std::max_align_t)) u8 m_Memory[ObjectSize * Capacity]{};
        usize m_FreeHead = 0;
    };
}
