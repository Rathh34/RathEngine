#pragma once
#include "RathEngine/Renderer/RHI/RHITypes.h"

namespace Rath { class IWindow; }

namespace Rath::RHI {
    class IRHIContext {
    public:
        virtual ~IRHIContext() = default;

        virtual void Init    (IWindow* window) = 0;
        virtual void Shutdown()                = 0;

        virtual void WaitIdle() = 0;

        [[nodiscard]] virtual bool BeginFrame() = 0;
        virtual void EndFrame()                 = 0;

        virtual void BeginPass(const ClearValue& clearColor) = 0;
        virtual void EndPass()                               = 0;
        virtual void Draw(u32 vertexCount)                   = 0;
        virtual void DrawIndexed(u32 indexCount)             = 0;

        virtual BufferHandle  CreateBuffer    (const BufferDesc&)   = 0;
        virtual void          DestroyBuffer   (BufferHandle handle) = 0;
        virtual void*         MapBuffer       (BufferHandle handle) = 0;
        virtual void          UnmapBuffer     (BufferHandle handle) = 0;
        virtual void          UploadBufferData(BufferHandle handle, const void* data, u64 size) = 0;
        virtual void          BindVertexBuffer(BufferHandle handle) = 0;
        virtual void          BindIndexBuffer (BufferHandle handle) = 0;

        virtual TextureHandle CreateTexture   (const TextureDesc&)  = 0;
        virtual void          DestroyTexture  (TextureHandle handle)= 0;
        virtual void          BindTexture     (TextureHandle handle)= 0;

        virtual void          PushConstants   (const void* data, u32 size) = 0;
    };
}
