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
    src/mesh.cpp
    src/renderer.cpp
    src/render_window.cpp
    src/backends/gl/gl_backend.cpp
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
