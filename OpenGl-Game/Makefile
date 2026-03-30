CXX = g++
CXXFLAGS = -std=c++11 -Wall -I./include -I./include/stb
LDFLAGS = -lglfw -lGL -ldl

SRC_DIR = src
CORE_DIR = src/core
SYSTEMS_DIR = src/systems
ENGINE_DIR = src/engine
GAME_DIR = src/game
BUILD_DIR = build/obj
BIN_DIR = build/bin
TARGET = $(BIN_DIR)/game

# Source files
CORE_SOURCES = $(CORE_DIR)/game.cpp $(CORE_DIR)/renderer.cpp $(CORE_DIR)/camera.cpp $(CORE_DIR)/map.cpp $(CORE_DIR)/shader.cpp $(CORE_DIR)/texture.cpp
SYSTEMS_SOURCES = $(SYSTEMS_DIR)/input.cpp $(SYSTEMS_DIR)/weapon.cpp $(SYSTEMS_DIR)/physics.cpp $(SYSTEMS_DIR)/enemy.cpp $(SYSTEMS_DIR)/projectile.cpp $(SYSTEMS_DIR)/player.cpp $(SYSTEMS_DIR)/game_state.cpp
ENGINE_SOURCES =
MAIN_SOURCES = $(SRC_DIR)/main.cpp
GLAD_SOURCES = glad.c
ALL_SOURCES = $(CORE_SOURCES) $(SYSTEMS_SOURCES) $(ENGINE_SOURCES) $(MAIN_SOURCES) $(GLAD_SOURCES)

# Object files
CORE_OBJECTS = $(addprefix $(BUILD_DIR)/core/, $(notdir $(CORE_SOURCES:.cpp=.o)))
SYSTEMS_OBJECTS = $(addprefix $(BUILD_DIR)/systems/, $(notdir $(SYSTEMS_SOURCES:.cpp=.o)))
ENGINE_OBJECTS = $(addprefix $(BUILD_DIR)/engine/, $(notdir $(ENGINE_SOURCES:.cpp=.o)))
MAIN_OBJECT = $(BUILD_DIR)/main.o
GLAD_OBJECT = $(BUILD_DIR)/glad.o
ALL_OBJECTS = $(CORE_OBJECTS) $(SYSTEMS_OBJECTS) $(ENGINE_OBJECTS) $(MAIN_OBJECT) $(GLAD_OBJECT)

all: $(TARGET)

$(TARGET): $(ALL_OBJECTS) | $(BIN_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

# Core object files
$(BUILD_DIR)/core/%.o: $(CORE_DIR)/%.cpp | $(BUILD_DIR)/core
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Systems object files
$(BUILD_DIR)/systems/%.o: $(SYSTEMS_DIR)/%.cpp | $(BUILD_DIR)/systems
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Engine object files
$(BUILD_DIR)/engine/%.o: $(ENGINE_DIR)/%.cpp | $(BUILD_DIR)/engine
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Game object files
$(BUILD_DIR)/game/%.o: $(GAME_DIR)/%.cpp | $(BUILD_DIR)/game
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Main object file
$(BUILD_DIR)/main_new.o: $(SRC_DIR)/main_new.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Main object file
$(BUILD_DIR)/main.o: $(SRC_DIR)/main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# GLAD object file
$(GLAD_OBJECT): $(GLAD_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Create directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/core:
	mkdir -p $(BUILD_DIR)/core

$(BUILD_DIR)/systems:
	mkdir -p $(BUILD_DIR)/systems

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

debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

.PHONY: all clean run debug
