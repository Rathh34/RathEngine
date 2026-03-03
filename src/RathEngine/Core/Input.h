#pragma once
#include "RathEngine/Core/KeyCodes.h"
#include <glm/glm.hpp>
#include <unordered_map>

namespace Rath {
    class IWindow;

    class Input {
    public:
        static void Init(IWindow* window);

        [[nodiscard]] static bool IsKeyDown(u16 keycode);
        [[nodiscard]] static bool IsMouseButtonDown(u16 button);
        [[nodiscard]] static glm::vec2 GetMousePosition();

    private:
        static IWindow* s_Window;
        static std::unordered_map<u16, int> s_KeyToScancode;
    };
}
