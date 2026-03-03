#include "RathEngine/Renderer/Vulkan/VulkanSwapchain.h"
#include "RathEngine/Platform/IWindow.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <algorithm>

namespace Rath::RHI {
    void VulkanSwapchain::Init(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VmaAllocator allocator, IWindow* window) {
        m_PhysicalDevice = physicalDevice;
        m_Device = device;
        m_Surface = surface;
        m_Allocator = allocator;
        m_Window = window;

        CreateSwapchain();
        CreateImageViews();
        CreateDepthResources();
    }

    void VulkanSwapchain::Shutdown() {
        if (m_DepthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, m_DepthImageView, nullptr);
            vmaDestroyImage(m_Allocator, m_DepthImage, m_DepthAllocation);
            m_DepthImageView = VK_NULL_HANDLE;
            m_DepthImage = VK_NULL_HANDLE;
        }
        for (auto iv : m_ImageViews) {
            vkDestroyImageView(m_Device, iv, nullptr);
        }
        m_ImageViews.clear();
        
        if (m_Swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }
    }

    void VulkanSwapchain::Recreate() {
        Shutdown();
        CreateSwapchain();
        CreateImageViews();
        CreateDepthResources();
    }

    void VulkanSwapchain::CreateSwapchain() {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &caps);

        u32 imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
            imageCount = caps.maxImageCount;
        }

        u32 formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());

        VkSurfaceFormatKHR chosen = formats[0];
        for (const auto& fmt : formats) {
            if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = fmt;
                break;
            }
        }
        m_SwapchainFormat = chosen.format;

        auto* gw = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
        int w, h;
        glfwGetFramebufferSize(gw, &w, &h);
        m_Extent.width = std::clamp(static_cast<u32>(w), caps.minImageExtent.width, caps.maxImageExtent.width);
        m_Extent.height = std::clamp(static_cast<u32>(h), caps.minImageExtent.height, caps.maxImageExtent.height);

        VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sci.surface = m_Surface;
        sci.minImageCount = imageCount;
        sci.imageFormat = m_SwapchainFormat;
        sci.imageColorSpace = chosen.colorSpace;
        sci.imageExtent = m_Extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform = caps.currentTransform;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(m_Device, &sci, nullptr, &m_Swapchain) != VK_SUCCESS) {
            throw std::runtime_error("Vulkan: Swapchain creation failed!");
        }

        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, nullptr);
        m_Images.resize(imageCount);
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, m_Images.data());
    }

    void VulkanSwapchain::CreateImageViews() {
        m_ImageViews.resize(m_Images.size());
        for (size_t i = 0; i < m_Images.size(); i++) {
            VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            ivci.image = m_Images[i];
            ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ivci.format = m_SwapchainFormat;
            ivci.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ivci.subresourceRange.baseMipLevel = 0;
            ivci.subresourceRange.levelCount = 1;
            ivci.subresourceRange.baseArrayLayer = 0;
            ivci.subresourceRange.layerCount = 1;

            if (vkCreateImageView(m_Device, &ivci, nullptr, &m_ImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("Vulkan: Image view creation failed!");
            }
        }
    }

    VkFormat VulkanSwapchain::FindDepthFormat() {
        VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
        for (VkFormat format : candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                return format;
            }
        }
        throw std::runtime_error("Vulkan: Failed to find supported depth format!");
    }

    void VulkanSwapchain::CreateDepthResources() {
        m_DepthFormat = FindDepthFormat();

        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_Extent.width;
        imageInfo.extent.height = m_Extent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_DepthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &m_DepthImage, &m_DepthAllocation, nullptr) != VK_SUCCESS) {
             throw std::runtime_error("Vulkan: Failed to create depth image!");
        }

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = m_DepthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_DepthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS) {
             throw std::runtime_error("Vulkan: Failed to create depth image view!");
        }
    }
}
