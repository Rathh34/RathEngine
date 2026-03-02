#pragma once
#include "RathEngine/Renderer/RHI/IRHIContext.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
#include <vector>

namespace Rath::RHI {
    class VulkanContext final : public IRHIContext {
    public:
        void Init    (IWindow* window) override;
        void Shutdown()                override;

        [[nodiscard]] bool BeginFrame()                       override;
        void               EndFrame()                         override;
        void               BeginPass(const ClearValue& clear) override;
        void               EndPass()                          override;
        void               Draw(u32 vertexCount)              override;

        BufferHandle  CreateBuffer  (const BufferDesc&)   override;
        void          DestroyBuffer (BufferHandle handle) override;
        TextureHandle CreateTexture (const TextureDesc&)  override;
        void          DestroyTexture(TextureHandle handle)override;

    private:
        void CreateInstance     (GLFWwindow* w);
        void PickPhysicalDevice ();
        void CreateLogicalDevice();
        void CreateSwapchain    (GLFWwindow* w);
        void CreateImageViews   ();
        void CreateRenderPass   ();
        void CreateFramebuffers ();
        void CreateCommandPool  ();
        void CreateSyncObjects  ();
        void CreatePipeline     ();
        VkShaderModule LoadShaderModule(const char* path);

        VkInstance       m_Instance       = VK_NULL_HANDLE;
        VkSurfaceKHR     m_Surface        = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice         m_Device         = VK_NULL_HANDLE;
        VkQueue          m_Queue          = VK_NULL_HANDLE;
        u32              m_QueueFamily    = 0;

        VkSwapchainKHR             m_Swapchain       = VK_NULL_HANDLE;
        VkFormat                   m_SwapchainFormat = VK_FORMAT_UNDEFINED;
        std::vector<VkImage>       m_SwapchainImages;
        std::vector<VkImageView>   m_ImageViews;
        std::vector<VkFramebuffer> m_Framebuffers;
        VkExtent2D                 m_Extent{};
        u32                        m_ImageIndex = 0;

        VkRenderPass     m_RenderPass     = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline       m_Pipeline       = VK_NULL_HANDLE;

        VkCommandPool   m_CommandPool    = VK_NULL_HANDLE;
        VkCommandBuffer m_CommandBuffer  = VK_NULL_HANDLE;
        VkSemaphore     m_ImageAvailable = VK_NULL_HANDLE;
        VkSemaphore     m_RenderFinished = VK_NULL_HANDLE;
        VkFence         m_InFlightFence  = VK_NULL_HANDLE;

        VmaAllocator m_Allocator = VK_NULL_HANDLE;
        struct InternalBuffer {
            VkBuffer      buffer     = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            u32           generation = 0;
        };
        std::vector<InternalBuffer> m_Buffers;
    };
}
