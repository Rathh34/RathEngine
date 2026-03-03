#include "RathEngine/Renderer/Vulkan/VulkanResourceManager.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdexcept>
#include <cstring>

namespace Rath::RHI {

    void VulkanResourceManager::Init(VkDevice device, VmaAllocator allocator, DescriptorAllocator* descAllocator, 
                                     std::function<void(std::function<void(VkCommandBuffer)>&&)> immediateSubmitFn) {
        m_Device = device;
        m_Allocator = allocator;
        m_DescriptorAllocator = descAllocator;
        m_ImmediateSubmit = immediateSubmitFn;
    }

    void VulkanResourceManager::Cleanup() {
        for (u32 i = 0; i < m_Textures.size(); ++i) {
            DestroyTexture({i, m_Textures[i].generation});
        }
        for (auto& ib : m_Buffers) {
            if (ib.buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(m_Allocator, ib.buffer, ib.allocation);
            }
        }
    }

    BufferHandle VulkanResourceManager::CreateBuffer(const BufferDesc& desc) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = desc.size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | 
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = desc.deviceLocal ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU;

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        if (vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Vulkan: Buffer creation failed!");
        }

        u32 index = UINT32_MAX;
        for (u32 i = 0; i < static_cast<u32>(m_Buffers.size()); ++i) {
            if (m_Buffers[i].buffer == VK_NULL_HANDLE) {
                index = i;
                break;
            }
        }

        if (index == UINT32_MAX) {
            index = static_cast<u32>(m_Buffers.size());
            m_Buffers.push_back({buffer, allocation, 1});
        } else {
            m_Buffers[index] = {buffer, allocation, m_Buffers[index].generation + 1};
        }
        return {index, m_Buffers[index].generation};
    }

    void VulkanResourceManager::DestroyBuffer(BufferHandle handle) {
        if (!handle.IsValid() || handle.index >= static_cast<u32>(m_Buffers.size())) return;
        InternalBuffer& ib = m_Buffers[handle.index];
        if (ib.generation != handle.generation || ib.buffer == VK_NULL_HANDLE) return;

        vmaDestroyBuffer(m_Allocator, ib.buffer, ib.allocation);
        ib.buffer = VK_NULL_HANDLE;
        ib.allocation = VK_NULL_HANDLE;
    }

    void* VulkanResourceManager::MapBuffer(BufferHandle handle) {
        void* mapped = nullptr;
        vmaMapMemory(m_Allocator, m_Buffers[handle.index].allocation, &mapped);
        return mapped;
    }

    void VulkanResourceManager::UnmapBuffer(BufferHandle handle) {
        vmaUnmapMemory(m_Allocator, m_Buffers[handle.index].allocation);
    }

    void VulkanResourceManager::UploadBufferData(BufferHandle handle, const void* data, u64 size) {
        BufferDesc stagingDesc{size, false, "Staging"};
        BufferHandle staging = CreateBuffer(stagingDesc);

        void* mapped = MapBuffer(staging);
        std::memcpy(mapped, data, size);
        UnmapBuffer(staging);

        m_ImmediateSubmit([=](VkCommandBuffer cmd) {
            VkBufferCopy copyRegion{};
            copyRegion.size = size;
            vkCmdCopyBuffer(cmd, m_Buffers[staging.index].buffer, m_Buffers[handle.index].buffer, 1, &copyRegion);
        });

        DestroyBuffer(staging);
    }

    InternalBuffer& VulkanResourceManager::GetBuffer(BufferHandle handle) {
        return m_Buffers[handle.index];
    }

    TextureHandle VulkanResourceManager::CreateTexture(const TextureDesc& desc) {
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load(desc.path, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) throw std::runtime_error("Vulkan: Failed to load image!");

        VkDeviceSize imageSize = texWidth * texHeight * 4;
        BufferDesc stagingDesc{imageSize, false, "TexStaging"};
        BufferHandle staging = CreateBuffer(stagingDesc);

        void* mapped = MapBuffer(staging);
        std::memcpy(mapped, pixels, static_cast<size_t>(imageSize));
        UnmapBuffer(staging);
        stbi_image_free(pixels);

        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = static_cast<uint32_t>(texWidth);
        imageInfo.extent.height = static_cast<uint32_t>(texHeight);
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkImage textureImage = VK_NULL_HANDLE;
        VmaAllocation textureAllocation = VK_NULL_HANDLE;
        vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &textureImage, &textureAllocation, nullptr);

        m_ImmediateSubmit([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = textureImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};

            vkCmdCopyBufferToImage(cmd, m_Buffers[staging.index].buffer, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        });

        DestroyBuffer(staging);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = textureImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView textureImageView = VK_NULL_HANDLE;
        vkCreateImageView(m_Device, &viewInfo, nullptr, &textureImageView);

        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        VkSampler textureSampler = VK_NULL_HANDLE;
        vkCreateSampler(m_Device, &samplerInfo, nullptr, &textureSampler);

        VkDescriptorImageInfo imageBufferInfo{};
        imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageBufferInfo.imageView = textureImageView;
        imageBufferInfo.sampler = textureSampler;

        VkDescriptorSet descriptorSet;
        VkDescriptorSetLayout layout;

        DescriptorBuilder(m_DescriptorAllocator)
            .BindImage(0, &imageBufferInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .Build(m_Device, descriptorSet, layout);

        u32 index = static_cast<u32>(m_Textures.size());
        m_Textures.push_back({textureImage, textureImageView, textureSampler, textureAllocation, descriptorSet, layout, 1});

        return {index, 1};
    }

    void VulkanResourceManager::DestroyTexture(TextureHandle handle) {
        if (!handle.IsValid() || handle.index >= static_cast<u32>(m_Textures.size())) return;
        InternalTexture& it = m_Textures[handle.index];
        if (it.generation != handle.generation || it.image == VK_NULL_HANDLE) return;

        vkDestroyDescriptorSetLayout(m_Device, it.layout, nullptr);
        vkDestroySampler(m_Device, it.sampler, nullptr);
        vkDestroyImageView(m_Device, it.view, nullptr);
        vmaDestroyImage(m_Allocator, it.image, it.allocation);

        it.image = VK_NULL_HANDLE;
    }

    InternalTexture& VulkanResourceManager::GetTexture(TextureHandle handle) {
        return m_Textures[handle.index];
    }
}
