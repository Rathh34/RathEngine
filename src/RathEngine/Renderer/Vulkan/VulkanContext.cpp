#include "RathEngine/Renderer/Vulkan/VulkanContext.h"
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "RathEngine/Platform/IWindow.h"
#include "RathEngine/Core/Assert.h"
#include "RathEngine/Core/Config.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <vector>
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
    CreateDepthResources();
    CreateRenderPass();
    CreateFramebuffers();
    CreateCommandPool();
    CreateSyncObjects();
    CreateDescriptors();
    CreatePipeline();
    InitImGui(gw);
}

void VulkanContext::Shutdown() {
    vkDeviceWaitIdle(m_Device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(m_Device, m_ImGuiPool, nullptr);

    for (u32 i = 0; i < m_Textures.size(); ++i) {
        DestroyTexture({i, m_Textures[i].generation});
    }

    for (auto& ib : m_Buffers) {
        if (ib.buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(m_Allocator, ib.buffer, ib.allocation);
    }

    vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, m_GlobalSetLayout, nullptr);
    vkDestroyDescriptorPool(m_Device, m_EnginePool, nullptr);

    for (VkFramebuffer fb : m_Framebuffers) vkDestroyFramebuffer(m_Device, fb, nullptr);

    vkDestroyImageView(m_Device, m_DepthImageView, nullptr);
    vmaDestroyImage(m_Allocator, m_DepthImage, m_DepthAllocation);

    for (VkImageView iv : m_ImageViews) vkDestroyImageView(m_Device, iv, nullptr);

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

    void VulkanContext::WaitIdle() {
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }
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

VkFormat VulkanContext::FindDepthFormat() {
    constexpr VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    throw std::runtime_error("[Vulkan] Failed to find supported depth format!");
}

void VulkanContext::CreateDepthResources() {
    m_DepthFormat = FindDepthFormat();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
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

    if (vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &m_DepthImage, &m_DepthAllocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] Failed to create depth image!");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_DepthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_DepthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] Failed to create depth image view!");
}

void VulkanContext::CreateRenderPass() {
    VkAttachmentDescription color{};
    color.format         = m_SwapchainFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format         = m_DepthFormat;
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = {color, depth};

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 2;
    rpci.pAttachments  = attachments;
    rpci.subpassCount    = 1;
    rpci.pSubpasses    = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    if (vkCreateRenderPass(m_Device, &rpci, nullptr, &m_RenderPass) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] Render pass creation failed!");
}

void VulkanContext::CreateFramebuffers() {
    m_Framebuffers.resize(m_SwapchainImages.size());
    for (size_t i = 0; i < m_SwapchainImages.size(); ++i) {
        VkImageView attachments[2] = { m_ImageViews[i], m_DepthImageView };

        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass      = m_RenderPass;
        fbci.attachmentCount = 2;
        fbci.pAttachments    = attachments;
        fbci.width           = m_Extent.width;
        fbci.height          = m_Extent.height;
        fbci.layers          = 1;
        if (vkCreateFramebuffer(m_Device, &fbci, nullptr, &m_Framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("[Vulkan] Framebuffer creation failed!");
    }
}

void VulkanContext::CreateDescriptors() {
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 10;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 10;
    vkCreateDescriptorPool(m_Device, &pool_info, nullptr, &m_EnginePool);

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;
    vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_GlobalSetLayout);
}

VkShaderModule VulkanContext::LoadShaderModule(const char* path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(std::string("[Vulkan] Cannot open shader: ") + path);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    file.close();

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = buffer.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(buffer.data());

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
    bindingDescription.stride = sizeof(f32) * 8;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[3]{};
    attributeDescriptions[0].binding = 0; attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; attributeDescriptions[0].offset = 0;

    attributeDescriptions[1].binding = 0; attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; attributeDescriptions[1].offset = sizeof(f32) * 3;

    attributeDescriptions[2].binding = 0; attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT; attributeDescriptions[2].offset = sizeof(f32) * 6;

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount = 3;
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

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAtt.blendEnable = VK_TRUE;
    blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
    blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1; colorBlend.pAttachments = &blendAtt;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(f32) * 16;

    VkPipelineLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutCI.setLayoutCount = 1;
    layoutCI.pSetLayouts = &m_GlobalSetLayout;
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges = &pushConstantRange;
    vkCreatePipelineLayout(m_Device, &layoutCI, nullptr, &m_PipelineLayout);

    VkGraphicsPipelineCreateInfo pipelineCI{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineCI.stageCount = 2;            pipelineCI.pStages             = stages;
    pipelineCI.pVertexInputState = &vertexInput; pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState = &viewportState;   pipelineCI.pRasterizationState = &rasterizer;
    pipelineCI.pMultisampleState = &multisampling; pipelineCI.pColorBlendState   = &colorBlend;
    pipelineCI.pDepthStencilState = &depthStencil;
    pipelineCI.pDynamicState = &dynState;         pipelineCI.layout              = m_PipelineLayout;
    pipelineCI.renderPass = m_RenderPass;         pipelineCI.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_Pipeline) != VK_SUCCESS)
        throw std::runtime_error("[Vulkan] Pipeline creation failed!");

    vkDestroyShaderModule(m_Device, fragMod, nullptr);
    vkDestroyShaderModule(m_Device, vertMod, nullptr);
}

void VulkanContext::InitImGui(GLFWwindow* window) {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    vkCreateDescriptorPool(m_Device, &pool_info, nullptr, &m_ImGuiPool);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_Instance;
    init_info.PhysicalDevice = m_PhysicalDevice;
    init_info.Device = m_Device;
    init_info.QueueFamily = m_QueueFamily;
    init_info.Queue = m_Queue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = m_ImGuiPool;
    init_info.RenderPass = m_RenderPass;
    init_info.Subpass = 0;
    init_info.MinImageCount = k_MaxFramesInFlight;
    init_info.ImageCount = static_cast<uint32_t>(m_SwapchainImages.size());
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;
    ImGui_ImplVulkan_Init(&init_info);

    ImmediateSubmit([&](VkCommandBuffer /*cmd*/) {
        ImGui_ImplVulkan_CreateFontsTexture();
    });
    ImGui_ImplVulkan_DestroyFontsTexture();
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
    VkClearValue cv[2]{};
    cv[0].color = {{ clearColor.r, clearColor.g, clearColor.b, clearColor.a }};
    cv[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass        = m_RenderPass;
    rpbi.framebuffer       = m_Framebuffers[m_ImageIndex];
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = m_Extent;
    rpbi.clearValueCount   = 2;
    rpbi.pClearValues      = cv;
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

void VulkanContext::PushConstants(const void* data, u32 size) {
    vkCmdPushConstants(m_CommandBuffers[m_CurrentFrame], m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, size, data);
}

TextureHandle VulkanContext::CreateTexture(const TextureDesc& desc) {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(desc.path, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) throw std::runtime_error("[Vulkan] Failed to load image!");

    VkDeviceSize imageSize = texWidth * texHeight * 4;
    BufferDesc stagingDesc{imageSize, false, "TexStaging"};
    BufferHandle staging = CreateBuffer(stagingDesc);

    void* mapped = MapBuffer(staging);
    std::memcpy(mapped, pixels, static_cast<size_t>(imageSize));
    UnmapBuffer(staging);
    stbi_image_free(pixels);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = texWidth;
    imageInfo.extent.height = texHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImage image; VmaAllocation allocation;
    vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr);

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};
        vkCmdCopyBufferToImage(cmd, m_Buffers[staging.index].buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    DestroyBuffer(staging);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VkImageView imageView;
    vkCreateImageView(m_Device, &viewInfo, nullptr, &imageView);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
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
    VkSampler sampler;
    vkCreateSampler(m_Device, &samplerInfo, nullptr, &sampler);

    VkDescriptorSetAllocateInfo allocSetInfo{};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = m_EnginePool;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &m_GlobalSetLayout;

    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(m_Device, &allocSetInfo, &descriptorSet);

    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = imageView;
    descImageInfo.sampler = sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;
    vkUpdateDescriptorSets(m_Device, 1, &descriptorWrite, 0, nullptr);

    u32 index = UINT32_MAX;
    for (u32 i = 0; i < static_cast<u32>(m_Textures.size()); ++i) {
        if (m_Textures[i].image == VK_NULL_HANDLE) { index = i; break; }
    }
    if (index == UINT32_MAX) {
        index = static_cast<u32>(m_Textures.size());
        m_Textures.push_back({image, imageView, sampler, allocation, descriptorSet, 1});
    } else m_Textures[index] = {image, imageView, sampler, allocation, descriptorSet, m_Textures[index].generation + 1};
    return {index, m_Textures[index].generation};
}

void VulkanContext::DestroyTexture(TextureHandle handle) {
    if (!handle.IsValid() || handle.index >= static_cast<u32>(m_Textures.size())) return;
    InternalTexture& it = m_Textures[handle.index];
    if (it.generation != handle.generation || it.image == VK_NULL_HANDLE) return;
    vkDestroySampler(m_Device, it.sampler, nullptr);
    vkDestroyImageView(m_Device, it.view, nullptr);
    vmaDestroyImage(m_Allocator, it.image, it.allocation);
    it.image = VK_NULL_HANDLE; it.view = VK_NULL_HANDLE; it.sampler = VK_NULL_HANDLE; it.allocation = VK_NULL_HANDLE;
}

void VulkanContext::BindTexture(TextureHandle handle) {
    vkCmdBindDescriptorSets(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &m_Textures[handle.index].descriptorSet, 0, nullptr);
}

void VulkanContext::BeginImGui() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void VulkanContext::EndImGui() {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_CommandBuffers[m_CurrentFrame]);
}

}
