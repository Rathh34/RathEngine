#include "RathEngine/Application/Application.h"
#include "RathEngine/Core/JobSystem/JobSystem.h"
#include "RathEngine/Platform/GlfwWindow.h"
#include "RathEngine/Renderer/Vulkan/VulkanContext.h"

#include <chrono>
#include <imgui.h>
#include <iostream>
#include <filesystem>

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace Rath {

    static std::filesystem::path GetExecutableDir()
    {
    #if defined(_WIN32)
        wchar_t buffer[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (len == 0 || len == MAX_PATH) {
            return std::filesystem::current_path();
        }
        std::filesystem::path exePath(buffer);
        return exePath.parent_path();
    #else
        return std::filesystem::current_path();
    #endif
    }

    static std::filesystem::path FindProjectRootFrom(const std::filesystem::path& startDir)
    {
        // Walk up a few levels looking for CMakeLists.txt as a "project root" marker.
        std::filesystem::path p = startDir;
        for (int i = 0; i < 8; i++) {
            if (std::filesystem::exists(p / "CMakeLists.txt"))
                return p;

            if (!p.has_parent_path())
                break;

            p = p.parent_path();
        }
        return startDir;
    }

    Application::Application() {
        JobSystem::Initialize();

        m_Window = std::make_unique<GlfwWindow>(WindowSpec{"RathEngine", 1280, 720});
        m_RHI    = std::make_unique<RHI::VulkanContext>();
        m_RHI->Init(m_Window.get());

        // Camera
        m_Camera = std::make_unique<Camera>(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f);
        m_Camera->SetPosition({0.0f, 0.0f, 5.0f});

        // Resolve assets relative to the *project root* (found by walking up from the executable dir).
        const std::filesystem::path exeDir   = GetExecutableDir();
        const std::filesystem::path rootDir  = FindProjectRootFrom(exeDir);
        const std::filesystem::path objPath  = rootDir / "assets" / "models" / "boulet.obj";
        const std::filesystem::path texPath  = rootDir / "assets" / "textures" / "texture.png";

        std::cout << "[Path] cwd     = " << std::filesystem::current_path().string() << "\n";
        std::cout << "[Path] exeDir  = " << exeDir.string() << "\n";
        std::cout << "[Path] rootDir = " << rootDir.string() << "\n";
        std::cout << "[Path] obj     = " << objPath.string() << "\n";
        std::cout << "[Path] tex     = " << texPath.string() << "\n";

        if (!std::filesystem::exists(objPath)) {
            std::cerr << "[Engine] OBJ does not exist at: " << objPath.string() << "\n";
        }

        if (!m_Mesh.LoadFromObj(m_RHI.get(), objPath.string())) {
            std::cerr << "[Engine] Failed to load " << objPath.string() << ". Defaulting to empty mesh.\n";
        }

        // Entities
        Transform t1;
        t1.position = {0.0f, 0.0f, 0.0f};
        m_Entities.push_back(t1);

        // Texture (TextureDesc::path appears to be const char*, so keep storage alive)
        static std::string texturePathStr;
        texturePathStr = texPath.string();

        RHI::TextureDesc tDesc{};
        tDesc.path = texturePathStr.c_str();
        m_Texture = m_RHI->CreateTexture(tDesc);
    }

    Application::~Application() {
        m_RHI->WaitIdle();

        m_Mesh.Destroy(m_RHI.get());
        m_RHI->Shutdown();
        for (auto& mod : m_Modules) mod->OnShutdown();
        JobSystem::Shutdown();
    }


    void Application::RegisterModule(std::unique_ptr<IModule> mod) {
        mod->OnInit(*this);
        m_Modules.push_back(std::move(mod));
    }

    void Application::Run() {
        using Clock = std::chrono::high_resolution_clock;
        Clock::time_point last = Clock::now();
        f32 time = 0.0f;
        f32 clearColor[4] = { 0.01f, 0.05f, 0.2f, 1.0f };

        while (m_Running && !m_Window->ShouldClose()) {
            Clock::time_point now = Clock::now();
            f32 dt = std::chrono::duration<f32>(now - last).count();
            last = now;
            time += dt;

            m_FrameAllocator.Reset();
            m_Window->PollEvents();

            for (auto& mod : m_Modules) mod->OnUpdate(dt);

            // Spin the model
            if (!m_Entities.empty())
                m_Entities[0].rotation.y = time * 45.0f;

            if (m_RHI->BeginFrame()) {
                static_cast<RHI::VulkanContext*>(m_RHI.get())->BeginImGui();

                ImGui::Begin("Engine Controls");
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::ColorEdit4("Clear Color", clearColor);
                ImGui::End();

                m_RHI->BeginPass({ clearColor[0], clearColor[1], clearColor[2], clearColor[3] });
                m_RHI->BindTexture(m_Texture);

                for (const auto& entity : m_Entities) {
                    glm::mat4 MVP = m_Camera->GetViewProjection() * entity.GetMatrix();
                    m_RHI->PushConstants(&MVP, sizeof(glm::mat4));

                    if (m_Mesh.GetIndexCount() > 0) {
                        m_RHI->BindVertexBuffer(m_Mesh.GetVertexBuffer());
                        m_RHI->BindIndexBuffer(m_Mesh.GetIndexBuffer());
                        m_RHI->DrawIndexed(m_Mesh.GetIndexCount());
                    }
                }

                static_cast<RHI::VulkanContext*>(m_RHI.get())->EndImGui();

                m_RHI->EndPass();
                m_RHI->EndFrame();
            }
        }
    }
}
