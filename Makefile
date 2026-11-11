# Cross-platform Makefile for Cube CS Lite.
#
#   Linux    make -j"$$(nproc)"
#   macOS    make -j"$$(sysctl -n hw.ncpu)"      (brew install glfw glm)
#   Windows  make           -> delegates to Makefile.windows under MSYS2/MinGW
#
# CMakeLists.txt builds the same sources and is the other supported entry point.

# Windows needs different system libraries, .exe targets and DLL staging, so it
# has its own makefile. Delegate there so a plain `make` works on every platform.
ifneq (,$(filter Windows_NT MINGW% MSYS% CYGWIN%,$(OS) $(shell uname -s)))
include Makefile.windows
else

CXX = g++

# Emit .d files listing every header each object includes, so that editing a
# header (e.g. game.hpp, which defines sizeof(Game)) recompiles every object
# that uses it. Without this, stale objects disagree about the Game layout and
# silently corrupt memory (game members get written at the wrong offsets).
DEPFLAGS = -MMD -MP

# Shared flags. CPPFLAGS is kept separate so the platform blocks below can add
# include paths (Homebrew on macOS) without repeating the common ones.
CPPFLAGS = -I./include -I./include/stb -I./src
CXXFLAGS = -std=c++17 -Wall $(CPPFLAGS)

# Platform-specific libraries.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # No libGL on macOS: OpenGL is a framework. Homebrew's prefix is outside
    # the default compiler/linker search paths, and differs per architecture
    # (/opt/homebrew on Apple Silicon, /usr/local on Intel).
    BREW_PREFIX ?= $(shell brew --prefix 2>/dev/null || echo /usr/local)
    CPPFLAGS += -I$(BREW_PREFIX)/include
    LDFLAGS = -L$(BREW_PREFIX)/lib -lglfw \
	-framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo \
	-pthread
else ifeq ($(UNAME_S),Linux)
    LDFLAGS = -lglfw -lGL -ldl -pthread
else
    $(error Unsupported platform '$(UNAME_S)' — use Makefile.windows on Windows or CMakeLists.txt)
endif

SRC_DIR = src
CORE_DIR = src/core
SYSTEMS_DIR = src/systems
ENGINE_DIR = src/engine
GAME_DIR = src/game
NETWORK_DIR = src/network
UI_DIR = src/ui
BUILD_DIR = build/obj
BIN_DIR = build/bin
TARGET = $(BIN_DIR)/game
SERVER_TARGET = $(BIN_DIR)/server

# Source files
CORE_SOURCES = $(CORE_DIR)/game.cpp $(CORE_DIR)/game_input.cpp $(CORE_DIR)/game_loop.cpp $(CORE_DIR)/game_gameplay.cpp $(CORE_DIR)/game_hud.cpp $(CORE_DIR)/game_menus.cpp $(CORE_DIR)/game_server.cpp $(CORE_DIR)/game_server_menu.cpp $(CORE_DIR)/game_audio.cpp $(CORE_DIR)/game_editor.cpp $(CORE_DIR)/settings.cpp $(CORE_DIR)/renderer.cpp $(CORE_DIR)/camera.cpp $(CORE_DIR)/map.cpp $(CORE_DIR)/map_loader.cpp $(CORE_DIR)/shader.cpp $(CORE_DIR)/texture.cpp
SYSTEMS_SOURCES = $(SYSTEMS_DIR)/input.cpp $(SYSTEMS_DIR)/weapon.cpp $(SYSTEMS_DIR)/physics.cpp $(SYSTEMS_DIR)/enemy.cpp $(SYSTEMS_DIR)/projectile.cpp $(SYSTEMS_DIR)/player.cpp $(SYSTEMS_DIR)/game_state.cpp
NETWORK_SOURCES = $(NETWORK_DIR)/network.cpp $(NETWORK_DIR)/server_config.cpp
UI_SOURCES = $(UI_DIR)/menu_core.cpp $(UI_DIR)/menu_draw.cpp $(UI_DIR)/menu_input.cpp $(UI_DIR)/menu_pages.cpp
ENGINE_SOURCES = $(ENGINE_DIR)/audio_manager.cpp
MINIAUDIO_SOURCES = third_party/miniaudio/miniaudio.c
MAIN_SOURCES = $(SRC_DIR)/main.cpp
SERVER_SOURCES = server_main.cpp $(NETWORK_SOURCES)
GLAD_SOURCES = glad.c
ALL_SOURCES = $(CORE_SOURCES) $(SYSTEMS_SOURCES) $(NETWORK_SOURCES) $(UI_SOURCES) $(ENGINE_SOURCES) $(MINIAUDIO_SOURCES) $(MAIN_SOURCES) $(GLAD_SOURCES)

# Object files
CORE_OBJECTS = $(addprefix $(BUILD_DIR)/core/, $(notdir $(CORE_SOURCES:.cpp=.o)))
SYSTEMS_OBJECTS = $(addprefix $(BUILD_DIR)/systems/, $(notdir $(SYSTEMS_SOURCES:.cpp=.o)))
NETWORK_OBJECTS = $(addprefix $(BUILD_DIR)/network/, $(notdir $(NETWORK_SOURCES:.cpp=.o)))
UI_OBJECTS = $(addprefix $(BUILD_DIR)/ui/, $(notdir $(UI_SOURCES:.cpp=.o)))
ENGINE_OBJECTS = $(addprefix $(BUILD_DIR)/engine/, $(notdir $(ENGINE_SOURCES:.cpp=.o)))
MAIN_OBJECT = $(BUILD_DIR)/main.o
SERVER_MAIN_OBJECT = $(BUILD_DIR)/server_main.o
GLAD_OBJECT = $(BUILD_DIR)/glad.o
MINIAUDIO_OBJECT = $(BUILD_DIR)/third_party/miniaudio.o
ALL_OBJECTS = $(CORE_OBJECTS) $(SYSTEMS_OBJECTS) $(NETWORK_OBJECTS) $(UI_OBJECTS) $(ENGINE_OBJECTS) $(MAIN_OBJECT) $(GLAD_OBJECT) $(MINIAUDIO_OBJECT)
SERVER_OBJECTS = $(SERVER_MAIN_OBJECT) $(NETWORK_OBJECTS)

all: $(TARGET) $(SERVER_TARGET)

client: $(TARGET)

server: $(SERVER_TARGET)

# `deps` is order-only: it runs (and can fail with a helpful message) before
# linking, but never forces a relink or appears in $^.
$(TARGET): $(ALL_OBJECTS) | deps-check $(BIN_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(SERVER_TARGET): $(SERVER_OBJECTS) | deps-check $(BIN_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

# Core object files
$(BUILD_DIR)/core/%.o: $(CORE_DIR)/%.cpp | $(BUILD_DIR)/core
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# Systems object files
$(BUILD_DIR)/systems/%.o: $(SYSTEMS_DIR)/%.cpp | $(BUILD_DIR)/systems
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# Network object files
$(BUILD_DIR)/network/%.o: $(NETWORK_DIR)/%.cpp | $(BUILD_DIR)/network
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# UI object files
$(BUILD_DIR)/ui/%.o: $(UI_DIR)/%.cpp | $(BUILD_DIR)/ui
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# Engine object files
$(BUILD_DIR)/engine/%.o: $(ENGINE_DIR)/%.cpp | $(BUILD_DIR)/engine
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# Game object files
$(BUILD_DIR)/game/%.o: $(GAME_DIR)/%.cpp | $(BUILD_DIR)/game
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# Main object file
$(BUILD_DIR)/main_new.o: $(SRC_DIR)/main_new.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# Main object file
$(BUILD_DIR)/main.o: $(SRC_DIR)/main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# Server main object file
$(BUILD_DIR)/server_main.o: server_main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# miniaudio implementation (third_party/miniaudio). Its header is large and
# stable, so it is compiled once into its own object.
$(MINIAUDIO_OBJECT): $(MINIAUDIO_SOURCES) | $(BUILD_DIR)/third_party
	$(CXX) $(CXXFLAGS) -c $< -o $@

# GLAD object file (C, no header deps to track)
$(GLAD_OBJECT): $(GLAD_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Create directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/core:
	mkdir -p $(BUILD_DIR)/core

$(BUILD_DIR)/systems:
	mkdir -p $(BUILD_DIR)/systems

$(BUILD_DIR)/network:
	mkdir -p $(BUILD_DIR)/network

$(BUILD_DIR)/ui:
	mkdir -p $(BUILD_DIR)/ui

$(BUILD_DIR)/engine:
	mkdir -p $(BUILD_DIR)/engine

$(BUILD_DIR)/game:
	mkdir -p $(BUILD_DIR)/game

$(BUILD_DIR)/third_party:
	mkdir -p $(BUILD_DIR)/third_party

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Preflight: fail early with install instructions instead of a bare
# "'GLFW/glfw3.h' file not found" deep in the first compile.
# `deps-check` is silent so it can run as a build prerequisite; `deps` reports.
deps: deps-check
	@echo "Dependencies OK"

deps-check:
	@printf '#include <GLFW/glfw3.h>\n#include <glm/glm.hpp>\n' \
	    | $(CXX) -std=c++17 $(CPPFLAGS) -E -x c++ - >/dev/null 2>&1 \
	    || { \
	        echo "error: GLFW and/or GLM headers not found." >&2; \
	        echo "  macOS          brew install glfw glm" >&2; \
	        echo "  Debian/Ubuntu  sudo apt-get install libglfw3-dev libglm-dev" >&2; \
	        echo "  Arch           sudo pacman -S glfw glm" >&2; \
	        exit 1; \
	    }

run: $(TARGET)
	./$(TARGET)

run-server: $(SERVER_TARGET)
	./$(SERVER_TARGET)

debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

help:
	@echo "Targets:"
	@echo "  all         build client and server (default)"
	@echo "  client      build $(TARGET)"
	@echo "  server      build $(SERVER_TARGET)"
	@echo "  run         build and launch the client"
	@echo "  run-server  build and launch the server"
	@echo "  debug       client build with -g -DDEBUG"
	@echo "  deps        check that GLFW/GLM headers are installed"
	@echo "  clean       remove build artifacts"

# Header dependency files produced by -MMD (included after the first target so
# they never become the default goal).
-include $(ALL_OBJECTS:.o=.d)

.DELETE_ON_ERROR:

.PHONY: all clean run run-server debug client server deps deps-check help

endif # !Windows
