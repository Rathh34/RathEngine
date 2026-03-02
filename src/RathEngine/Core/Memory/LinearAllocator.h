#pragma once
#include "RathEngine/Core/Memory/Allocator.h"
#include "RathEngine/Core/Assert.h"
#include "RathEngine/Core/Types.h"
#include <new>

namespace Rath {
    class LinearAllocator final : public IAllocator {
    public:
        explicit LinearAllocator(usize capacity)
            : m_Memory(static_cast<u8*>(::operator new(capacity))), m_Capacity(capacity) {}

        ~LinearAllocator() override { ::operator delete(m_Memory); }

        void* Alloc(usize size, usize alignment = 8) override {
            RATH_ASSERT((alignment & (alignment - 1)) == 0, "Alignment must be power of two");
            usize aligned = (m_Offset + alignment - 1) & ~(alignment - 1);
            if (aligned + size > m_Capacity) return nullptr;
            m_Offset = aligned + size;
            return m_Memory + aligned;
        }

        void Free(void*)  override {}
        void Reset()      override { m_Offset = 0; }

        usize Used()      const { return m_Offset; }
        usize Capacity()  const { return m_Capacity; }

    private:
        u8*   m_Memory   = nullptr;
        usize m_Capacity = 0;
        usize m_Offset   = 0;
    };
}
