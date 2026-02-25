#include "RathEngine/Application/Application.h"

int main() {
    Rath::Application app;
    // app.RegisterModule(std::make_unique<Rath::PhysicsModule>());
    // app.RegisterModule(std::make_unique<Rath::AudioModule>());
    app.Run();
    return 0;
}
