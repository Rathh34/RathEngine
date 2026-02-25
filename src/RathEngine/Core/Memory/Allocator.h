#pragma once
#include <cstddef>
#include "RathEngine/Core/Types.h" // Fixed: Added this include for 'usize'

namespace Rath {
    struct IAllocator {
        virtual void* Alloc(usize size, usize alignment = 8) = 0;
        virtual void  Free(void* ptr) = 0;
        virtual void  Reset() {}
        virtual ~IAllocator() = default;
    };
}
