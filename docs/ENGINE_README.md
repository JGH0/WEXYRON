# Modern OpenGL Game Engine

> **Status (16.11.2026):** This document describes the earlier `src/engine/`
> prototype. The reusable engine used by the game has been extracted to
> `engine/` and builds as the `cube_engine` library. See
> [`engine/README.md`](engine/README.md) for the current architecture and the
> boundary rules. The prototype is kept for reference and is not part of the
> build.

A powerful, modular game engine built with C++ and OpenGL that provides a strong foundation for any type of game.

## Architecture Overview

The engine follows a modular design with clear separation of concerns:

```
Engine (Core)
├── Window Management
├── Rendering System
├── Input Management
├── Scene Management
├── Resource Management
├── Physics Engine
├── Audio Manager
└── Network Manager
```

## Key Features

### 🏗️ **Strong Core Engine**
- **Modular Architecture**: Each system is independent but can communicate through the Engine
- **Component-Based**: Easy to extend and customize
- **Event-Driven**: Uses callbacks for loose coupling
- **Resource Management**: Automatic loading, caching, and cleanup
- **Scene System**: Easy management of game states and transitions

### 🎮 **Game Systems**
- **Input Management**: Keyboard, mouse, and gamepad support with action mapping
- **Physics Engine**: Rigid body dynamics, collision detection, and forces
- **Audio System**: 3D positional audio, streaming, and effects
- **Rendering**: Modern OpenGL with shaders, lighting, and post-processing
- **Networking**: Client-server and peer-to-peer communication

### 🛠️ **Developer-Friendly**
- **Clean API**: Simple, intuitive interfaces
- **Extensible**: Easy to add new systems and components
- **Performance**: Optimized for real-time applications
- **Debug Support**: Built-in debugging and profiling tools

## Quick Start

### Basic Usage

```cpp
#include "game/game_application.hpp"

int main() {
    GameApplication app;
    
    if (!app.initialize()) {
        return -1;
    }
    
    app.run();
    return 0;
}
```

### Creating a Game Scene

```cpp
class MyGameScene : public Scene {
public:
    MyGameScene(const std::string& name) : Scene(name) {}
    
    void onLoad() override {
        // Load resources and setup game objects
        auto resourceMgr = getSceneManager()->getEngine()->getResourceManager();
        auto texture = resourceMgr->loadTexture("player.png");
    }
    
    void update(float deltaTime) override {
        // Update game logic
        auto inputMgr = getEngine()->getInputManager();
        if (inputMgr->isActionJustPressed("Jump")) {
            // Handle jump
        }
    }
    
    void render(float deltaTime) override {
        // Render game objects
        auto renderer = getEngine()->getRenderer();
        renderer->renderCube(position, size, color);
    }
};
```

### Setting Up Input

```cpp
void setupInput() {
    auto inputMgr = engine->getInputManager();
    
    // Map actions to keys
    inputMgr->mapAction("MoveForward", InputManager::Key::W);
    inputMgr->mapAction("MoveBackward", InputManager::Key::S);
    inputMgr->mapAction("Jump", InputManager::Key::Space);
    inputMgr->mapAction("Shoot", InputManager::MouseButton::Left);
    
    // Set up callbacks
    inputMgr->setActionCallback("Jump", []() {
        std::cout << "Jump pressed!" << std::endl;
    });
}
```

### Resource Management

```cpp
void loadResources() {
    auto resourceMgr = engine->getResourceManager();
    
    // Add search paths
    resourceMgr->addSearchPath("assets/textures");
    resourceMgr->addSearchPath("assets/shaders");
    
    // Load resources
    auto texture = resourceMgr->loadTexture("player.png");
    auto shader = resourceMgr->loadShader("basic.vert", "basic.frag");
    
    // Resources are automatically cached and managed
}
```

## Engine Systems

### Engine Core
The main Engine class coordinates all systems and provides the main loop.

### Window Management
- GLFW-based window creation and management
- Event handling for resize, focus, and close events
- Cursor management and input event routing

### Rendering System
- Modern OpenGL rendering pipeline
- Shader management and uniforms
- Texture and material systems
- Lighting and shadows
- Post-processing effects
- Debug rendering utilities

### Input Management
- Unified input system for keyboard, mouse, and gamepad
- Action mapping and abstraction
- Event callbacks and polling
- Mouse delta and scroll handling

### Scene Management
- Scene-based organization
- Scene transitions with effects
- Scene stack for overlays and menus
- Automatic scene lifecycle management

### Resource Management
- Automatic resource loading and caching
- Search path system
- Memory management and cleanup
- Support for textures, shaders, meshes, audio, and fonts

### Physics Engine
- Rigid body dynamics
- Collision detection and response
- Forces and impulses
- Raycasting and spatial queries
- Debug visualization

### Audio Manager
- 3D positional audio
- Streaming for music
- Sound effects with effects
- Volume control and audio groups
- Audio listener management

### Network Manager
- Client-server architecture
- Peer-to-peer support
- Reliable and unreliable messaging
- Packet serialization
- Connection management

## File Structure

```
src/
├── engine/                 # Core engine systems
│   ├── engine.hpp/cpp      # Main engine class
│   ├── window.hpp/cpp      # Window management
│   ├── renderer.hpp/cpp    # Rendering system
│   ├── input_manager.hpp/cpp # Input management
│   ├── scene_manager.hpp/cpp # Scene management
│   ├── resource_manager.hpp/cpp # Resource management
│   ├── physics_engine.hpp/cpp # Physics simulation
│   ├── audio_manager.hpp/cpp # Audio system
│   └── network_manager.hpp/cpp # Networking
├── game/                   # Game-specific code
│   ├── game_application.hpp/cpp # Main game application
│   ├── game_scene.hpp/cpp  # Game scene implementation
│   └── menu_scene.hpp/cpp  # Menu scene implementation
├── systems/               # Legacy systems (being migrated)
└── main_new.cpp           # New main function
```

## Benefits

### ✅ **Strong Abstraction**
- Clean separation between engine and game code
- Easy to test and maintain
- Reusable across different projects

### ✅ **Extensible Design**
- Easy to add new systems
- Plugin-style architecture
- Component-based entities

### ✅ **Performance Optimized**
- Efficient resource management
- Minimal overhead
- Modern C++ practices

### ✅ **Developer Experience**
- Clear documentation
- Intuitive API
- Good error handling

### ✅ **Production Ready**
- Robust error handling
- Memory management
- Cross-platform support

## Migration from Legacy Code

The new engine architecture is designed to gradually replace the legacy systems:

1. **Core Systems**: Window, Renderer, Input are already implemented
2. **Game Logic**: Migrate from monolithic `game.cpp` to scene-based architecture
3. **Physics**: Replace legacy physics with new physics engine
4. **Resources**: Use centralized resource management
5. **Audio/Network**: Add new capabilities

## Next Steps

1. Implement remaining engine system stubs
2. Create resource implementations (Texture, Shader, Mesh, etc.)
3. Add more scene examples
4. Implement physics engine backend
5. Add audio system backend
6. Create comprehensive tests
7. Add profiling and debugging tools

This engine provides a solid foundation that can handle any type of game, from simple 2D games to complex 3D applications.
