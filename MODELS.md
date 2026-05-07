# 3D Model Loading System

This game now supports loading complex 3D models from Blender using Assimp library.

## Setup

The system uses Assimp for loading 3D models. Make sure you have Assimp installed:

### Linux (Arch/Manjaro)
```bash
sudo pacman -S assimp
```

### Linux (Ubuntu/Debian)
```bash
sudo apt-get install libassimp-dev
```

## Exporting from Blender

To export a model from Blender for use in the game:

1. Create your model in Blender
2. Select File > Export > Wavefront (.obj)
3. Make sure to check:
   - Include Normals
   - Include UVs
   - Include Materials
   - Apply Modifiers (optional, but recommended)
4. Save the .obj file and .mtl file in the `assets/models/` directory
5. Copy any texture images to the same directory or a subdirectory

## Model Format

The game supports the following model formats through Assimp:
- OBJ (recommended)
- FBX
- GLTF/GLB
- Collada (DAE)
- And many more

## Using Models in the Game

### Loading a Model

```cpp
#include "model.hpp"

// Load a model
Model* playerModel = new Model();
playerModel->loadModel("assets/models/player.obj");
```

### Rendering a Model

```cpp
#include "renderer.hpp"

// Render the model at a specific position with scale and rotation
renderer.drawModel(playerModel, 
                   glm::vec3(0.0f, 0.0f, 0.0f),  // position
                   glm::vec3(1.0f, 1.0f, 1.0f),  // scale
                   glm::vec3(0.0f, 0.0f, 0.0f)); // rotation (degrees)
```

### Example: Adding a Player Model

To use a custom model for the player instead of the default cube:

1. Add a Model pointer to your Game class in `game.hpp`:
```cpp
class Game {
private:
    Model* playerModel;
    // ... other members
};
```

2. Initialize the model in the Game constructor or init function:
```cpp
Game::Game(int width, int height) {
    // ... other initialization
    playerModel = new Model();
    playerModel->loadModel("assets/models/player.obj");
}
```

3. Render the model in your render function instead of the cube:
```cpp
void Game::render() {
    // ... other rendering
    
    // Render player with model
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, playerPos);
    model = glm::rotate(model, glm::radians(-camera.Yaw), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(1.0f, 1.8f, 1.0f)); // Human-like proportions
    
    renderer.drawModel(playerModel, playerPos, glm::vec3(1.0f, 1.8f, 1.0f), 
                      glm::vec3(0.0f, -camera.Yaw, 0.0f));
}
```

4. Clean up in the destructor:
```cpp
Game::~Game() {
    if (playerModel) delete playerModel;
    // ... other cleanup
}
```

## Texturing Your Models

1. Create your textures in Blender
2. UV unwrap your model
3. Apply textures in Blender
4. When exporting as OBJ, make sure to include materials
5. The texture images should be in the same directory as the .obj file
6. Supported texture formats: PNG, JPG, TGA, BMP

## Model Requirements

For best results, your models should:

- Have proper UV mapping
- Include normals for lighting
- Be centered at origin (0, 0, 0)
- Have reasonable polygon count (keep it optimized for real-time rendering)
- Be scaled appropriately (typically 1 unit = 1 meter in-game)
- Face the +Z direction (forward) for proper orientation

## Troubleshooting

### Model doesn't appear
- Check console for loading errors
- Verify the file path is correct
- Ensure the model file exists in the assets/models directory
- Check that Assimp is properly installed

### Textures don't load
- Make sure texture files are in the same directory as the .obj file
- Verify texture paths in the .mtl file are correct
- Check that stb_image is properly linked

### Model appears black
- Check that normals are included in the export
- Verify lighting is working in the scene
- Make sure the model shader is compiling correctly

### Model is too small/large
- Adjust the scale parameter in drawModel()
- Or scale the model in Blender before exporting

## Advanced Features

The model system supports:
- Multiple meshes per model (for complex objects)
- Multiple textures per mesh (diffuse, specular, normal maps)
- Tangent and bitangent data for normal mapping
- Automatic texture loading from material files

## Performance Tips

- Keep polygon counts reasonable for real-time rendering
- Use texture atlases when possible
- Consider using LOD (Level of Detail) models for distant objects
- Optimize your models by removing hidden faces
