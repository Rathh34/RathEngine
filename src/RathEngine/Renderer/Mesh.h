#pragma once
#include "RathEngine/Renderer/RHI/RHITypes.h"
#include "RathEngine/Renderer/RHI/IRHIContext.h"
#include "RathEngine/Core/Types.h"
#include <vector>
#include <string>

namespace Rath {
    struct Vertex {
        f32 position[3]; // Now 3D!
        f32 color[3];
        f32 uv[2];
    };

    class Mesh {
    public:
        Mesh() = default;
        ~Mesh() = default;

        void LoadFromData(RHI::IRHIContext* rhi, const std::vector<Vertex>& vertices, const std::vector<u16>& indices);
        
        // Loads a 3D model from disk using tinyobjloader
        bool LoadFromObj(RHI::IRHIContext* rhi, const std::string& filepath);

        void Destroy(RHI::IRHIContext* rhi);

        [[nodiscard]] RHI::BufferHandle GetVertexBuffer() const { return m_VertexBuffer; }
        [[nodiscard]] RHI::BufferHandle GetIndexBuffer() const { return m_IndexBuffer; }
        [[nodiscard]] u32 GetIndexCount() const { return m_IndexCount; }

    private:
        RHI::BufferHandle m_VertexBuffer;
        RHI::BufferHandle m_IndexBuffer;
        u32               m_IndexCount = 0;
    };
}
