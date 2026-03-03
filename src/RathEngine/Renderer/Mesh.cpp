#include "RathEngine/Renderer/Mesh.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <iostream>
#include <filesystem>

namespace Rath {

    void Mesh::LoadFromData(RHI::IRHIContext* rhi,
                            const std::vector<Vertex>& vertices,
                            const std::vector<u16>& indices)
    {
        m_IndexCount = static_cast<u32>(indices.size());

        u64 vertexBufferSize = vertices.size() * sizeof(Vertex);
        RHI::BufferDesc vDesc{vertexBufferSize, true, "MeshVertexBuffer"};
        m_VertexBuffer = rhi->CreateBuffer(vDesc);
        rhi->UploadBufferData(m_VertexBuffer, vertices.data(), vertexBufferSize);

        u64 indexBufferSize = indices.size() * sizeof(u16);
        RHI::BufferDesc iDesc{indexBufferSize, true, "MeshIndexBuffer"};
        m_IndexBuffer = rhi->CreateBuffer(iDesc);
        rhi->UploadBufferData(m_IndexBuffer, indices.data(), indexBufferSize);
    }

    bool Mesh::LoadFromObj(RHI::IRHIContext* rhi, const std::string& filepath)
    {
        const std::filesystem::path objPath(filepath);

        if (!std::filesystem::exists(objPath)) {
            std::cerr << "[Mesh Error] OBJ not found: " << objPath.string()
                      << " (cwd=" << std::filesystem::current_path().string() << ")\n";
            return false;
        }

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        std::string baseDir = objPath.parent_path().string();
        if (!baseDir.empty())
            baseDir += std::filesystem::path::preferred_separator;

        const bool success = tinyobj::LoadObj(
            &attrib, &shapes, &materials,
            &warn, &err,
            objPath.string().c_str(),
            baseDir.c_str()
        );

        if (!warn.empty())
            std::cout << "[Mesh Warning] " << warn << "\n";

        if (!err.empty())
            std::cerr << "[Mesh Error] " << err << "\n";

        if (!success) {
            std::cerr << "[Mesh] Failed to load OBJ: " << objPath.string() << "\n";
            return false;
        }

        std::vector<Vertex> vertices;
        std::vector<u16> indices;
        vertices.reserve(1024);
        indices.reserve(1024);

        for (const auto& shape : shapes) {
            for (const auto& idx : shape.mesh.indices) {
                Vertex v{};

                v.position[0] = attrib.vertices[3 * idx.vertex_index + 0];
                v.position[1] = attrib.vertices[3 * idx.vertex_index + 1];
                v.position[2] = attrib.vertices[3 * idx.vertex_index + 2];

                if (idx.texcoord_index >= 0) {
                    v.uv[0] = attrib.texcoords[2 * idx.texcoord_index + 0];
                    v.uv[1] = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
                } else {
                    v.uv[0] = 0.0f;
                    v.uv[1] = 0.0f;
                }

                v.color[0] = 1.0f;
                v.color[1] = 1.0f;
                v.color[2] = 1.0f;

                indices.push_back(static_cast<u16>(vertices.size()));
                vertices.push_back(v);
            }
        }

        LoadFromData(rhi, vertices, indices);
        return true;
    }

    void Mesh::Destroy(RHI::IRHIContext* rhi)
    {
        if (m_VertexBuffer.IsValid()) rhi->DestroyBuffer(m_VertexBuffer);
        if (m_IndexBuffer.IsValid())  rhi->DestroyBuffer(m_IndexBuffer);
    }
}
