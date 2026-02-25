#pragma once
#include "RathEngine/Renderer/RHI/RHITypes.h"

namespace Rath { class IWindow; }

namespace Rath::RHI {
    class IRHIContext {
    public:
        virtual ~IRHIContext() = default;

        virtual void Init    (IWindow* window) = 0;
        virtual void Shutdown()                = 0;

        virtual bool BeginFrame()                      = 0;
        virtual void EndFrame()                        = 0;
        virtual void ClearColor(const ClearColor& col) = 0;

        virtual BufferHandle  CreateBuffer (const BufferDesc&)   = 0;
        virtual void          DestroyBuffer(BufferHandle handle)  = 0;
        virtual TextureHandle CreateTexture(const TextureDesc&)   = 0;
        virtual void          DestroyTexture(TextureHandle handle)= 0;
    };
}
