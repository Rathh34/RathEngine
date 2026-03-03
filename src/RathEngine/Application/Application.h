#pragma once
#include "RathEngine/Core/Types.h"
#include "RathEngine/Core/Memory/LinearAllocator.h"
#include "RathEngine/Renderer/RHI/IRHIContext.h"
#include "RathEngine/Renderer/Mesh.h"
#include "RathEngine/Scene/Camera.h"
#include "RathEngine/Scene/Transform.h"
#include <memory>
#include <vector>
#include <string_view>

namespace Rath {
    class IWindow;

    class IModule {
    public:
        virtual ~IModule() = default;
        virtual std::string_view GetName() const = 0;
        virtual void OnInit(class Application& app) = 0;
        virtual void OnUpdate(f32 dt) = 0;
        virtual void OnShutdown() = 0;
    };

    class Application {
    public:
        Application();
        ~Application();

        void Run();
        void RegisterModule(std::unique_ptr<IModule> mod);

        template<typename T, typename... Args>
        T* AllocateFrame(Args&&... args) {
            return m_FrameAllocator.Allocate<T>(std::forward<Args>(args)...);
        }

    private:
        std::unique_ptr<IWindow> m_Window;
        std::unique_ptr<RHI::IRHIContext> m_RHI;
        std::vector<std::unique_ptr<IModule>> m_Modules;

        bool m_Running{true};
        LinearAllocator m_FrameAllocator{1024 * 1024};

        Mesh m_Mesh;
        std::vector<Transform> m_Entities;
        std::unique_ptr<Camera> m_Camera;
        RHI::TextureHandle m_Texture;
    };
}
