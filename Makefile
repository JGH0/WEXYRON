CXX = g++
CXXFLAGS = -std=c++17 -Wall -I./include -I./include/stb -I./src
# Emit .d files listing every header each object includes, so that editing a
# header (e.g. game.hpp, which defines sizeof(Game)) recompiles every object
# that uses it. Without this, stale objects disagree about the Game layout and
# silently corrupt memory (game members get written at the wrong offsets).
DEPFLAGS = -MMD -MP
LDFLAGS = -lglfw -lGL -ldl -pthread

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
CORE_SOURCES = $(CORE_DIR)/game.cpp $(CORE_DIR)/game_input.cpp $(CORE_DIR)/game_loop.cpp $(CORE_DIR)/game_gameplay.cpp $(CORE_DIR)/game_hud.cpp $(CORE_DIR)/game_menus.cpp $(CORE_DIR)/game_server.cpp $(CORE_DIR)/game_server_menu.cpp $(CORE_DIR)/game_editor.cpp $(CORE_DIR)/settings.cpp $(CORE_DIR)/renderer.cpp $(CORE_DIR)/camera.cpp $(CORE_DIR)/map.cpp $(CORE_DIR)/map_loader.cpp $(CORE_DIR)/shader.cpp $(CORE_DIR)/texture.cpp
SYSTEMS_SOURCES = $(SYSTEMS_DIR)/input.cpp $(SYSTEMS_DIR)/weapon.cpp $(SYSTEMS_DIR)/physics.cpp $(SYSTEMS_DIR)/enemy.cpp $(SYSTEMS_DIR)/projectile.cpp $(SYSTEMS_DIR)/player.cpp $(SYSTEMS_DIR)/game_state.cpp
NETWORK_SOURCES = $(NETWORK_DIR)/network.cpp
UI_SOURCES = $(UI_DIR)/menu_core.cpp $(UI_DIR)/menu_draw.cpp $(UI_DIR)/menu_input.cpp $(UI_DIR)/menu_pages.cpp
ENGINE_SOURCES =
MAIN_SOURCES = $(SRC_DIR)/main.cpp
SERVER_SOURCES = server_main.cpp $(NETWORK_SOURCES)
GLAD_SOURCES = glad.c
ALL_SOURCES = $(CORE_SOURCES) $(SYSTEMS_SOURCES) $(NETWORK_SOURCES) $(UI_SOURCES) $(ENGINE_SOURCES) $(MAIN_SOURCES) $(GLAD_SOURCES)

# Object files
CORE_OBJECTS = $(addprefix $(BUILD_DIR)/core/, $(notdir $(CORE_SOURCES:.cpp=.o)))
SYSTEMS_OBJECTS = $(addprefix $(BUILD_DIR)/systems/, $(notdir $(SYSTEMS_SOURCES:.cpp=.o)))
NETWORK_OBJECTS = $(addprefix $(BUILD_DIR)/network/, $(notdir $(NETWORK_SOURCES:.cpp=.o)))
UI_OBJECTS = $(addprefix $(BUILD_DIR)/ui/, $(notdir $(UI_SOURCES:.cpp=.o)))
ENGINE_OBJECTS = $(addprefix $(BUILD_DIR)/engine/, $(notdir $(ENGINE_SOURCES:.cpp=.o)))
MAIN_OBJECT = $(BUILD_DIR)/main.o
SERVER_MAIN_OBJECT = $(BUILD_DIR)/server_main.o
GLAD_OBJECT = $(BUILD_DIR)/glad.o
ALL_OBJECTS = $(CORE_OBJECTS) $(SYSTEMS_OBJECTS) $(NETWORK_OBJECTS) $(UI_OBJECTS) $(ENGINE_OBJECTS) $(MAIN_OBJECT) $(GLAD_OBJECT)
SERVER_OBJECTS = $(SERVER_MAIN_OBJECT) $(NETWORK_OBJECTS)

all: $(TARGET) $(SERVER_TARGET)

client: $(TARGET)

server: $(SERVER_TARGET)

$(TARGET): $(ALL_OBJECTS) | $(BIN_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(SERVER_TARGET): $(SERVER_OBJECTS) | $(BIN_DIR)
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

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

run: $(TARGET)
	./$(TARGET)

run-server: $(SERVER_TARGET)
	./$(SERVER_TARGET)

debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

# Header dependency files produced by -MMD (included after the first target so
# they never become the default goal).
-include $(ALL_OBJECTS:.o=.d)

.PHONY: all clean run run-server debug client server
