# Fails if the extracted engine includes game-specific code.
#
# Run via:
#   cmake -DENGINE_SOURCE_DIR=<repo>/engine -P check_boundary.cmake

if(NOT DEFINED ENGINE_SOURCE_DIR)
    message(FATAL_ERROR "ENGINE_SOURCE_DIR is not set")
endif()

file(GLOB_RECURSE ENGINE_FILES
    "${ENGINE_SOURCE_DIR}/include/*.hpp"
    "${ENGINE_SOURCE_DIR}/src/*.cpp"
)
list(LENGTH ENGINE_FILES ENGINE_FILE_COUNT)

set(VIOLATIONS 0)
foreach(ENGINE_FILE ${ENGINE_FILES})
    file(READ "${ENGINE_FILE}" CONTENT)
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"][^>\"]*[>\"]" INCLUDES "${CONTENT}")
    foreach(INCLUDE_LINE ${INCLUDES})
        if(INCLUDE_LINE MATCHES "core/game|game/|systems/|ui/|network/|humanoid|settings\\.hpp|menu")
            message(SEND_ERROR "Engine dependency violation in ${ENGINE_FILE}: ${INCLUDE_LINE}")
            math(EXPR VIOLATIONS "${VIOLATIONS} + 1")
        endif()
    endforeach()
endforeach()

if(VIOLATIONS GREATER 0)
    message(FATAL_ERROR "cube_engine is not game-agnostic (${VIOLATIONS} violation(s))")
endif()

message(STATUS "cube_engine boundary check passed (${ENGINE_FILE_COUNT} files)")
