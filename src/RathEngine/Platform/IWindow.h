#pragma once
#include "RathEngine/Core/Types.h"
#include <string>

namespace Rath {

    struct WindowSpec {
        std::string title  = "RathEngine";
        u32         width  = 1280;
        u32         height = 720;
    };

    class IWindow {
    public:
        virtual ~IWindow() = default;
        virtual void  PollEvents()            = 0;
        virtual bool  ShouldClose()     const = 0;
        virtual void* GetNativeHandle() const = 0; // GLFWwindow*
        virtual u32   GetWidth()        const = 0;
        virtual u32   GetHeight()       const = 0;
    };

} // namespace Rath
