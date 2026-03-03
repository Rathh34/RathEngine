#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace Rath::RHI {
    class VulkanPipelineBuilder {
    public:
        VulkanPipelineBuilder() { Clear(); }

        void Clear();

        VkShaderModule LoadShaderModule(VkDevice device, const std::string& filepath);

        // Builder methods
        VulkanPipelineBuilder& SetShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
        VulkanPipelineBuilder& SetInputTopology(VkPrimitiveTopology topology);
        VulkanPipelineBuilder& SetPolygonMode(VkPolygonMode mode);
        VulkanPipelineBuilder& SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
        VulkanPipelineBuilder& SetMultisamplingNone();
        VulkanPipelineBuilder& DisableBlending();
        VulkanPipelineBuilder& EnableAlphaBlending();
        VulkanPipelineBuilder& EnableDepthTest(bool depthWriteEnable, VkCompareOp op);
        VulkanPipelineBuilder& SetPipelineLayout(VkPipelineLayout layout);

        // Build the final pipeline
        VkPipeline Build(VkDevice device, VkRenderPass pass);

    private:
        std::vector<VkPipelineShaderStageCreateInfo> m_ShaderStages;
        VkPipelineVertexInputStateCreateInfo m_VertexInputInfo;
        VkPipelineInputAssemblyStateCreateInfo m_InputAssembly;
        VkViewport m_Viewport;
        VkRect2D m_Scissor;
        VkPipelineRasterizationStateCreateInfo m_Rasterizer;
        VkPipelineColorBlendAttachmentState m_ColorBlendAttachment;
        VkPipelineMultisampleStateCreateInfo m_Multisampling;
        VkPipelineDepthStencilStateCreateInfo m_DepthStencil;
        VkPipelineLayout m_PipelineLayout{VK_NULL_HANDLE};
    };
}
