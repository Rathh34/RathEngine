#include "RathEngine/Platform/GlfwWindow.h" // Fixed include path
#include <stdexcept>

namespace Rath {

    GlfwWindow::GlfwWindow(const WindowSpec& spec)
        : m_Width(spec.width), m_Height(spec.height) {
        if (!glfwInit())
            throw std::runtime_error("[RathEngine] GLFW init failed!");

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_Window = glfwCreateWindow((int)m_Width, (int)m_Height, spec.title.c_str(), nullptr, nullptr);

        if (!m_Window)
            throw std::runtime_error("[RathEngine] GLFW window creation failed!");
    }

    GlfwWindow::~GlfwWindow() {
        if (m_Window) glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

} // namespace Rath
