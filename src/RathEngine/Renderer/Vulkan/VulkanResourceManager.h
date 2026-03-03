#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "RathEngine/Renderer/RHI/RHITypes.h"
#include "RathEngine/Renderer/Vulkan/VulkanDescriptors.h"
#include <vector>
#include <functional>

namespace Rath::RHI {
    struct InternalBuffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        u32 generation{0};
    };

    struct InternalTexture {
        VkImage image{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkSampler sampler{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        VkDescriptorSetLayout layout{VK_NULL_HANDLE};
        u32 generation{0};
    };

    class VulkanResourceManager {
    public:
        void Init(VkDevice device, VmaAllocator allocator, DescriptorAllocator* descAllocator, 
                  std::function<void(std::function<void(VkCommandBuffer)>&&)> immediateSubmitFn);
        void Cleanup();

        BufferHandle CreateBuffer(const BufferDesc& desc);
        void DestroyBuffer(BufferHandle handle);
        void* MapBuffer(BufferHandle handle);
        void UnmapBuffer(BufferHandle handle);
        void UploadBufferData(BufferHandle handle, const void* data, u64 size);
        InternalBuffer& GetBuffer(BufferHandle handle);

        TextureHandle CreateTexture(const TextureDesc& desc);
        void DestroyTexture(TextureHandle handle);
        InternalTexture& GetTexture(TextureHandle handle);

    private:
        VkDevice m_Device{VK_NULL_HANDLE};
        VmaAllocator m_Allocator{VK_NULL_HANDLE};
        DescriptorAllocator* m_DescriptorAllocator{nullptr};
        std::function<void(std::function<void(VkCommandBuffer)>&&)> m_ImmediateSubmit;

        std::vector<InternalBuffer> m_Buffers;
        std::vector<InternalTexture> m_Textures;
    };
}
