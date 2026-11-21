# 3D Models (glTF 2.0)

The game uses **glTF 2.0**, packaged as **binary `.glb`**, for its authored
models. The runtime loader is part of the engine (`engine/include/engine/gltf_model.hpp`)
and has no external dependencies: it reads the JSON/binary glTF directly and
uses the stb_image already in the project.

> The old Assimp/OBJ loader in `src/core/model.cpp` is **legacy and not part of
> the build**. Use the glTF path below.

## Assets

| File | Used by | Animation |
| --- | --- | --- |
| `assets/models/player.glb` | remote players, practice enemies | `Idle`, `Walk`, `Shoot`, `Hit`, `Jump`, `Death` |
| `assets/models/bomb.glb` | planted bomb (bomb defusal) | `Blink` |
| `assets/models/weapon_pistol.glb` | first-person viewmodel | – (recoil/bob are applied by the game) |
| `assets/models/weapon_shotgun.glb` | first-person viewmodel | – |
| `assets/models/weapon_sniper.glb` | first-person viewmodel | – |
| `assets/models/weapon_grenade.glb` | first-person viewmodel | – |
| `assets/models/weapon_flashbang.glb` | first-person viewmodel | – |

The player model is authored in the same Box/Blockbench layout the procedural
`humanoid.hpp` used: feet at the origin, **+Y up**, facing **+Z**, roughly
1.8 units tall. It is skinned to a small skeleton (hips, spine, head, arms and
legs) and tinted per team at runtime.

## Authoring in Blockbench

1. Build the model from cuboids; keep the feet at `y = 0` and the front facing
   `+Z`. The player should fit the 0.8 × 1.8 body used by the hit boxes.
2. Add bones/pivots for the parts that animate (hips, spine, head, upper/lower
   arms, upper/lower legs). Rigid (one-bone-per-cuboid) weights are fine.
3. Create clips named `Idle`, `Walk`, `Shoot`, `Hit`, `Jump` and `Death` for
   characters, or `Blink` for the bomb. Clip names are looked up
   case-insensitively.
4. `File → Export → glTF`, choose **`.glb`**, embed textures, Y-up, and a unit
   scale that keeps the player at 1.8 units tall.
5. Save under `assets/models/`.

For weapons, author them in the same local space the viewmodel used before
(barrel towards `-Z`, grip near the origin). Keep hands and arms out of the
weapon file: first-person draws only the gun, and third-person attaches it to
the character's hand bone. The game applies the bob/sway/recoil transform, so
the file itself can stay static.

### Regenerating the built-in models

The checked-in assets are produced by a dependency-free script:

```bash
python3 tools/gen_gltf_models.py
```

Edit the box lists / clips in `tools/gen_gltf_models.py` if you want to change
the built-in player, bomb or weapons without opening Blockbench.

## Runtime API

```cpp
#include "engine/gltf_model.hpp"

GltfModel model;
if (!model.load("assets/models/player.glb")) {
    std::cerr << model.error() << std::endl;
}

const int walk = model.findAnimation("Walk");

GltfInstance instance;
instance.init(&model);

// Each frame: blend walk/idle and update the pose.
instance.setPose(walk, walkTime, walkWeight,
                 model.findAnimation("Idle"), idleTime, 1.0f - walkWeight);
instance.update();

// Queue the draw (the renderer batches it after the world boxes).
renderer.submitModel(&model, &instance, transform, teamColor);
```

`GltfInstance::setPose()` blends two clips (translation/scale lerp, rotation
slerp) and blends the result against the rest pose when the weights sum to
less than one. Negative clip indices mean "rest pose".

For the first-person pass use:

```cpp
renderer.submitViewModelModel(&model, &instance, viewSpaceTransform);
```

## Supported glTF features

- `.glb` and `.gltf` (external files or data-URI buffers/images)
- buffers, bufferViews, accessors (all component types, normalized integers,
  sparse accessors)
- `POSITION`, `NORMAL`, `TEXCOORD_0`, `COLOR_0`, `JOINTS_0`, `WEIGHTS_0`
- indexed and non-indexed TRIANGLES primitives
- node hierarchies with matrix or TRS transforms
- PBR metallic/roughness factors, base-colour and emissive textures,
  alpha modes `OPAQUE`/`MASK`/`BLEND`, double-sided materials
- skins with inverse bind matrices, GPU linear-blend skinning (32 bones)
- animation channels for translation/rotation/scale with `LINEAR`, `STEP` and
  `CUBICSPLINE` interpolation
- two-clip cross-fading per instance

Not supported (ignored): morph targets, geometry-changing extensions, cameras
and punctual lights, non-triangle primitive modes, and metallic/roughness,
normal or occlusion textures (the scalar factors are used instead).

## Troubleshooting

**Model does not appear**
- Check the console for `[models] ...` errors from `Game::initModels()`.
- Verify the file exists relative to the process working directory (the game
  searches for `assets/` next to the executable/project root).
- If loading fails, the game falls back to the old procedural boxes; if you see
  a plain cube, the asset is the problem, not the renderer.

**Model is too small / large**
- glTF units are metres. A Blockbench model exported at 1 block = 1 metre
  should match the 1.8-unit player. Scale the model in Blockbench and re-export
  (the game's outer transform is unit-scale for characters).

**Model faces the wrong way**
- Characters must face `+Z`. The game applies the same feet/yaw basis as the
  old procedural body (`humanoid::rootTransform`).

**Animation looks wrong / T-posed**
- Check the clip name and that the channels target the nodes you expect.
- The player expects `Idle`, `Walk`, `Shoot`, `Hit`, `Jump`, `Death`; the bomb
  expects `Blink`. Missing clips fall back to the rest pose.
