#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "RathEngine/Core/Types.h"
#include <vector>

namespace Rath {
    class IWindow;
}

namespace Rath::RHI {
    class VulkanSwapchain {
    public:
        void Init(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VmaAllocator allocator, IWindow* window);
        void Shutdown();
        void Recreate();

        [[nodiscard]] VkSwapchainKHR GetHandle() const { return m_Swapchain; }
        [[nodiscard]] VkFormat GetImageFormat() const { return m_SwapchainFormat; }
        [[nodiscard]] VkFormat GetDepthFormat() const { return m_DepthFormat; }
        [[nodiscard]] VkExtent2D GetExtent() const { return m_Extent; }
        [[nodiscard]] u32 GetImageCount() const { return static_cast<u32>(m_Images.size()); }
        [[nodiscard]] VkImageView GetImageView(u32 index) const { return m_ImageViews[index]; }
        [[nodiscard]] VkImageView GetDepthImageView() const { return m_DepthImageView; }

    private:
        void CreateSwapchain();
        void CreateImageViews();
        void CreateDepthResources();
        VkFormat FindDepthFormat();

        VkPhysicalDevice m_PhysicalDevice{VK_NULL_HANDLE};
        VkDevice m_Device{VK_NULL_HANDLE};
        VkSurfaceKHR m_Surface{VK_NULL_HANDLE};
        VmaAllocator m_Allocator{VK_NULL_HANDLE};
        IWindow* m_Window{nullptr};

        VkSwapchainKHR m_Swapchain{VK_NULL_HANDLE};
        VkFormat m_SwapchainFormat;
        VkFormat m_DepthFormat;
        VkExtent2D m_Extent;

        std::vector<VkImage> m_Images;
        std::vector<VkImageView> m_ImageViews;

        VkImage m_DepthImage{VK_NULL_HANDLE};
        VmaAllocation m_DepthAllocation{VK_NULL_HANDLE};
        VkImageView m_DepthImageView{VK_NULL_HANDLE};
    };
}
