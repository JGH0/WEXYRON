# Cube Engine: reusable, game-agnostic core extracted from WEXYRON.
#
# The target is a static library. It links the graphics/audio dependencies and
# exposes only the public headers in engine/include. It must never include
# game headers (see engine/README.md and the dependency check target below).

add_library(cube_engine STATIC
    src/audio_manager.cpp
    src/camera.cpp
    src/gltf_model.cpp
    src/map.cpp
    src/map_loader.cpp
    src/renderer.cpp
    src/render_window.cpp
    src/backends/gl/gl_backend.cpp
    src/backends/nvrhi/nvrhi_backend.cpp
    src/backends/nvrhi/vulkan_device.cpp
    src/backends/nvrhi/fsr_vulkan.cpp
    src/backends/nvrhi/streamline_vulkan.cpp
    src/shader.cpp
    src/texture.cpp
    ${CMAKE_SOURCE_DIR}/glad.c
    ${CMAKE_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
)

target_include_directories(cube_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/stb
)

target_link_libraries(cube_engine PUBLIC
    OpenGL::GL
    glfw
    Threads::Threads
    ${PLATFORM_LIBS}
)

if(TARGET glm::glm)
    target_link_libraries(cube_engine PUBLIC glm::glm)
endif()

# ---------------------------------------------------------------------------
# Vulkan backend (NVRHI)
# ---------------------------------------------------------------------------
# Needs the third_party/nvrhi and third_party/vulkan-headers submodules
# (git submodule update --init). Nothing links against the Vulkan loader:
# GLFW loads it at runtime, so the game still starts without Vulkan and then
# uses OpenGL. Without the submodules the engine builds OpenGL-only.
set(CUBE_NVRHI_DIR "${CMAKE_SOURCE_DIR}/third_party/nvrhi")
set(CUBE_VULKAN_HEADERS_DIR "${CMAKE_SOURCE_DIR}/third_party/vulkan-headers")
if(EXISTS "${CUBE_NVRHI_DIR}/CMakeLists.txt" AND EXISTS "${CUBE_VULKAN_HEADERS_DIR}/CMakeLists.txt")
    set(_cube_vulkan_default ON)
else()
    set(_cube_vulkan_default OFF)
endif()
option(CUBE_WITH_VULKAN "Build the Vulkan render backend (NVRHI)" ${_cube_vulkan_default})

if(CUBE_WITH_VULKAN)
    if(NOT TARGET Vulkan::Headers)
        add_subdirectory(${CUBE_VULKAN_HEADERS_DIR} ${CMAKE_BINARY_DIR}/third_party/vulkan-headers EXCLUDE_FROM_ALL)
    endif()
    set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
    set(NVRHI_FETCH_VULKAN_HEADERS OFF CACHE BOOL "" FORCE)
    set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
    set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
    set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)   # Direct3D 12 comes in a later step
    add_subdirectory(${CUBE_NVRHI_DIR} ${CMAKE_BINARY_DIR}/third_party/nvrhi EXCLUDE_FROM_ALL)
    # Backend library before the common one (GNU ld resolves static archives
    # left to right). nvrhi_vk also carries VK_USE_PLATFORM_WIN32_KHR on
    # Windows, which must match between NVRHI and the engine.
    target_link_libraries(cube_engine PUBLIC nvrhi_vk nvrhi Vulkan::Headers)
    target_compile_definitions(cube_engine PUBLIC CUBE_WITH_VULKAN)
    message(STATUS "Cube Engine: Vulkan backend enabled (NVRHI)")

    # AMD FSR frame generation: SDK files from tools/fetch_sdks.py (Windows).
    set(CUBE_FSR_KIT "${CMAKE_SOURCE_DIR}/third_party/sdks/fidelityfx/Kits/FidelityFX")
    if(WIN32 AND EXISTS "${CUBE_FSR_KIT}/api/include/ffx_api.h")
        target_compile_definitions(cube_engine PRIVATE CUBE_WITH_FSR)
        target_include_directories(cube_engine PRIVATE "${CUBE_FSR_KIT}/api/include"
                                                       "${CUBE_FSR_KIT}/framegeneration/include")
        message(STATUS "Cube Engine: AMD FSR frame generation enabled")
    endif()

    # NVIDIA DLSS frame generation + Reflex (Streamline): also fetch_sdks.py.
    set(CUBE_SL_DIR "${CMAKE_SOURCE_DIR}/third_party/sdks/streamline")
    if(WIN32 AND EXISTS "${CUBE_SL_DIR}/include/sl.h")
        target_compile_definitions(cube_engine PRIVATE CUBE_WITH_STREAMLINE)
        target_include_directories(cube_engine PRIVATE "${CUBE_SL_DIR}/include")
        target_link_libraries(cube_engine PRIVATE wintrust crypt32)
        message(STATUS "Cube Engine: NVIDIA DLSS frame generation enabled (Streamline)")
    endif()
else()
    message(STATUS "Cube Engine: Vulkan backend disabled (run 'git submodule update --init')")
endif()

target_compile_features(cube_engine PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(cube_engine PRIVATE /W4)
else()
    target_compile_options(cube_engine PRIVATE -Wall)
endif()

# ---------------------------------------------------------------------------
# Dependency boundary check
# ---------------------------------------------------------------------------
# The engine is only general if it never pulls in game code. This target fails
# the build if an engine source includes game/, systems/, ui/, network/ or the
# monolithic core/game header.
add_custom_target(engine_boundary_check
    COMMAND ${CMAKE_COMMAND}
        -DENGINE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_boundary.cmake
    COMMENT "Checking that cube_engine has no game dependencies"
)

add_dependencies(cube_engine engine_boundary_check)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_executable(engine_smoke_test tests/engine_smoke_test.cpp)
target_link_libraries(engine_smoke_test PRIVATE cube_engine)
set_target_properties(engine_smoke_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_test(NAME engine_smoke_test COMMAND engine_smoke_test)
