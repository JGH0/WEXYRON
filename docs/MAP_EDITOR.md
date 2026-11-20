# Map Editor

The map editor is a self-contained level builder: open it from the main menu
(`MAP EDITOR`), build with the mouse, and save as many `.map` files as you like.
Saved maps appear in `MAP SELECTION` immediately (the folder is rescanned after
a save).

## Screen layout

```
+---------------------------------------------------------------+
| MAP EDITOR   NEW  OPEN  SAVE  HELP  EXIT                      |
+----------+----------------------------------------+-----------+
| TOOLS    |                                        | BRUSH     |
| BUILD    |            3D view (mouse aims)        | size/rot  |
| PAINT    |                                        | TEXTURE   |
| ERASE    |                                        | VIEW      |
| PICK     |                                        | MAP STATS |
| SPAWN    |                                        |           |
| UNDO REDO|                                        |           |
+----------+----------------------------------------+-----------+
| controls and hotkeys                                          |
+---------------------------------------------------------------+
```

Every button is clickable. The right-hand inspector changes with the active
tool, so it only shows settings that matter.

## Controls

| Input | Action |
| --- | --- |
| `W A S D` | Fly the camera |
| `Q` / `E` | Down / up |
| `Shift` | Fast camera |
| Right mouse drag | Look around |
| Left mouse | Apply the active tool at the cursor |
| Wheel | Placement distance (when nothing is under the cursor) |
| `Ctrl` + wheel | Cycle textures |
| `B`, `P`, `X`, `I`, `M` | Build / Paint / Erase / Pick / Spawn tool |
| `1` - `8` | Choose texture |
| `[` / `]` | Shrink / grow the brush uniformly |
| `R` | Reset brush rotation |
| `Up` / `Down` | Placement distance |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+S` | Save (current name) |
| `Ctrl+N` / `Ctrl+O` | New map / open an existing map |
| `F1` | Controls reference |
| `Esc` | Leave the editor (asks first if there are unsaved changes) |

## Tools

- **Build** — places the current brush on the face under the cursor. The brush
  size (`SIZE X/Y/Z`), yaw and pitch are shown in green while aiming.
- **Paint** — replaces the texture of the aimed block.
- **Erase** — removes the aimed block, or a spawn point / bomb site when no
  block is under the cursor.
- **Pick** — copies the aimed block's size, rotation and texture into the brush.
- **Spawn** — places deathmatch, team A/B spawns or bomb sites A/B.
  Spawn markers are drawn in the world with their colours.

## Building maps faster

- Start from **NEW** and keep *STARTER ROOM* on: you get a floor, four walls,
  cover, four deathmatch spawns, two team spawns and two bomb sites.
- Use a large brush (`SIZE` up to 32) for floors and walls, then switch to a
  small one for detail.
- **PICK** copies an existing block, so repeated structures match exactly.
- **UNDO** covers every edit, including new/open map actions; the history keeps
  the last 40 changes.
- The map is only written to disk by **SAVE** (or `Ctrl+S`). The title bar shows
  `*UNSAVED` until then. Saving an existing name asks for nothing extra but the
  Save dialog warns when it will overwrite a file.

## File format

Maps live in `assets/maps/*.map`. The editor writes this text format:

```
name <map_name>
dimensions <width> <height> <depth>

block <x> <y> <z> <sx> <sy> <sz> <texture> <type> <solid> \
      <r> <g> <b> <yaw> <pitch>

spawn <x> <y> <z> <yaw> <pitch>
team_spawn <x> <y> <z> <team> <yaw> <pitch>
bomb_site <x> <y> <z> <A|B>
```

The eight stock textures (0-7) are grass, stone, brick, metal, wood, sand, ice
and dirt. They are loaded automatically for every map.
