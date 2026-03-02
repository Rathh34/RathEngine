cmake_minimum_required(VERSION 3.20)
project(RathEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Find dependencies
find_package(glfw3 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

# 2. Safely extract glslc path directly from the Vulkan package
if(TARGET Vulkan::glslc)
    # Modern CMake sets this target if glslc is found
    get_target_property(GLSLC_EXECUTABLE Vulkan::glslc IMPORTED_LOCATION)
elseif(Vulkan_GLSLC_EXECUTABLE)
    # Legacy fallback variable populated by FindVulkan.cmake
    set(GLSLC_EXECUTABLE ${Vulkan_GLSLC_EXECUTABLE})
endif()

# 3. Final safety check with a clear error message
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "Vulkan was found, but glslc.exe could not be located. Please ensure the Vulkan SDK is installed correctly.")
endif()

# 4. Shader compilation setup
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

set(SHADER_SOURCES
        "${SHADER_SRC_DIR}/triangle.vert"
        "${SHADER_SRC_DIR}/triangle.frag"
)

set(SPIRV_BINARIES)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME "${SHADER}" NAME)
    set(SPV "${SHADER_BIN_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
            OUTPUT "${SPV}"
            COMMAND "${GLSLC_EXECUTABLE}" "${SHADER}" -o "${SPV}"
            DEPENDS "${SHADER}"
            VERBATIM
    )
    list(APPEND SPIRV_BINARIES "${SPV}")
endforeach()

add_custom_target(RathShaders ALL DEPENDS ${SPIRV_BINARIES})

# 5. Config Header setup
file(TO_CMAKE_PATH "${SHADER_BIN_DIR}" RATH_SHADER_DIR)
set(RATH_SHADER_DIR "${RATH_SHADER_DIR}/")

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/RathEngine/Core/Config.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/RathEngine/Core/Config.h"
        @ONLY
)

# 6. Executable setup
file(GLOB_RECURSE RATHENGINE_SOURCES "src/*.cpp")
add_executable(RathEngine ${RATHENGINE_SOURCES})
add_dependencies(RathEngine RathShaders)

target_include_directories(RathEngine PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

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
