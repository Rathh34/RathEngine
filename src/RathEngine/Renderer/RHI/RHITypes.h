#pragma once
#include "RathEngine/Core/Types.h"

namespace Rath::RHI {
    template <typename Tag>
    struct Handle {
        u32 index      = UINT32_MAX;
        u32 generation = 0;

        bool IsValid() const { return index != UINT32_MAX; }
        bool operator==(const Handle& o) const { return index == o.index && generation == o.generation; }
        bool operator!=(const Handle& o) const { return !(*this == o); }
    };

    struct BufferTag {}; struct TextureTag {}; struct PipelineTag {}; struct ShaderTag {};
    using BufferHandle   = Handle<BufferTag>;
    using TextureHandle  = Handle<TextureTag>;
    using PipelineHandle = Handle<PipelineTag>;
    using ShaderHandle   = Handle<ShaderTag>;

    struct BufferDesc {
        u64         size        = 0;
        bool        deviceLocal = true;
        const char* debugName   = nullptr;
    };

    struct TextureDesc {
        u32         width     = 1;
        u32         height    = 1;
        u32         mips      = 1;
        const char* debugName = nullptr;
    };

    struct ClearValue { f32 r = 0.f, g = 0.f, b = 0.f, a = 1.f; };
}
