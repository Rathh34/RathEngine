#pragma once
#include <string_view>
#include "RathEngine/Core/Types.h"

namespace Rath {
    class Application;

    class IModule {
    public:
        virtual ~IModule() = default;
        virtual std::string_view GetName()          const = 0;
        virtual void OnInit    (Application& app)         = 0;
        virtual void OnUpdate  (f32 dt)                   = 0;
        virtual void OnShutdown()                         = 0;
    };
}
