#pragma once
#include "RathEngine/Core/Events/EventBus.h"
#include "RathEngine/Core/Memory/LinearAllocator.h"
#include "RathEngine/Core/Module/IModule.h"
#include "RathEngine/Platform/IWindow.h"
#include "RathEngine/Renderer/RHI/IRHIContext.h"
#include <memory>
#include <vector>

namespace Rath {
    class Application {
    public:
        Application();
        ~Application();
        void Run();
        void RequestExit() { m_Running = false; }
        void RegisterModule(std::unique_ptr<IModule> mod);
        EventBus&         GetEventBus()       { return m_EventBus; }
        IWindow*          GetWindow()         { return m_Window.get(); }
        RHI::IRHIContext* GetRHI()            { return m_RHI.get(); }
        LinearAllocator&  GetFrameAllocator() { return m_FrameAllocator; }

    private:
        bool m_Running = true;
        std::unique_ptr<IWindow>              m_Window;
        std::unique_ptr<RHI::IRHIContext>     m_RHI;
        EventBus                              m_EventBus;
        LinearAllocator                       m_FrameAllocator{ 64 * 1024 * 1024 };
        std::vector<std::unique_ptr<IModule>> m_Modules;

        RHI::BufferHandle m_TriangleBuffer;
        RHI::BufferHandle m_IndexBuffer;
    };
}
