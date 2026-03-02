#include "RathEngine/Renderer/Vulkan/VulkanContext.h"
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include "RathEngine/Platform/IWindow.h"
#include "RathEngine/Core/Assert.h"
#include "RathEngine/Core/Config.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <cstdio>
#include <cstring>

namespace Rath::RHI {

namespace {
#ifndef NDEBUG
    bool IsValidationLayerAvailable() {
        u32 count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> layers(count);
        vkEnumerateInstanceLayerProperties(&count, layers.data());
        for (const auto& p : layers) {
            if (std::string_view(p.layerName) == "VK_LAYER_KHRONOS_validation") return true;
        }
        return false;
    }
#endif
}

void VulkanContext::Init(IWindow* window) {
    auto* gw = static_cast<GLFWwindow*>(window->GetNativeHandle());
    CreateInstance(gw);

    if (glfwCreateWindowSurface(m_Instance, gw, nullptr, &m_Surface) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] Surface creation failed!");

    PickPhysicalDevice();
    CreateLogicalDevice();

    VmaAllocatorCreateInfo vmaInfo{};
    vmaInfo.physicalDevice   = m_PhysicalDevice;
    vmaInfo.device           = m_Device;
    vmaInfo.instance         = m_Instance;
    vmaInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    if (vmaCreateAllocator(&vmaInfo, &m_Allocator) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] VMA initialisation failed!");

    CreateSwapchain(gw);
    CreateImageViews();
    CreateRenderPass();
    CreateFramebuffers();
    CreateCommandPool();
    CreateSyncObjects();
    CreatePipeline();
}

void VulkanContext::Shutdown() {
    vkDeviceWaitIdle(m_Device);

    for (auto& ib : m_Buffers) {
        if (ib.buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(m_Allocator, ib.buffer, ib.allocation);
    }

    vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);

    for (VkFramebuffer fb : m_Framebuffers) vkDestroyFramebuffer(m_Device, fb, nullptr);
    for (VkImageView iv : m_ImageViews)     vkDestroyImageView(m_Device, iv, nullptr);

    vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
    vmaDestroyAllocator(m_Allocator);

    for (size_t i = 0; i < k_MaxFramesInFlight; i++) {
        vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_Device, m_InFlightFences[i], nullptr);
    }

    for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); i++) {
        vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
    }

    vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);

    vkDestroyFence(m_Device, m_UploadFence, nullptr);
    vkDestroyCommandPool(m_Device, m_UploadCommandPool, nullptr);

    vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
    vkDestroyDevice(m_Device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    vkDestroyInstance(m_Instance, nullptr);
}

void VulkanContext::CreateInstance(GLFWwindow*) {
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName   = "RathEngine";
    ai.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    ai.apiVersion         = VK_API_VERSION_1_2;

    u32 extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = extCount;
    ci.ppEnabledExtensionNames = glfwExts;

#ifndef NDEBUG
    static constexpr const char* k_ValidationLayer = "VK_LAYER_KHRONOS_validation";
    if (IsValidationLayerAvailable()) {
        ci.enabledLayerCount   = 1;
        ci.ppEnabledLayerNames = &k_ValidationLayer;
    }
#endif

    if (vkCreateInstance(&ci, nullptr, &m_Instance) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] Instance creation failed!");
}

void VulkanContext::PickPhysicalDevice() {
    u32 count = 0;
    vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
    RATH_ASSERT(count > 0, "No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());

    for (VkPhysicalDevice dev : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_PhysicalDevice = dev;
            return;
        }
    }
    m_PhysicalDevice = devices[0];
}

void VulkanContext::CreateLogicalDevice() {
    u32 qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &qfCount, families.data());

    for (u32 i = 0; i < qfCount; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, i, m_Surface, &present);
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { m_QueueFamily = i; break; }
    }

    constexpr f32 k_Priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = m_QueueFamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &k_Priority;

    constexpr const char* k_Exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pQueueCreateInfos       = &qci;
    dci.queueCreateInfoCount    = 1;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = k_Exts;

    if (vkCreateDevice(m_PhysicalDevice, &dci, nullptr, &m_Device) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] Logical device creation failed!");
    vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
}

void VulkanContext::CreateSwapchain(GLFWwindow* window) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &caps);

    u32 imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    u32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = fmt; break;
        }
    }
    m_SwapchainFormat = chosen.format;

    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    m_Extent.width  = std::clamp(static_cast<u32>(w), caps.minImageExtent.width,  caps.maxImageExtent.width);
    m_Extent.height = std::clamp(static_cast<u32>(h), caps.minImageExtent.height, caps.maxImageExtent.height);

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface          = m_Surface;
    sci.minImageCount    = imageCount;
    sci.imageFormat      = m_SwapchainFormat;
    sci.imageColorSpace  = chosen.colorSpace;
    sci.imageExtent      = m_Extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
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

void VulkanContext::CreateImageViews() {
    m_ImageViews.resize(m_SwapchainImages.size());
    for (size_t i = 0; i < m_SwapchainImages.size(); ++i) {
        VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ivci.image            = m_SwapchainImages[i];
        ivci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format           = m_SwapchainFormat;
        ivci.components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_Device, &ivci, nullptr, &m_ImageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("[Vulkan] Image view creation failed!");
    }
}

void VulkanContext::CreateRenderPass() {
    VkAttachmentDescription color{};
    color.format         = m_SwapchainFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;     rpci.pAttachments  = &color;
    rpci.subpassCount    = 1;     rpci.pSubpasses    = &subpass;
    rpci.dependencyCount = 1;     rpci.pDependencies = &dep;

    if (vkCreateRenderPass(m_Device, &rpci, nullptr, &m_RenderPass) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] Render pass creation failed!");
}

void VulkanContext::CreateFramebuffers() {
    m_Framebuffers.resize(m_SwapchainImages.size());
    for (size_t i = 0; i < m_SwapchainImages.size(); ++i) {
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass      = m_RenderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &m_ImageViews[i];
        fbci.width           = m_Extent.width;
        fbci.height          = m_Extent.height;
        fbci.layers          = 1;
        if (vkCreateFramebuffer(m_Device, &fbci, nullptr, &m_Framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("[Vulkan] Framebuffer creation failed!");
    }
}

VkShaderModule VulkanContext::LoadShaderModule(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) throw std::runtime_error(std::string("[Vulkan] Cannot open shader: ") + path);
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<char> code(static_cast<size_t>(size));
    fread(code.data(), 1, static_cast<size_t>(size), f);
    fclose(f);

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = static_cast<size_t>(size);
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_Device, &ci, nullptr, &mod) != VK_SUCCESS)
        throw std::runtime_error(std::string("[Vulkan] Shader module failed: ") + path);
    return mod;
}

void VulkanContext::CreatePipeline() {
    VkShaderModule vertMod = LoadShaderModule(RATH_SHADER_DIR "triangle.vert.spv");
    VkShaderModule fragMod = LoadShaderModule(RATH_SHADER_DIR "triangle.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertMod; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragMod; stages[1].pName = "main";

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(f32) * 5;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2]{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(f32) * 2;

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    constexpr VkDynamicState k_DynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2; dynState.pDynamicStates = k_DynStates;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1; viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = VK_CULL_MODE_NONE;
    rasterizer.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAtt.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1; colorBlend.pAttachments = &blendAtt;

    VkPipelineLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    vkCreatePipelineLayout(m_Device, &layoutCI, nullptr, &m_PipelineLayout);

    VkGraphicsPipelineCreateInfo pipelineCI{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineCI.stageCount = 2;            pipelineCI.pStages             = stages;
    pipelineCI.pVertexInputState = &vertexInput; pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState = &viewportState;   pipelineCI.pRasterizationState = &rasterizer;
    pipelineCI.pMultisampleState = &multisampling; pipelineCI.pColorBlendState   = &colorBlend;
    pipelineCI.pDynamicState = &dynState;         pipelineCI.layout              = m_PipelineLayout;
    pipelineCI.renderPass = m_RenderPass;         pipelineCI.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_Pipeline) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] Pipeline creation failed!");

    vkDestroyShaderModule(m_Device, fragMod, nullptr);
    vkDestroyShaderModule(m_Device, vertMod, nullptr);
}

void VulkanContext::CreateCommandPool() {
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = m_QueueFamily;
    vkCreateCommandPool(m_Device, &pi, nullptr, &m_CommandPool);

    m_CommandBuffers.resize(k_MaxFramesInFlight);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = m_CommandPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = k_MaxFramesInFlight;
    vkAllocateCommandBuffers(m_Device, &ai, m_CommandBuffers.data());

    VkCommandPoolCreateInfo upi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    upi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    upi.queueFamilyIndex = m_QueueFamily;
    vkCreateCommandPool(m_Device, &upi, nullptr, &m_UploadCommandPool);

    VkCommandBufferAllocateInfo uai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    uai.commandPool        = m_UploadCommandPool;
    uai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    uai.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_Device, &uai, &m_UploadCommandBuffer);
}

void VulkanContext::CreateSyncObjects() {
    m_ImageAvailableSemaphores.resize(k_MaxFramesInFlight);
    m_InFlightFences.resize(k_MaxFramesInFlight);
    m_RenderFinishedSemaphores.resize(m_SwapchainImages.size());

    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo     fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < k_MaxFramesInFlight; i++) {
        vkCreateSemaphore(m_Device, &si, nullptr, &m_ImageAvailableSemaphores[i]);
        vkCreateFence(m_Device, &fi, nullptr, &m_InFlightFences[i]);
    }

    for (size_t i = 0; i < m_SwapchainImages.size(); i++) {
        vkCreateSemaphore(m_Device, &si, nullptr, &m_RenderFinishedSemaphores[i]);
    }

    VkFenceCreateInfo ufi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(m_Device, &ufi, nullptr, &m_UploadFence);
}

bool VulkanContext::BeginFrame() {
    vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);
    const VkResult r = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &m_ImageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) return false;

    vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame]);
    vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrame], 0);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(m_CommandBuffers[m_CurrentFrame], &bi);
    return true;
}

void VulkanContext::BeginPass(const ClearValue& clearColor) {
    VkClearValue cv{}; cv.color = {{ clearColor.r, clearColor.g, clearColor.b, clearColor.a }};
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass        = m_RenderPass;
    rpbi.framebuffer       = m_Framebuffers[m_ImageIndex];
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = m_Extent;
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &cv;
    vkCmdBeginRenderPass(m_CommandBuffers[m_CurrentFrame], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanContext::EndPass() { vkCmdEndRenderPass(m_CommandBuffers[m_CurrentFrame]); }

void VulkanContext::Draw(u32 vertexCount) {
    vkCmdBindPipeline(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    VkViewport vp{0, 0, static_cast<float>(m_Extent.width), static_cast<float>(m_Extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(m_CommandBuffers[m_CurrentFrame], 0, 1, &vp);
    const VkRect2D scissor{{0, 0}, m_Extent};
    vkCmdSetScissor(m_CommandBuffers[m_CurrentFrame], 0, 1, &scissor);
    vkCmdDraw(m_CommandBuffers[m_CurrentFrame], vertexCount, 1, 0, 0);
}

void VulkanContext::DrawIndexed(u32 indexCount) {
    vkCmdBindPipeline(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    VkViewport vp{0, 0, static_cast<float>(m_Extent.width), static_cast<float>(m_Extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(m_CommandBuffers[m_CurrentFrame], 0, 1, &vp);
    const VkRect2D scissor{{0, 0}, m_Extent};
    vkCmdSetScissor(m_CommandBuffers[m_CurrentFrame], 0, 1, &scissor);
    vkCmdDrawIndexed(m_CommandBuffers[m_CurrentFrame], indexCount, 1, 0, 0, 0);
}

void VulkanContext::EndFrame() {
    vkEndCommandBuffer(m_CommandBuffers[m_CurrentFrame]);
    constexpr VkPipelineStageFlags k_WaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = &m_ImageAvailableSemaphores[m_CurrentFrame];
    si.pWaitDstStageMask = &k_WaitStage;
    si.commandBufferCount = 1; si.pCommandBuffers = &m_CommandBuffers[m_CurrentFrame];
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &m_RenderFinishedSemaphores[m_ImageIndex];

    vkQueueSubmit(m_Queue, 1, &si, m_InFlightFences[m_CurrentFrame]);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &m_RenderFinishedSemaphores[m_ImageIndex];
    pi.swapchainCount = 1; pi.pSwapchains = &m_Swapchain; pi.pImageIndices = &m_ImageIndex;

    vkQueuePresentKHR(m_Queue, &pi);

    m_CurrentFrame = (m_CurrentFrame + 1) % k_MaxFramesInFlight;
}

void VulkanContext::ImmediateSubmit(std::function<void(VkCommandBuffer)>&& function) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_UploadCommandBuffer, &bi);
    function(m_UploadCommandBuffer);
    vkEndCommandBuffer(m_UploadCommandBuffer);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &m_UploadCommandBuffer;

    vkQueueSubmit(m_Queue, 1, &si, m_UploadFence);
    vkWaitForFences(m_Device, 1, &m_UploadFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_Device, 1, &m_UploadFence);
    vkResetCommandPool(m_Device, m_UploadCommandPool, 0);
}

BufferHandle VulkanContext::CreateBuffer(const BufferDesc& desc) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = desc.size;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = desc.deviceLocal ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkBuffer buffer = VK_NULL_HANDLE; VmaAllocation allocation = VK_NULL_HANDLE;
    if (vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] Buffer creation failed!");

    u32 index = UINT32_MAX;
    for (u32 i = 0; i < static_cast<u32>(m_Buffers.size()); ++i) {
        if (m_Buffers[i].buffer == VK_NULL_HANDLE) { index = i; break; }
    }
    if (index == UINT32_MAX) {
        index = static_cast<u32>(m_Buffers.size());
        m_Buffers.push_back({buffer, allocation, 1});
    } else m_Buffers[index] = {buffer, allocation, m_Buffers[index].generation + 1};
    return {index, m_Buffers[index].generation};
}

void VulkanContext::DestroyBuffer(BufferHandle handle) {
    if (!handle.IsValid() || handle.index >= static_cast<u32>(m_Buffers.size())) return;
    InternalBuffer& ib = m_Buffers[handle.index];
    if (ib.generation != handle.generation || ib.buffer == VK_NULL_HANDLE) return;
    vmaDestroyBuffer(m_Allocator, ib.buffer, ib.allocation);
    ib.buffer = VK_NULL_HANDLE; ib.allocation = VK_NULL_HANDLE;
}

void* VulkanContext::MapBuffer(BufferHandle handle) {
    void* mapped = nullptr;
    vmaMapMemory(m_Allocator, m_Buffers[handle.index].allocation, &mapped);
    return mapped;
}

void VulkanContext::UnmapBuffer(BufferHandle handle) {
    vmaUnmapMemory(m_Allocator, m_Buffers[handle.index].allocation);
}

void VulkanContext::UploadBufferData(BufferHandle handle, const void* data, u64 size) {
    BufferDesc stagingDesc{size, false, "Staging"};
    BufferHandle staging = CreateBuffer(stagingDesc);

    void* mapped = MapBuffer(staging);
    std::memcpy(mapped, data, size);
    UnmapBuffer(staging);

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, m_Buffers[staging.index].buffer, m_Buffers[handle.index].buffer, 1, &copyRegion);
    });

    DestroyBuffer(staging);
}

void VulkanContext::BindVertexBuffer(BufferHandle handle) {
    VkBuffer buffers[] = { m_Buffers[handle.index].buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(m_CommandBuffers[m_CurrentFrame], 0, 1, buffers, offsets);
}

void VulkanContext::BindIndexBuffer(BufferHandle handle) {
    vkCmdBindIndexBuffer(m_CommandBuffers[m_CurrentFrame], m_Buffers[handle.index].buffer, 0, VK_INDEX_TYPE_UINT16);
}

TextureHandle VulkanContext::CreateTexture(const TextureDesc&) { return {}; }
void VulkanContext::DestroyTexture(TextureHandle) {}

}
