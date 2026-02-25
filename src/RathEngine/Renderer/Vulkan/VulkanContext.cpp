#include "RathEngine/Renderer/Vulkan/VulkanContext.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "RathEngine/Platform/IWindow.h"

#include <stdexcept>
#include <iostream>

namespace Rath::RHI {

    void VulkanContext::Init(IWindow* window) {
        GLFWwindow* gw = static_cast<GLFWwindow*>(window->GetNativeHandle());

        CreateInstance(gw);

        if (glfwCreateWindowSurface(m_Instance, gw, nullptr, &m_Surface) != VK_SUCCESS)
            throw std::runtime_error("[Vulkan] Surface creation failed!");

        PickPhysicalDevice();
        CreateLogicalDevice();

        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.physicalDevice   = m_PhysicalDevice;
        allocatorInfo.device           = m_Device;
        allocatorInfo.instance         = m_Instance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;

        if (vmaCreateAllocator(&allocatorInfo, &m_Allocator) != VK_SUCCESS)
            throw std::runtime_error("[Vulkan] VMA initialization failed!");

        CreateSwapchain(gw);
        CreateCommandPool();
        CreateSyncObjects();
        std::cout << "[RathEngine] Vulkan context initialized.\n";
    }

    void VulkanContext::CreateInstance(GLFWwindow*) {
        VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        ai.pApplicationName = "RathEngine";
        ai.apiVersion       = VK_API_VERSION_1_2;

        u32          extCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);

        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo        = &ai;
        ci.enabledExtensionCount   = extCount;
        ci.ppEnabledExtensionNames = glfwExts;

        if (vkCreateInstance(&ci, nullptr, &m_Instance) != VK_SUCCESS)
            throw std::runtime_error("[Vulkan] Instance creation failed!");
    }

    void VulkanContext::PickPhysicalDevice() {
        u32 count = 0;
        vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
        if (!count) throw std::runtime_error("[Vulkan] No GPU found!");

        std::vector<VkPhysicalDevice> devs(count);
        vkEnumeratePhysicalDevices(m_Instance, &count, devs.data());
        m_PhysicalDevice = devs[0];
    }

    void VulkanContext::CreateLogicalDevice() {
        u32 qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &qfCount, qfs.data());

        for (u32 i = 0; i < qfCount; ++i) {
            VkBool32 present = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, i, m_Surface, &present);
            if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                m_QueueFamily = i;
                break;
            }
        }

        f32 priority = 1.0f;

        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = m_QueueFamily;
        qci.queueCount       = 1;
        qci.pQueuePriorities = &priority;

        const char* exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.pQueueCreateInfos       = &qci;
        dci.queueCreateInfoCount    = 1;
        dci.enabledExtensionCount   = 1;
        dci.ppEnabledExtensionNames = exts;

        if (vkCreateDevice(m_PhysicalDevice, &dci, nullptr, &m_Device) != VK_SUCCESS)
            throw std::runtime_error("[Vulkan] Logical device creation failed!");

        vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
    }

    void VulkanContext::CreateSwapchain(GLFWwindow* window) {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        m_Extent = { static_cast<u32>(w), static_cast<u32>(h) };

        VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sci.surface          = m_Surface;
        sci.minImageCount    = 2;
        sci.imageFormat      = VK_FORMAT_B8G8R8A8_SRGB;
        sci.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        sci.imageExtent      = m_Extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped          = VK_TRUE;

        if (vkCreateSwapchainKHR(m_Device, &sci, nullptr, &m_Swapchain) != VK_SUCCESS)
            throw std::runtime_error("[Vulkan] Swapchain creation failed!");

        u32 imgCount = 0;
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imgCount, nullptr);
        m_SwapchainImages.resize(imgCount);
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imgCount, m_SwapchainImages.data());
    }

    void VulkanContext::CreateCommandPool() {
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = m_QueueFamily;
        vkCreateCommandPool(m_Device, &pi, nullptr, &m_CommandPool);

        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool        = m_CommandPool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_Device, &ai, &m_CommandBuffer);
    }

    void VulkanContext::CreateSyncObjects() {
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo     fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        vkCreateSemaphore(m_Device, &si, nullptr, &m_ImageAvailable);
        vkCreateSemaphore(m_Device, &si, nullptr, &m_RenderFinished);
        vkCreateFence    (m_Device, &fi, nullptr, &m_InFlightFence);
    }

    bool VulkanContext::BeginFrame() {
        vkWaitForFences(m_Device, 1, &m_InFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences  (m_Device, 1, &m_InFlightFence);

        VkResult r = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX,
                                           m_ImageAvailable, VK_NULL_HANDLE, &m_ImageIndex);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) return false;

        vkResetCommandBuffer(m_CommandBuffer, 0);

        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(m_CommandBuffer, &bi);
        return true;
    }

    void VulkanContext::ClearColor(const RHI::ClearColor& col) {
        VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = m_SwapchainImages[m_ImageIndex];
        b.subresourceRange    = range;
        b.srcAccessMask       = 0;
        b.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(m_CommandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);

        VkClearColorValue cv{{ col.r, col.g, col.b, col.a }};
        vkCmdClearColorImage(m_CommandBuffer, m_SwapchainImages[m_ImageIndex],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &range);

        b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = 0;

        vkCmdPipelineBarrier(m_CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    }

    void VulkanContext::EndFrame() {
        vkEndCommandBuffer(m_CommandBuffer);

        VkSemaphore          waitSems[]   = { m_ImageAvailable };
        VkSemaphore          signalSems[] = { m_RenderFinished };
        VkPipelineStageFlags stages[]     = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount   = 1; si.pWaitSemaphores   = waitSems;
        si.pWaitDstStageMask    = stages;
        si.commandBufferCount   = 1; si.pCommandBuffers   = &m_CommandBuffer;
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = signalSems;
        vkQueueSubmit(m_Queue, 1, &si, m_InFlightFence);

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = signalSems;
        pi.swapchainCount     = 1; pi.pSwapchains     = &m_Swapchain;
        pi.pImageIndices      = &m_ImageIndex;
        vkQueuePresentKHR(m_Queue, &pi);
    }

    void VulkanContext::Shutdown() {
        vkDeviceWaitIdle(m_Device);

        for (u32 i = 0; i < static_cast<u32>(m_Buffers.size()); ++i) {
            InternalBuffer& ib = m_Buffers[i];
            if (ib.buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(m_Allocator, ib.buffer, ib.allocation);
            }
        }
        vmaDestroyAllocator(m_Allocator);

        vkDestroySemaphore   (m_Device, m_RenderFinished,  nullptr);
        vkDestroySemaphore   (m_Device, m_ImageAvailable,  nullptr);
        vkDestroyFence       (m_Device, m_InFlightFence,   nullptr);
        vkDestroyCommandPool (m_Device, m_CommandPool,     nullptr);
        vkDestroySwapchainKHR(m_Device, m_Swapchain,       nullptr);
        vkDestroyDevice      (m_Device,                    nullptr);
        vkDestroySurfaceKHR  (m_Instance, m_Surface,       nullptr);
        vkDestroyInstance    (m_Instance,                  nullptr);
        std::cout << "[RathEngine] Vulkan context shut down.\n";
    }

    BufferHandle VulkanContext::CreateBuffer(const BufferDesc& desc) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size        = desc.size;
        bufferInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = desc.deviceLocal ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU;

        VkBuffer      buffer     = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        if (vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS)
            throw std::runtime_error("[Vulkan] Failed to create GPU buffer!");

        u32 index = UINT32_MAX;
        for (u32 i = 0; i < static_cast<u32>(m_Buffers.size()); ++i) {
            if (m_Buffers[i].buffer == VK_NULL_HANDLE) {
                index = i;
                break;
            }
        }

        if (index == UINT32_MAX) {
            index = static_cast<u32>(m_Buffers.size());
            InternalBuffer ib{};
            ib.buffer     = buffer;
            ib.allocation = allocation;
            ib.generation = 1;
            m_Buffers.push_back(ib);
        } else {
            m_Buffers[index].buffer     = buffer;
            m_Buffers[index].allocation = allocation;
            m_Buffers[index].generation++;
        }

        BufferHandle handle;
        handle.index      = index;
        handle.generation = m_Buffers[index].generation;
        return handle;
    }

    void VulkanContext::DestroyBuffer(BufferHandle handle) {
        if (!handle.IsValid() || handle.index >= static_cast<u32>(m_Buffers.size())) return;

        InternalBuffer& ib = m_Buffers[handle.index];

        if (ib.generation != handle.generation || ib.buffer == VK_NULL_HANDLE) return;

        vmaDestroyBuffer(m_Allocator, ib.buffer, ib.allocation);

        ib.buffer     = VK_NULL_HANDLE;
        ib.allocation = VK_NULL_HANDLE;
    }

    TextureHandle VulkanContext::CreateTexture(const TextureDesc&) { return {}; }
    void          VulkanContext::DestroyTexture(TextureHandle)      {}

} // namespace Rath::RHI
