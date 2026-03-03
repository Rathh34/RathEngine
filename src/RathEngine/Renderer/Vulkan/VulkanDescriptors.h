#pragma once
#include "RathEngine/Core/Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>

namespace Rath::RHI {
    
    class DescriptorAllocator {
    public:
        void Init(VkDevice device);
        void Cleanup();
        bool Allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout);

    private:
        VkDescriptorPool GrabPool();

        VkDevice m_Device{VK_NULL_HANDLE};
        VkDescriptorPool m_CurrentPool{VK_NULL_HANDLE};
        std::vector<VkDescriptorPool> m_UsedPools;
        std::vector<VkDescriptorPool> m_FreePools;
    };

    class DescriptorBuilder {
    public:
        DescriptorBuilder(DescriptorAllocator* allocator) : m_Allocator(allocator) {}

        DescriptorBuilder& BindBuffer(u32 binding, VkDescriptorBufferInfo* bufferInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);
        DescriptorBuilder& BindImage(u32 binding, VkDescriptorImageInfo* imageInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);

        bool Build(VkDevice device, VkDescriptorSet& set, VkDescriptorSetLayout& layout);

    private:
        DescriptorAllocator* m_Allocator;
        std::vector<VkWriteDescriptorSet> m_Writes;
        std::vector<VkDescriptorSetLayoutBinding> m_Bindings;
    };
}
