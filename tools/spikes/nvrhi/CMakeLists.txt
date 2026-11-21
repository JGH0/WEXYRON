# Phase 0 spike (Vulkan/DX12 port): NVRHI + GLFW, one instanced triangle.
# Standalone project, not part of the game build:
#   cmake -S tools/spikes/nvrhi -B build-spike -G Ninja && cmake --build build-spike
# Needs the Vulkan SDK (VULKAN_SDK set; provides the loader and dxc for SPIR-V).
# On Windows, DXIL is compiled with the Windows SDK dxc, which can sign it.
cmake_minimum_required(VERSION 3.20)
project(nvrhi_spike LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

include(FetchContent)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)   # X11 is enough for the spike
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)

set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
    GIT_TAG bf0bbd9b666a7297492beb1999a4ddc0dfded143)
FetchContent_MakeAvailable(glfw nvrhi)

# ── Shaders: one HLSL source -> SPIR-V (+ signed DXIL on Windows) ───────────
find_program(DXC_SPIRV dxc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)
set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.hlsl)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
set(SHADER_OUTPUTS ${SHADER_OUT}/triangle_vs.spv ${SHADER_OUT}/triangle_ps.spv)
set(SHADER_CMDS
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT}
    COMMAND ${DXC_SPIRV} -spirv -fvk-invert-y -fspv-target-env=vulkan1.2 -T vs_6_0 -E main_vs ${SHADER_SRC} -Fo ${SHADER_OUT}/triangle_vs.spv
    COMMAND ${DXC_SPIRV} -spirv -fspv-target-env=vulkan1.2 -T ps_6_0 -E main_ps ${SHADER_SRC} -Fo ${SHADER_OUT}/triangle_ps.spv)
if(WIN32)
    # Newest installed Windows SDK that ships dxc + dxil.dll.
    file(GLOB _kit_dxcs "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x64/dxc.exe")
    list(SORT _kit_dxcs COMPARE NATURAL ORDER DESCENDING)
    list(GET _kit_dxcs 0 DXC_DXIL)
    if(NOT DXC_DXIL)
        message(FATAL_ERROR "Windows SDK dxc.exe not found")
    endif()
    message(STATUS "DXIL compiler: ${DXC_DXIL}")
    list(APPEND SHADER_OUTPUTS ${SHADER_OUT}/triangle_vs.dxil ${SHADER_OUT}/triangle_ps.dxil)
    list(APPEND SHADER_CMDS
        COMMAND ${DXC_DXIL} -T vs_6_0 -E main_vs ${SHADER_SRC} -Fo ${SHADER_OUT}/triangle_vs.dxil
        COMMAND ${DXC_DXIL} -T ps_6_0 -E main_ps ${SHADER_SRC} -Fo ${SHADER_OUT}/triangle_ps.dxil)
endif()
add_custom_command(OUTPUT ${SHADER_OUTPUTS} ${SHADER_CMDS} DEPENDS ${SHADER_SRC})
add_custom_target(spike_shaders DEPENDS ${SHADER_OUTPUTS})

add_executable(nvrhi_spike src/main.cpp)
add_dependencies(nvrhi_spike spike_shaders)
target_compile_definitions(nvrhi_spike PRIVATE SPIKE_SHADER_DIR="${SHADER_OUT}")
# Backend libraries before the common nvrhi library: GNU ld resolves static
# archives in one pass, left to right.
target_link_libraries(nvrhi_spike PRIVATE nvrhi_vk)
if(WIN32)
    target_link_libraries(nvrhi_spike PRIVATE nvrhi_d3d12 d3d12 dxgi)
endif()
target_link_libraries(nvrhi_spike PRIVATE nvrhi glfw Vulkan::Vulkan)
