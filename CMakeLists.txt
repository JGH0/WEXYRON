cmake_minimum_required(VERSION 3.10)
project(CubeGame VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)   # std::filesystem, std::make_unique (also used by the Makefiles)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform detection
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS pthread dl)
endif()

# Threads for the worker pool / background map loading
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Find packages
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/stb
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/game
    ${CMAKE_SOURCE_DIR}/src/network
    ${CMAKE_SOURCE_DIR}/src/systems
    ${CMAKE_SOURCE_DIR}/src/ui
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
    src/core/game_editor.cpp
    src/core/settings.cpp
    src/core/renderer.cpp
    src/core/camera.cpp
    src/core/map.cpp
    src/core/map_loader.cpp
    src/core/shader.cpp
    src/core/texture.cpp
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

set(UI_SOURCES
    src/ui/menu_core.cpp
    src/ui/menu_draw.cpp
    src/ui/menu_input.cpp
    src/ui/menu_pages.cpp
)

set(ENGINE_SOURCES
)

set(MAIN_SOURCES
    src/main.cpp
)

set(SERVER_SOURCES
    server_main.cpp
)

set(GLAD_SOURCES
    glad.c
)

# Client executable
add_executable(game
    ${MAIN_SOURCES}
    ${CORE_SOURCES}
    ${SYSTEMS_SOURCES}
    ${NETWORK_SOURCES}
    ${UI_SOURCES}
    ${ENGINE_SOURCES}
    ${GLAD_SOURCES}
)

target_link_libraries(game
    OpenGL::GL
    glfw
    Threads::Threads
    ${PLATFORM_LIBS}
)

# Server executable (headless, no OpenGL/GLFW needed)
add_executable(server
    ${SERVER_SOURCES}
    ${NETWORK_SOURCES}
)

target_include_directories(server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/network
)

target_link_libraries(server
    glm::glm
    Threads::Threads
    ${PLATFORM_LIBS}
)

# Set output directories
set_target_properties(game server
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy assets to build directory
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:game>/assets
)
