# OpenGL Game Project Structure

## Directory Layout

```
OpenGl-Game/
├── assets/                 # Game assets
│   ├── textures/          # Texture files (PNG, JPG, etc.)
│   ├── models/            # glTF 2.0 models (.glb), see MODELS.md
│   ├── maps/              # Map definition files
│   └── shaders/           # OpenGL shader files
├── build/                 # Build output
│   ├── obj/               # Object files
│   │   └── core/          # Core object files
│   └── bin/               # Executable files
├── docs/                  # Generated documentation
├── include/               # External libraries
│   ├── glad/              # GLAD OpenGL loader
│   ├── KHR/               # Khronos headers
│   └── stb/               # STB image library
├── src/                   # Source code
│   ├── core/              # Core game systems
│   │   ├── camera.cpp/.hpp
│   │   ├── game.cpp/.hpp
│   │   ├── map.cpp/.hpp
│   │   ├── renderer.cpp/.hpp
│   │   ├── shader.cpp/.hpp
│   │   └── texture.cpp/.hpp
│   └── main.cpp           # Application entry point
├── config/                # Configuration files (future)
└── Makefile               # Build configuration
```

## Build System

The project uses a custom Makefile that:
- Compiles all core systems in `src/core/`
- Places object files in `build/obj/core/`
- Places executables in `build/bin/`
- Supports debug builds with `make debug`
- Includes proper header paths for external libraries

## Asset Loading

- **Maps**: Loaded from `assets/maps/` with `.map` extension
- **Textures**: Loaded from `assets/textures/` with PNG/JPG support
- **Shaders**: Loaded from `assets/shaders/` with `.glsl` extension

## Usage

```bash
# Build the project
make

# Build with debug symbols
make debug

# Run the game
make run

# Clean build artifacts
make clean
```

## Map System

The map system supports:
- Textured blocks with collision detection
- Multiple texture types per map
- Simple text-based map format
- Dynamic map loading at runtime

Example map loading in code:
```cpp
game.loadMap("simple_test.map");
```
