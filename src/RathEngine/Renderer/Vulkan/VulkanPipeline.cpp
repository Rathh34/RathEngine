#include "RathEngine/Renderer/Vulkan/VulkanPipeline.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace Rath::RHI {
    void VulkanPipelineBuilder::Clear() {
        m_ShaderStages.clear();
        
        m_VertexInputInfo = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        m_InputAssembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        m_Rasterizer = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        m_ColorBlendAttachment = {};
        m_Multisampling = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        m_DepthStencil = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        m_PipelineLayout = VK_NULL_HANDLE;
    }

    VkShaderModule VulkanPipelineBuilder::LoadShaderModule(VkDevice device, const std::string& filepath) {
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Vulkan: Cannot open shader " + filepath);

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
        file.close();

        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = buffer.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

        VkShaderModule mod = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) {
            throw std::runtime_error("Vulkan: Shader module failed " + filepath);
        }
        return mod;
    }

    VulkanPipelineBuilder& VulkanPipelineBuilder::SetShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader) {
        m_ShaderStages.clear();

        VkPipelineShaderStageCreateInfo vertStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertexShader;
        vertStage.pName = "main";
        m_ShaderStages.push_back(vertStage);

        VkPipelineShaderStageCreateInfo fragStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragmentShader;
        fragStage.pName = "main";
        m_ShaderStages.push_back(fragStage);

        return *this;
    }

    VulkanPipelineBuilder& VulkanPipelineBuilder::SetInputTopology(VkPrimitiveTopology topology) {
        m_InputAssembly.topology = topology;
        m_InputAssembly.primitiveRestartEnable = VK_FALSE;
        return *this;
    }

    VulkanPipelineBuilder& VulkanPipelineBuilder::SetPolygonMode(VkPolygonMode mode) {
        m_Rasterizer.polygonMode = mode;
        m_Rasterizer.lineWidth = 1.0f;
        return *this;
    }

    VulkanPipelineBuilder& VulkanPipelineBuilder::SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace) {
        m_Rasterizer.cullMode = cullMode;
        m_Rasterizer.frontFace = frontFace;
        return *this;
    }

    VulkanPipelineBuilder& VulkanPipelineBuilder::SetMultisamplingNone() {
        m_Multisampling.sampleShadingEnable = VK_FALSE;
        m_Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        m_Multisampling.minSampleShading = 1.0f;
        m_Multisampling.pSampleMask = nullptr;
        m_Multisampling.alphaToCoverageEnable = VK_FALSE;
        m_Multisampling.alphaToOneEnable = VK_FALSE;
        return *this;
    }

    VulkanPipelineBuilder& VulkanPipelineBuilder::DisableBlending() {
        m_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        m_ColorBlendAttachment.blendEnable = VK_FALSE;
        return *this;
    }

    VulkanPipelineBuilder& VulkanPipelineBuilder::EnableAlphaBlending() {
        m_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        m_ColorBlendAttachment.blendEnable = VK_TRUE;
        m_ColorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        m_ColorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        m_ColorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        m_ColorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        m_ColorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        m_ColorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        return *this;
    }

    VulkanPipelineBuilder& VulkanPipelineBuilder::EnableDepthTest(bool depthWriteEnable, VkCompareOp op) {
        m_DepthStencil.depthTestEnable = VK_TRUE;
        m_DepthStencil.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;
        m_DepthStencil.depthCompareOp = op;
        m_DepthStencil.depthBoundsTestEnable = VK_FALSE;
        m_DepthStencil.stencilTestEnable = VK_FALSE;
        return *this;
    }

    VulkanPipelineBuilder& VulkanPipelineBuilder::SetPipelineLayout(VkPipelineLayout layout) {
        m_PipelineLayout = layout;
        return *this;
    }

    VkPipeline VulkanPipelineBuilder::Build(VkDevice device, VkRenderPass pass) {
        // We will use dynamic viewport and scissor states so we don't have to rebuild the pipeline on window resize
        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.pViewports = nullptr; // Dynamic
        viewportState.scissorCount = 1;
        viewportState.pScissors = nullptr; // Dynamic

        VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlend.logicOpEnable = VK_FALSE;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &m_ColorBlendAttachment;

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicInfo{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicInfo.dynamicStateCount = 2;
        dynamicInfo.pDynamicStates = dynamicStates;

        // Hardcoded Vertex Input for now (we will abstract this in the Material system later)
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(float) * 8; 
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attributeDescriptions[3]{};
        attributeDescriptions[0].binding = 0; attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; attributeDescriptions[0].offset = 0;
        attributeDescriptions[1].binding = 0; attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; attributeDescriptions[1].offset = sizeof(float) * 3;
        attributeDescriptions[2].binding = 0; attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT; attributeDescriptions[2].offset = sizeof(float) * 6;

        m_VertexInputInfo.vertexBindingDescriptionCount = 1;
        m_VertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        m_VertexInputInfo.vertexAttributeDescriptionCount = 3;
        m_VertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = static_cast<uint32_t>(m_ShaderStages.size());
        pipelineInfo.pStages = m_ShaderStages.data();
        pipelineInfo.pVertexInputState = &m_VertexInputInfo;
        pipelineInfo.pInputAssemblyState = &m_InputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &m_Rasterizer;
        pipelineInfo.pMultisampleState = &m_Multisampling;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDepthStencilState = &m_DepthStencil;
        pipelineInfo.pDynamicState = &dynamicInfo;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = pass;
        pipelineInfo.subpass = 0;

        VkPipeline newPipeline;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
            std::cerr << "Failed to create pipeline!" << std::endl;
            return VK_NULL_HANDLE;
        }
        return newPipeline;
    }
}
