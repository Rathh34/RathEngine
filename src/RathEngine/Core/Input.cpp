#include "RathEngine/Core/Input.h"
#include "RathEngine/Platform/IWindow.h"
#include <GLFW/glfw3.h>
#include <cctype>

namespace Rath {
    IWindow* Input::s_Window = nullptr;
    std::unordered_map<u16, int> Input::s_KeyToScancode;

    void Input::Init(IWindow* window) {
        s_Window = window;
        s_KeyToScancode.clear();

        // Build the layout-independent map.
        for (int glfwKey = GLFW_KEY_SPACE; glfwKey <= GLFW_KEY_GRAVE_ACCENT; ++glfwKey) {
            int scancode = glfwGetKeyScancode(glfwKey);
            const char* keyName = glfwGetKeyName(glfwKey, scancode);
            
            if (keyName) {
                // Convert the printed character to uppercase (e.g., 'z' -> 'Z')
                char upperChar = static_cast<char>(std::toupper(keyName[0]));
                s_KeyToScancode[static_cast<u16>(upperChar)] = scancode;
            }
        }

        // Map non-printable keys directly
        s_KeyToScancode[Key::LeftShift] = glfwGetKeyScancode(GLFW_KEY_LEFT_SHIFT);
        s_KeyToScancode[Key::Space] = glfwGetKeyScancode(GLFW_KEY_SPACE);
    }

    bool Input::IsKeyDown(u16 keycode) {
        auto* window = static_cast<GLFWwindow*>(s_Window->GetNativeHandle());
        
        auto it = s_KeyToScancode.find(keycode);
        if (it != s_KeyToScancode.end()) {
            int targetScancode = it->second;
            
            // Find which GLFW virtual key corresponds to this scancode and check it
            for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; ++k) {
                if (glfwGetKeyScancode(k) == targetScancode) {
                    auto state = glfwGetKey(window, k);
                    return state == GLFW_PRESS || state == GLFW_REPEAT;
                }
            }
        }
        
        // Fallback
        auto state = glfwGetKey(window, keycode);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsMouseButtonDown(u16 button) {
        auto* window = static_cast<GLFWwindow*>(s_Window->GetNativeHandle());
        auto state = glfwGetMouseButton(window, button);
        return state == GLFW_PRESS;
    }

    glm::vec2 Input::GetMousePosition() {
        auto* window = static_cast<GLFWwindow*>(s_Window->GetNativeHandle());
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        return { static_cast<f32>(xpos), static_cast<f32>(ypos) };
    }
}
