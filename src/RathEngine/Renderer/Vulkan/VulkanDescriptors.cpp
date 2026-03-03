#include "RathEngine/Renderer/Vulkan/VulkanDescriptors.h"
#include <stdexcept>

namespace Rath::RHI {

    void DescriptorAllocator::Init(VkDevice device) {
        m_Device = device;
    }

    void DescriptorAllocator::Cleanup() {
        for (auto p : m_FreePools) {
            vkDestroyDescriptorPool(m_Device, p, nullptr);
        }
        for (auto p : m_UsedPools) {
            vkDestroyDescriptorPool(m_Device, p, nullptr);
        }
        if (m_CurrentPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Device, m_CurrentPool, nullptr);
        }
    }

    VkDescriptorPool DescriptorAllocator::GrabPool() {
        if (!m_FreePools.empty()) {
            VkDescriptorPool pool = m_FreePools.back();
            m_FreePools.pop_back();
            return pool;
        }

        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000}
        };

        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1000;
        poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
        poolInfo.pPoolSizes = poolSizes;

        VkDescriptorPool newPool;
        if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &newPool) != VK_SUCCESS) {
            throw std::runtime_error("Vulkan: Failed to create descriptor pool");
        }
        return newPool;
    }

    bool DescriptorAllocator::Allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout) {
        if (m_CurrentPool == VK_NULL_HANDLE) {
            m_CurrentPool = GrabPool();
        }

        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = m_CurrentPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        VkResult result = vkAllocateDescriptorSets(m_Device, &allocInfo, set);

        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
            m_UsedPools.push_back(m_CurrentPool);
            m_CurrentPool = GrabPool();
            allocInfo.descriptorPool = m_CurrentPool;
            result = vkAllocateDescriptorSets(m_Device, &allocInfo, set);
        }

        return result == VK_SUCCESS;
    }

    DescriptorBuilder& DescriptorBuilder::BindBuffer(u32 binding, VkDescriptorBufferInfo* bufferInfo, VkDescriptorType type, VkShaderStageFlags stageFlags) {
        VkDescriptorSetLayoutBinding newBinding{};
        newBinding.binding = binding;
        newBinding.descriptorCount = 1;
        newBinding.descriptorType = type;
        newBinding.pImmutableSamplers = nullptr;
        newBinding.stageFlags = stageFlags;

        m_Bindings.push_back(newBinding);

        VkWriteDescriptorSet newWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        newWrite.dstBinding = binding;
        newWrite.dstArrayElement = 0;
        newWrite.descriptorType = type;
        newWrite.descriptorCount = 1;
        newWrite.pBufferInfo = bufferInfo;

        m_Writes.push_back(newWrite);
        return *this;
    }

    DescriptorBuilder& DescriptorBuilder::BindImage(u32 binding, VkDescriptorImageInfo* imageInfo, VkDescriptorType type, VkShaderStageFlags stageFlags) {
        VkDescriptorSetLayoutBinding newBinding{};
        newBinding.binding = binding;
        newBinding.descriptorCount = 1;
        newBinding.descriptorType = type;
        newBinding.pImmutableSamplers = nullptr;
        newBinding.stageFlags = stageFlags;

        m_Bindings.push_back(newBinding);

        VkWriteDescriptorSet newWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        newWrite.dstBinding = binding;
        newWrite.dstArrayElement = 0;
        newWrite.descriptorType = type;
        newWrite.descriptorCount = 1;
        newWrite.pImageInfo = imageInfo;

        m_Writes.push_back(newWrite);
        return *this;
    }

    bool DescriptorBuilder::Build(VkDevice device, VkDescriptorSet& set, VkDescriptorSetLayout& layout) {
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<uint32_t>(m_Bindings.size());
        layoutInfo.pBindings = m_Bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
            return false;
        }

        if (!m_Allocator->Allocate(&set, layout)) {
            return false;
        }

        for (VkWriteDescriptorSet& w : m_Writes) {
            w.dstSet = set;
        }
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(m_Writes.size()), m_Writes.data(), 0, nullptr);

        return true;
    }
}
