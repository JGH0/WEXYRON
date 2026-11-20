cmake_minimum_required(VERSION 3.10)
project(WEXYRON VERSION 1.0 LANGUAGES C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)   # std::filesystem, std::make_unique (also used by the Makefiles)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform detection
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS pthread dl)
endif()

# Threads for the worker pool / background map loading + std::this_thread::sleep_for
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Find packages. On Linux these come from the distro (pacman/apt) and this is
# all that runs. On Windows the same find_package calls succeed when built
# inside MSYS2 UCRT64/MINGW64 (pacman -S mingw-w64-ucrt-x86_64-glfw
# mingw-w64-ucrt-x86_64-glm) or with vcpkg (glfw3 + glm, see vcpkg.json).
# When they are missing (e.g. Visual Studio without vcpkg), fall back to
# FetchContent so `cmake --preset` / `cmake -S . -B build` works with just
# internet access on first configure. Vendored MinGW binaries in
# .mingw-deps/glfw are the last resort for GLFW only when git is unavailable.
find_package(OpenGL REQUIRED)

find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "glfw3 not found via find_package — trying FetchContent / vendored fallback")
    set(CUBE_HAS_GLFW OFF)
    # Vendored prebuilt GLFW (committed in the repo for offline MinGW builds).
    set(_vendored_glfw "${CMAKE_SOURCE_DIR}/.mingw-deps/glfw/glfw-3.4.bin.WIN64")
    if(WIN32 AND EXISTS "${_vendored_glfw}/include/GLFW/glfw3.h")
        if(NOT MSVC AND EXISTS "${_vendored_glfw}/lib-mingw-w64/libglfw3dll.a")
            add_library(glfw_vendored UNKNOWN IMPORTED)
            set_target_properties(glfw_vendored PROPERTIES
                IMPORTED_LOCATION "${_vendored_glfw}/lib-mingw-w64/libglfw3dll.a"
                INTERFACE_INCLUDE_DIRECTORIES "${_vendored_glfw}/include")
            add_library(glfw ALIAS glfw_vendored)
            set(CUBE_HAS_GLFW ON)
            message(STATUS "Using vendored MinGW GLFW from .mingw-deps/glfw")
        elseif(MSVC AND EXISTS "${_vendored_glfw}/lib-vc2022/glfw3dll.lib")
            add_library(glfw_vendored UNKNOWN IMPORTED)
            # glfw3_mt.lib is the static one; prefer the DLL import lib.
            set_target_properties(glfw_vendored PROPERTIES
                IMPORTED_LOCATION "${_vendored_glfw}/lib-vc2022/glfw3dll.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${_vendored_glfw}/include")
            add_library(glfw ALIAS glfw_vendored)
            set(CUBE_HAS_GLFW ON)
            message(STATUS "Using vendored MSVC GLFW from .mingw-deps/glfw")
        endif()
    endif()
    if(NOT CUBE_HAS_GLFW)
        include(FetchContent)
        FetchContent_Declare(glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(glfw)
        message(STATUS "Using FetchContent GLFW")
    endif()
endif()

find_package(glm QUIET)
if(NOT glm_FOUND)
    message(STATUS "glm not found via find_package — trying FetchContent fallback")
    include(FetchContent)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1)
    FetchContent_MakeAvailable(glm)
    # glm 1.0.x provides glm::glm-header-only (CMake <3.19 compat) and
    # glm::glm; normalise to glm::glm for the server link below.
    if(TARGET glm::glm-header-only AND NOT TARGET glm::glm)
        add_library(glm::glm ALIAS glm::glm-header-only)
    endif()
    message(STATUS "Using FetchContent glm")
endif()

# Reusable engine library (game-agnostic). The game links it; the server does
# not need it.
add_subdirectory(engine)

# SpatialKit provides the continuous (segment/swept) geometry tests used for
# fast projectiles. Prefer the live checkout next to this repository; fall back
# to the vendored header snapshot so CI and fresh clones build without it.
set(SPATIALKIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Einzelkleinprojekt/spartialkit"
    CACHE PATH "SpatialKit checkout (header-only geometry is used here)")
if(NOT EXISTS "${SPATIALKIT_DIR}/include/spatialkit/geometry.hpp")
    set(SPATIALKIT_DIR "${CMAKE_SOURCE_DIR}/third_party/spatialkit")
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/stb
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/game
    ${CMAKE_SOURCE_DIR}/src/network
    ${CMAKE_SOURCE_DIR}/src/systems
    ${CMAKE_SOURCE_DIR}/src/ui
    ${SPATIALKIT_DIR}/include
)

# Source files
set(CORE_SOURCES
    src/core/game.cpp
    src/core/game_input.cpp
    src/core/game_loop.cpp
    src/core/game_gameplay.cpp
    src/core/game_hud.cpp
    src/core/game_menus.cpp
    src/core/game_server.cpp
    src/core/game_server_menu.cpp
    src/core/game_audio.cpp
    src/core/game_editor.cpp
    src/core/discord_presence.cpp
    src/core/settings.cpp
)

set(SYSTEMS_SOURCES
    src/systems/input.cpp
    src/systems/weapon.cpp
    src/systems/physics.cpp
    src/systems/enemy.cpp
    src/systems/projectile.cpp
    src/systems/player.cpp
    src/systems/game_state.cpp
)

set(NETWORK_SOURCES
    src/network/network.cpp
    src/network/server_config.cpp
)

# Portable bot AI + its dedicated-server host. The AI has no GL/GLFW
# dependency; server_bots.cpp is only linked into the headless server.
set(BOT_AI_SOURCES
    src/game/bot.cpp
)
set(SERVER_BOT_SOURCES
    src/network/server_bots.cpp
    src/network/server_bomb.cpp
)

set(UI_SOURCES
    src/ui/menu_core.cpp
    src/ui/menu_draw.cpp
    src/ui/menu_input.cpp
    src/ui/menu_pages.cpp
)

set(MAIN_SOURCES
    src/main.cpp
)

set(SERVER_SOURCES
    server_main.cpp
)

# Optional Discord Rich Presence (local IPC, no bot token). The client id is a
# public application identifier; see src/core/brand.hpp.
set(DISCORD_SOURCES
    third_party/discord-rpc/src/discord_rpc.cpp
    third_party/discord-rpc/src/rpc_connection.cpp
    third_party/discord-rpc/src/serialization.cpp
    third_party/discord-rpc/src/discord_register_stub.cpp
)
if(WIN32)
    list(APPEND DISCORD_SOURCES third_party/discord-rpc/src/connection_win.cpp)
else()
    list(APPEND DISCORD_SOURCES third_party/discord-rpc/src/connection_unix.cpp)
endif()

# The vendored client uses MSVC-style #pragma warning guards.
if(NOT MSVC)
    set_source_files_properties(${DISCORD_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-unknown-pragmas")
endif()

# Client executable. Rendering, maps, audio and GLAD are provided by the
# extracted cube_engine library.
add_executable(game
    ${MAIN_SOURCES}
    ${CORE_SOURCES}
    ${SYSTEMS_SOURCES}
    ${NETWORK_SOURCES}
    ${BOT_AI_SOURCES}
    ${UI_SOURCES}
    ${DISCORD_SOURCES}
)

target_include_directories(game PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/discord-rpc/include
    ${CMAKE_SOURCE_DIR}/third_party/rapidjson
)

target_link_libraries(game PRIVATE
    cube_engine
    OpenGL::GL
    glfw
    Threads::Threads
    ${PLATFORM_LIBS}
)
if(TARGET glm::glm)
    target_link_libraries(game PRIVATE glm::glm)
endif()

# Server executable (headless, no OpenGL/GLFW needed)
add_executable(server
    ${SERVER_SOURCES}
    ${NETWORK_SOURCES}
    ${BOT_AI_SOURCES}
    ${SERVER_BOT_SOURCES}
)

target_include_directories(server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/network
)

target_link_libraries(server PRIVATE
    glm::glm
    Threads::Threads
    ${PLATFORM_LIBS}
)

# Regression test for fast-projectile hit detection (uses SpatialKit geometry).
add_executable(swept_hit_test
    tests/swept_hit_test.cpp
)
if(MSVC)
    target_compile_options(swept_hit_test PRIVATE /W4)
else()
    target_compile_options(swept_hit_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME swept_hit_test COMMAND swept_hit_test)

# Set output directories
set_target_properties(game server swept_hit_test
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy assets to build directory. The trailing slash keeps run.sh's
# assets/ -> build/assets symlink intact (without it, cmake replaces the link
# with a real directory, and later re-runs fail with "assets already exists").
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets/ $<TARGET_FILE_DIR:game>/assets/
)

# Windows: stage the runtime + software-rendering DLLs next to the exes so a
# double-click (or run.bat) works without the MSYS2 toolchain on PATH, and so
# the Mesa fallback in game.cpp finds opengl32_sw.dll + its sibling DLLs.
#   - MinGW runtime + glfw3.dll: from the toolchain's bin dir when CMake is
#     driven from inside MSYS2 (where CMAKE_PREFIX_PATH / registry layout
#     exposes /ucrt64/bin), else from the vendored bundle in .mingw-dlls.
#   - Mesa (VMs without a GPU driver): mesa-deps/x64/* -> opengl32_sw.dll
#     (renamed — the system opengl32.dll must stay the default), siblings
#     keep their names. The game copies opengl32_sw.dll to opengl32.dll
#     only when WGL fails (or CUBE_RENDERER=software).
# Nothing here runs on Linux (guarded by WIN32).
if(WIN32)
    set(_mingw_bin "")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # An MSYS2/UCRT64 cmake invocation has the toolchain bin dir on PATH
        # at configure time; find_program locates the compiler's DLLs.
        find_program(_cube_gcc NAMES gcc.exe x86_64-w64-mingw32-gcc.exe PATHS
            "C:/msys64/ucrt64/bin" "C:/msys2/ucrt64/bin"
            "C:/msys64/mingw64/bin" "C:/msys2/mingw64/bin")
        if(_cube_gcc)
            get_filename_component(_mingw_bin "${_cube_gcc}" DIRECTORY)
        endif()
    endif()

    foreach(_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll glfw3.dll)
        if(_mingw_bin AND EXISTS "${_mingw_bin}/${_dll}")
            add_custom_command(TARGET game POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_mingw_bin}/${_dll}" "$<TARGET_FILE_DIR:game>/${_dll}")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/.mingw-dlls/${_dll}")
            add_custom_command(TARGET game POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/.mingw-dlls/${_dll}" "$<TARGET_FILE_DIR:game>/${_dll}")
        endif()
    endforeach()

    # Mesa software renderer (llvmpipe/D3D12) for GPU-less VMs. Installed
    # repeatedly so a fresh configure always re-stages it.
    if(EXISTS "${CMAKE_SOURCE_DIR}/mesa-deps/x64/opengl32.dll")
        add_custom_command(TARGET game POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/mesa-deps/x64/libgallium_wgl.dll" "$<TARGET_FILE_DIR:game>/libgallium_wgl.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/mesa-deps/x64/libdlclose-skip.dll" "$<TARGET_FILE_DIR:game>/libdlclose-skip.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/mesa-deps/x64/opengl32.dll" "$<TARGET_FILE_DIR:game>/opengl32_sw.dll"
        )
        message(STATUS "Mesa software renderer will be staged as opengl32_sw.dll (auto-fallback for VMs without a GPU)")
    endif()
endif()
