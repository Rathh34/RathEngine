#include "RathEngine/Renderer/Vulkan/VulkanContext.h"
#include "RathEngine/Renderer/Vulkan/VulkanPipeline.h"
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "RathEngine/Platform/IWindow.h"
#include "RathEngine/Core/Assert.h"
#include "RathEngine/Core/Config.h"
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Rath::RHI
{
    namespace
    {
#ifndef NDEBUG
        bool IsValidationLayerAvailable()
        {
            u32 count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> layers(count);
            vkEnumerateInstanceLayerProperties(&count, layers.data());
            for (const auto& p : layers)
            {
                if (std::string_view(p.layerName) == "VK_LAYER_KHRONOS_validation") return true;
            }
            return false;
        }
#endif
        constexpr u32 kMaxFramesInFlight = 2;
    }

    void VulkanContext::Init(IWindow* window)
    {
        auto* gw = static_cast<GLFWwindow*>(window->GetNativeHandle());

        CreateInstance(gw);
        if (glfwCreateWindowSurface(m_Instance, gw, nullptr, &m_Surface) != VK_SUCCESS) throw std::runtime_error(
            "Vulkan: Surface failed!");

        PickPhysicalDevice();
        CreateLogicalDevice();

        VmaAllocatorCreateInfo vmaInfo{};
        vmaInfo.physicalDevice = m_PhysicalDevice;
        vmaInfo.device = m_Device;
        vmaInfo.instance = m_Instance;
        vmaInfo.vulkanApiVersion = VK_API_VERSION_1_2;
        vmaCreateAllocator(&vmaInfo, &m_Allocator);

        m_Swapchain = std::make_unique<VulkanSwapchain>();
        m_Swapchain->Init(m_PhysicalDevice, m_Device, m_Surface, m_Allocator, window);

        m_DescriptorAllocator.Init(m_Device);

        CreateRenderPass();
        CreateCommandPool();
        CreateSyncObjects();
        CreateFramebuffers();
        CreateDescriptors();
        CreatePipeline();

        m_ResourceManager.Init(m_Device, m_Allocator, &m_DescriptorAllocator,
                               [this](std::function<void(VkCommandBuffer)>&& fn) { ImmediateSubmit(std::move(fn)); });

        InitImGui(gw);
    }

    void VulkanContext::Shutdown()
    {
        vkDeviceWaitIdle(m_Device);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(m_Device, m_ImGuiPool, nullptr);

        m_ResourceManager.Cleanup();

        vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_Device, m_GlobalSetLayout, nullptr);

        m_DescriptorAllocator.Cleanup();

        for (VkFramebuffer fb : m_Framebuffers) vkDestroyFramebuffer(m_Device, fb, nullptr);
        vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);

        m_Swapchain->Shutdown();
        m_Swapchain.reset();
        vmaDestroyAllocator(m_Allocator);

        for (size_t i = 0; i < kMaxFramesInFlight; ++i)
        {
            vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
            vkDestroyFence(m_Device, m_InFlightFences[i], nullptr);
        }
        for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); ++i)
        {
            vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
        }

        vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        vkDestroyFence(m_Device, m_UploadFence, nullptr);
        vkDestroyCommandPool(m_Device, m_UploadCommandPool, nullptr);

        vkDestroyDevice(m_Device, nullptr);
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        vkDestroyInstance(m_Instance, nullptr);
    }

    void VulkanContext::WaitIdle() { vkDeviceWaitIdle(m_Device); }

    void VulkanContext::CreateInstance(GLFWwindow*)
    {
        VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        ai.pApplicationName = "RathEngine";
        ai.apiVersion = VK_API_VERSION_1_2;

        u32 extCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &ai;
        ci.enabledExtensionCount = extCount;
        ci.ppEnabledExtensionNames = glfwExts;

#ifndef NDEBUG
        static constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
        if (IsValidationLayerAvailable())
        {
            ci.enabledLayerCount = 1;
            ci.ppEnabledLayerNames = &kValidationLayer;
        }
#endif
        vkCreateInstance(&ci, nullptr, &m_Instance);
    }

    void VulkanContext::PickPhysicalDevice()
    {
        u32 count = 0;
        vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());
        for (VkPhysicalDevice dev : devices)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                m_PhysicalDevice = dev;
                return;
            }
        }
        m_PhysicalDevice = devices[0];
    }

    void VulkanContext::CreateLogicalDevice()
    {
        u32 qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &qfCount, families.data());

        for (u32 i = 0; i < qfCount; ++i)
        {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, i, m_Surface, &present);
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && present)
            {
                m_QueueFamily = i;
                break;
            }
        }

        f32 kPriority = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = m_QueueFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &kPriority;

        const char* kExts = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.pQueueCreateInfos = &qci;
        dci.queueCreateInfoCount = 1;
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = &kExts;

        vkCreateDevice(m_PhysicalDevice, &dci, nullptr, &m_Device);
        vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
    }

    void VulkanContext::CreateRenderPass()
    {
        VkAttachmentDescription color{};
        color.format = m_Swapchain->GetImageFormat();
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depth{};
        depth.format = m_Swapchain->GetDepthFormat();
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkAttachmentDescription attachments[2] = {color, depth};
        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 2;
        rpci.pAttachments = attachments;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass;
        rpci.dependencyCount = 1;
        rpci.pDependencies = &dep;
        vkCreateRenderPass(m_Device, &rpci, nullptr, &m_RenderPass);
    }

    void VulkanContext::CreateFramebuffers()
    {
        m_Framebuffers.resize(m_Swapchain->GetImageCount());
        for (size_t i = 0; i < m_Swapchain->GetImageCount(); ++i)
        {
            VkImageView attachments[2] = {m_Swapchain->GetImageView(i), m_Swapchain->GetDepthImageView()};
            VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbci.renderPass = m_RenderPass;
            fbci.attachmentCount = 2;
            fbci.pAttachments = attachments;
            fbci.width = m_Swapchain->GetExtent().width;
            fbci.height = m_Swapchain->GetExtent().height;
            fbci.layers = 1;
            vkCreateFramebuffer(m_Device, &fbci, nullptr, &m_Framebuffers[i]);
        }
    }

    void VulkanContext::CreateDescriptors()
    {
        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 0;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &samplerLayoutBinding;
        vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_GlobalSetLayout);
    }

    void VulkanContext::CreatePipeline()
    {
        VulkanPipelineBuilder builder;
        VkShaderModule vertMod = builder.LoadShaderModule(m_Device, RATH_SHADER_DIR "triangle.vert.spv");
        VkShaderModule fragMod = builder.LoadShaderModule(m_Device, RATH_SHADER_DIR "triangle.frag.spv");

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(float) * 16;

        VkPipelineLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutCI.setLayoutCount = 1;
        layoutCI.pSetLayouts = &m_GlobalSetLayout;
        layoutCI.pushConstantRangeCount = 1;
        layoutCI.pPushConstantRanges = &pushConstantRange;
        vkCreatePipelineLayout(m_Device, &layoutCI, nullptr, &m_PipelineLayout);

        m_Pipeline = builder.SetShaders(vertMod, fragMod).SetInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
                            .SetPolygonMode(VK_POLYGON_MODE_FILL).SetCullMode(
                                VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE)
                            .SetMultisamplingNone().EnableAlphaBlending().EnableDepthTest(true, VK_COMPARE_OP_LESS)
                            .SetPipelineLayout(m_PipelineLayout).Build(m_Device, m_RenderPass);

        vkDestroyShaderModule(m_Device, fragMod, nullptr);
        vkDestroyShaderModule(m_Device, vertMod, nullptr);
    }

    void VulkanContext::InitImGui(GLFWwindow* window)
    {
        VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000}};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = pool_sizes;
        vkCreateDescriptorPool(m_Device, &pool_info, nullptr, &m_ImGuiPool);

        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForVulkan(window, true);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_Instance;
        init_info.PhysicalDevice = m_PhysicalDevice;
        init_info.Device = m_Device;
        init_info.QueueFamily = m_QueueFamily;
        init_info.Queue = m_Queue;
        init_info.DescriptorPool = m_ImGuiPool;
        init_info.RenderPass = m_RenderPass;
        init_info.MinImageCount = kMaxFramesInFlight;
        init_info.ImageCount = m_Swapchain->GetImageCount();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_Init(&init_info);

        ImmediateSubmit([&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(); });
        ImGui_ImplVulkan_DestroyFontsTexture();
    }

    void VulkanContext::CreateCommandPool()
    {
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = m_QueueFamily;
        vkCreateCommandPool(m_Device, &pi, nullptr, &m_CommandPool);

        m_CommandBuffers.resize(kMaxFramesInFlight);
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = m_CommandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = kMaxFramesInFlight;
        vkAllocateCommandBuffers(m_Device, &ai, m_CommandBuffers.data());

        VkCommandPoolCreateInfo upi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        upi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        upi.queueFamilyIndex = m_QueueFamily;
        vkCreateCommandPool(m_Device, &upi, nullptr, &m_UploadCommandPool);

        VkCommandBufferAllocateInfo uai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        uai.commandPool = m_UploadCommandPool;
        uai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        uai.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_Device, &uai, &m_UploadCommandBuffer);
    }

    void VulkanContext::CreateSyncObjects()
    {
        m_ImageAvailableSemaphores.resize(kMaxFramesInFlight);
        m_InFlightFences.resize(kMaxFramesInFlight);
        m_RenderFinishedSemaphores.resize(m_Swapchain->GetImageCount());
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (size_t i = 0; i < kMaxFramesInFlight; ++i)
        {
            vkCreateSemaphore(m_Device, &si, nullptr, &m_ImageAvailableSemaphores[i]);
            vkCreateFence(m_Device, &fi, nullptr, &m_InFlightFences[i]);
        }
        for (size_t i = 0; i < m_Swapchain->GetImageCount(); ++i) vkCreateSemaphore(
            m_Device, &si, nullptr, &m_RenderFinishedSemaphores[i]);
        VkFenceCreateInfo ufi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkCreateFence(m_Device, &ufi, nullptr, &m_UploadFence);
    }

    bool VulkanContext::BeginFrame()
    {
        vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);
        if (vkAcquireNextImageKHR(m_Device, m_Swapchain->GetHandle(), UINT64_MAX,
                                  m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE,
                                  &m_ImageIndex) == VK_ERROR_OUT_OF_DATE_KHR) return false;
        vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame]);
        vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrame], 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(m_CommandBuffers[m_CurrentFrame], &bi);
        return true;
    }

    void VulkanContext::BeginPass(const ClearValue& clearColor)
    {
        VkClearValue cv[2]{};
        cv[0].color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
        cv[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpbi.renderPass = m_RenderPass;
        rpbi.framebuffer = m_Framebuffers[m_ImageIndex];
        rpbi.renderArea.offset = {0, 0};
        rpbi.renderArea.extent = m_Swapchain->GetExtent();
        rpbi.clearValueCount = 2;
        rpbi.pClearValues = cv;
        vkCmdBeginRenderPass(m_CommandBuffers[m_CurrentFrame], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    }

    void VulkanContext::EndPass() { vkCmdEndRenderPass(m_CommandBuffers[m_CurrentFrame]); }

    void VulkanContext::Draw(u32 vertexCount)
    {
        vkCmdBindPipeline(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
        VkViewport vp{
            0, 0, static_cast<float>(m_Swapchain->GetExtent().width),
            static_cast<float>(m_Swapchain->GetExtent().height), 0.0f, 1.0f
        };
        vkCmdSetViewport(m_CommandBuffers[m_CurrentFrame], 0, 1, &vp);
        const VkRect2D scissor{{0, 0}, m_Swapchain->GetExtent()};
        vkCmdSetScissor(m_CommandBuffers[m_CurrentFrame], 0, 1, &scissor);
        vkCmdDraw(m_CommandBuffers[m_CurrentFrame], vertexCount, 1, 0, 0);
    }

    void VulkanContext::DrawIndexed(u32 indexCount)
    {
        vkCmdBindPipeline(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
        VkViewport vp{
            0, 0, static_cast<float>(m_Swapchain->GetExtent().width),
            static_cast<float>(m_Swapchain->GetExtent().height), 0.0f, 1.0f
        };
        vkCmdSetViewport(m_CommandBuffers[m_CurrentFrame], 0, 1, &vp);
        const VkRect2D scissor{{0, 0}, m_Swapchain->GetExtent()};
        vkCmdSetScissor(m_CommandBuffers[m_CurrentFrame], 0, 1, &scissor);
        vkCmdDrawIndexed(m_CommandBuffers[m_CurrentFrame], indexCount, 1, 0, 0, 0);
    }

    void VulkanContext::EndFrame()
    {
        vkEndCommandBuffer(m_CommandBuffers[m_CurrentFrame]);
        constexpr VkPipelineStageFlags kWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &m_ImageAvailableSemaphores[m_CurrentFrame];
        si.pWaitDstStageMask = &kWaitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &m_CommandBuffers[m_CurrentFrame];
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &m_RenderFinishedSemaphores[m_ImageIndex];
        vkQueueSubmit(m_Queue, 1, &si, m_InFlightFences[m_CurrentFrame]);

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &m_RenderFinishedSemaphores[m_ImageIndex];
        pi.swapchainCount = 1;
        VkSwapchainKHR sc = m_Swapchain->GetHandle();
        pi.pSwapchains = &sc;
        pi.pImageIndices = &m_ImageIndex;
        vkQueuePresentKHR(m_Queue, &pi);
        m_CurrentFrame = (m_CurrentFrame + 1) % kMaxFramesInFlight;
    }

    void VulkanContext::ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
    {
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_UploadCommandBuffer, &bi);
        function(m_UploadCommandBuffer);
        vkEndCommandBuffer(m_UploadCommandBuffer);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &m_UploadCommandBuffer;
        vkQueueSubmit(m_Queue, 1, &si, m_UploadFence);
        vkWaitForFences(m_Device, 1, &m_UploadFence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_Device, 1, &m_UploadFence);
        vkResetCommandPool(m_Device, m_UploadCommandPool, 0);
    }

    // --- DELEGATE EVERYTHING TO RESOURCE MANAGER ---
    BufferHandle VulkanContext::CreateBuffer(const BufferDesc& desc) { return m_ResourceManager.CreateBuffer(desc); }
    void VulkanContext::DestroyBuffer(BufferHandle handle) { m_ResourceManager.DestroyBuffer(handle); }
    void* VulkanContext::MapBuffer(BufferHandle handle) { return m_ResourceManager.MapBuffer(handle); }
    void VulkanContext::UnmapBuffer(BufferHandle handle) { m_ResourceManager.UnmapBuffer(handle); }

    void VulkanContext::UploadBufferData(BufferHandle handle, const void* data, u64 size)
    {
        m_ResourceManager.UploadBufferData(handle, data, size);
    }

    TextureHandle VulkanContext::CreateTexture(const TextureDesc& desc)
    {
        return m_ResourceManager.CreateTexture(desc);
    }

    void VulkanContext::DestroyTexture(TextureHandle handle) { m_ResourceManager.DestroyTexture(handle); }

    void VulkanContext::BindVertexBuffer(BufferHandle handle)
    {
        VkBuffer buffers[] = {m_ResourceManager.GetBuffer(handle).buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(m_CommandBuffers[m_CurrentFrame], 0, 1, buffers, offsets);
    }

    void VulkanContext::BindIndexBuffer(BufferHandle handle)
    {
        vkCmdBindIndexBuffer(m_CommandBuffers[m_CurrentFrame], m_ResourceManager.GetBuffer(handle).buffer, 0,
                             VK_INDEX_TYPE_UINT16);
    }

    void VulkanContext::BindTexture(TextureHandle handle)
    {
        vkCmdBindDescriptorSets(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0,
                                1, &m_ResourceManager.GetTexture(handle).descriptorSet, 0, nullptr);
    }

    void VulkanContext::PushConstants(const void* data, u32 size)
    {
        vkCmdPushConstants(m_CommandBuffers[m_CurrentFrame], m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, size,
                           data);
    }

    void VulkanContext::BeginImGui()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void VulkanContext::EndImGui()
    {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_CommandBuffers[m_CurrentFrame]);
    }
}
