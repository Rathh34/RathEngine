#include "RathEngine/Application/Application.h"
#include "RathEngine/Core/JobSystem/JobSystem.h"
#include "RathEngine/Platform/GlfwWindow.h"
#include "RathEngine/Renderer/Vulkan/VulkanContext.h"

#include <chrono>
#include <iostream>

namespace Rath {

    Application::Application() {
        JobSystem::Initialize();
        m_Window = std::make_unique<GlfwWindow>(WindowSpec{"RathEngine", 1280, 720});
        m_RHI    = std::make_unique<RHI::VulkanContext>();
        m_RHI->Init(m_Window.get());
        std::cout << "[RathEngine] Application initialized.\n";
    }

    Application::~Application() {
        for (std::unique_ptr<IModule>& mod : m_Modules) mod->OnShutdown();
        m_RHI->Shutdown();
        JobSystem::Shutdown();
        std::cout << "[RathEngine] Application shutdown.\n";
    }

    void Application::RegisterModule(std::unique_ptr<IModule> mod) {
        mod->OnInit(*this);
        m_Modules.push_back(std::move(mod));
    }

    void Application::Run() {
        using Clock = std::chrono::high_resolution_clock;

        Clock::time_point last = Clock::now();

        while (m_Running && !m_Window->ShouldClose()) {
            Clock::time_point now = Clock::now();
            std::chrono::duration<f32> delta = now - last;
            f32 dt = delta.count();
            last = now;

            m_FrameAllocator.Reset();
            m_Window->PollEvents();

            for (std::unique_ptr<IModule>& mod : m_Modules) mod->OnUpdate(dt);

            if (m_RHI->BeginFrame()) {
                m_RHI->ClearColor({ 0.01f, 0.05f, 0.2f, 1.0f });
                m_RHI->EndFrame();
            }
        }
    }

} // namespace Rath
