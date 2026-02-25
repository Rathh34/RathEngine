cmake_minimum_required(VERSION 3.20)
project(RathEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Packages
find_package(glfw3 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(imgui CONFIG REQUIRED)
# vvv CHANGE THIS LINE vvv
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

file(GLOB_RECURSE RATHENGINE_SOURCES "src/*.cpp")
add_executable(RathEngine ${RATHENGINE_SOURCES})

target_include_directories(RathEngine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(RathEngine PRIVATE
        glfw
        Vulkan::Vulkan
        imgui::imgui
        GPUOpen::VulkanMemoryAllocator
)

target_compile_options(RathEngine PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
