#pragma once
#include "RathEngine/Platform/IWindow.h" // Fixed include path
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Rath {

    class GlfwWindow final : public IWindow {
    public:
        explicit GlfwWindow(const WindowSpec& spec);
        ~GlfwWindow() override;

        void  PollEvents()            override { glfwPollEvents(); }
        bool  ShouldClose()     const override { return glfwWindowShouldClose(m_Window); }
        void* GetNativeHandle() const override { return m_Window; }
        u32   GetWidth()        const override { return m_Width; }
        u32   GetHeight()       const override { return m_Height; }

    private:
        GLFWwindow* m_Window = nullptr;
        u32 m_Width = 0, m_Height = 0;
    };

} // namespace Rath
