# Proj4D

Proj4D is an intentionally small C++ block game with genuine 4D, 3D, and 2D
views of three shared worlds. The first startup menu selects the number of
spatial dimensions; the second continues the persistent Flat, Low, or High
world through that view.

The 4D mode does not reveal its world as a stack of 3D slices. A native 4D
perspective camera produces a solid 3D image:

```text
4D tesseract world -> 3D vision cube -> ordinary 2D display
```

Empty in-world space uses a blue sky in every mode, with pale dimension-native
clouds. In 4D, the external area beyond the 3D vision cube remains black.

The 3D mode is the conventional middle view:

```text
shared 4D world at w=0 -> ordinary 3D cube world -> 2D perspective display
```

It uses standard mouse look and WASD movement. Only cube faces that are not
blocked by a solid neighbor
are meshed, clipped at the near plane, and drawn in one depth-ordered triangle
batch so rear blocks and underground cavities stay behind solid terrain. Face
and wireframe colors identify grass, stone, wood, ores, and the other block
materials. Each cube has a stable coordinate-derived lighter or darker
variation. The targeted cube is surrounded by a white wireframe.

The 2D mode follows the same idea one dimension lower. It is not a side-on
platformer or an overhead view:

```text
2D square world -> 1D first-person vision line -> vertical display strip
```

The vision line spans the screen vertically and is drawn as a centered strip
whose width is one tenth of its height. This makes projected block edges
readable as rectangular intervals without widening the line to half of the
display. Only the nearest solid edge along each sight ray is shown, so terrain
cannot be seen through other blocks. In the reversed 2D palette, green
intervals use their material color. Every block receives a stable,
coordinate-derived lighter or darker shade so neighboring projected blocks
remain visible without flickering as the camera moves.

All three dimensions offer the same terrain choices:

- **Flat** is a perfectly level, infinitely wide and deep Hypercraft plains
  world. Its grass/dirt/stone layers have no caves, deterministic ores are
  allowed, and the plains biome adds no trees.
- **Low** is the complete output of Hypercraft's Flat generator, including its
  biome materials, caves, ores, fluids, and trees.
- **High** is the complete output of Hypercraft's Normal generator. It is
  represented internally by the compatibility name `TerrainMode::Density`.

The only unit of generation and storage is a real `16x16x16x16` chunk
containing 65,536 tesseracts. Nearby chunks generate lazily, a bounded
least-recently-used cache prevents infinite memory growth, and player edits
survive eviction and deterministic regeneration. The 3D and 2D modes are thin
adapters over the same chunks: a 3D cube `(x,y,z)` is exactly the tesseract
`(x,y,z,0)`, and a 2D square `(x,y)` is `(x,y,0,0)`. Slice edits therefore
appear in 4D, while edits outside a slice remain saved but invisible there.

In 4D, every tesseract has eight cubical boundary cells. A cell is visible only
when the neighboring tesseract on that side does not block it, including across
chunk boundaries. The renderer projects exposed cells into the vision cube and
suppresses wireframes across smooth faces and two-face ridges. Only true 4D
feature edges remain, colored by block material instead of direction.

The projected boundary cells also form colored 3D volumes. An analytical,
bounded compositor intersects display rays with those convex volumes and
weights each material by its exact occupied path length. Empty length inside the
vision cube contributes blue sky; a ray that misses the cube remains black.
Overlapping materials produce an order-independent average. It uses projected
screen bounds and convex-plane intersections rather than stepping rays through
the infinite world. Boundary volumes that cross the camera's near or far plane
are clipped in their original 3D source cell before projection, so a nearby
solid face remains visible instead of disappearing when one corner passes the
camera. The result is cached until the camera, display cube, geometry, or window
changes, while crisp material-colored wireframes remain on top. Each of the
eight ±X/±Y/±Z/±W cubic boundary directions has a fixed
brightness: upward faces are brightest, downward faces darkest, and side
directions retain distinct shading without changing the block's material hue.

Before extracting 4D geometry, missing chunks in the bounded render region are
generated concurrently and inserted into the world's normal LRU cache on the
main thread. Generation remains deterministic and exact; this only shortens the
initial Low/High load and never turns rendering into unbounded background work.

Occlusion is resolved along short intervals of each projected feature edge,
using exact 4D grid traversal toward the corresponding source points. Adjacent
visible intervals are merged before drawing. A block can therefore hide part
of an edge without incorrectly erasing the rest, and an edge whose midpoint is
clear cannot show a blocked end through nearer terrain. Sampling scales with
projected length and has a fixed per-edge cap to preserve frame time. The white
selection wireframe uses the same visibility path.

## Build and run

Proj4D requires CMake 3.24 or newer, a C++20 compiler, SDL2, Git, and network
access on the first configure. CMake fetches the pinned public Hypercraft commit
`d830abc` and compiles only its authoritative native terrain/content sources;
browser, multiplayer, lighting, and mob targets are not included.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/proj4d
```

On NixOS, SDL2 can be supplied without modifying the system:

```bash
nix-shell -p SDL2 cmake pkg-config --run \
  "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"
nix-shell -p SDL2 cmake pkg-config --run \
  "cmake --build build --parallel"
./build/proj4d
```

The deterministic graphical smoke paths use SDL's headless driver and save
bitmaps for both menus and every playable combination:

```bash
./build/proj4d --menu-smoke-test --smoke-output dimension-menu.bmp
./build/proj4d --4d-terrain-menu-smoke-test --smoke-output 4d-terrain-menu.bmp
./build/proj4d --3d-terrain-menu-smoke-test --smoke-output 3d-terrain-menu.bmp
./build/proj4d --2d-terrain-menu-smoke-test --smoke-output 2d-terrain-menu.bmp
./build/proj4d --smoke-test --smoke-output 4d-flat.bmp
./build/proj4d --low-smoke-test --smoke-output 4d-low.bmp
./build/proj4d --high-smoke-test --smoke-output 4d-high.bmp
./build/proj4d --3d-smoke-test --smoke-output 3d-flat.bmp
./build/proj4d --3d-low-smoke-test --smoke-output 3d-low.bmp
./build/proj4d --3d-high-smoke-test --smoke-output 3d-high.bmp
./build/proj4d --2d-smoke-test --smoke-output 2d-flat.bmp
./build/proj4d --2d-low-smoke-test --smoke-output 2d-low.bmp
./build/proj4d --2d-high-smoke-test --smoke-output 2d-high.bmp
./build/proj4d --4d-inventory-smoke-test --smoke-output 4d-inventory.bmp
./build/proj4d --3d-inventory-smoke-test --smoke-output 3d-inventory.bmp
./build/proj4d --2d-inventory-smoke-test --smoke-output 2d-inventory.bmp
```

`--normal-smoke-test` remains the compatibility name for the 4D High terrain
smoke path.

## World saving

Flat, Low, and High each have one persistent world. Dimension is not part of
the save identity: selecting Low in 2D, 3D, or 4D automatically opens the
same Low world, for example. There are no save or load buttons. Successful
building, breaking, mining, placing, crafting, and inventory changes are saved,
and the world is checked again when the game exits.

Saves live in the operating system's per-user Proj4D application-data
directory under `worlds/flat.p4world`, `worlds/low.p4world`, and
`worlds/high.p4world`. The versioned files store the terrain identity, seed,
durable 4D material edits, canonical inventory volumes, crafting fields, cursor,
tool durability, and selection rather than generated chunks. Writes use a
temporary file and atomic replacement, while signatures, size checks, sorted
coordinates, and checksums reject damaged or mismatched saves without
overwriting them.

## Inventory, crafting, tools, and clouds

The save owns one canonical 4D inventory. Its 4D interface is a rotatable 3D
cube of slots; 3D exposes the centered 2D plane, and 2D exposes the centered 1D
line. Mining times, stack limits, tool speed, durability, and block/item
definitions come from the pinned Hypercraft content rules. Mining collects the
actual material, placing consumes it, full visible storage prevents a drop from
being destroyed, and a durability bar appears on tools.

Crafting follows the same dimensional rule. 2D recipes occupy a line, 3D uses
ordinary planar recipes, and 4D expands recipes into a 3D crafting field—for
example, the crafting table becomes a `2x2x2` volume and a 4D tool has a
material head plane with a centered handle. Right-clicking a crafting table
opens the larger field.

Clouds are a deterministic infinite 4D field. The 3D clouds are its exact
`w=0` slice and the 2D clouds are the exact `z=0,w=0` slice. They are visual
only and do not add lighting or collision. Proj4D deliberately does not include
mobs, combat, lighting, multiplayer, or browser/WASM support.

## Controls

In the first menu, use the arrow keys and `Enter`, press `4`, `3`, or `2`, or
click a dimension. In the terrain menu, use the arrow keys and `Enter`, press
`F`, `L`, or `H`, or click a choice. `N` is retained as an alias for High.

In every play mode, `T` opens/closes hand inventory and crafting, number keys
select visible quick-storage slots, and `Escape` closes an open inventory before
quitting. Left click mines with the selected tool. Right click places the
selected block, or opens table crafting when aimed at a crafting table. In an
inventory, left click moves/merges stacks and right click splits them (the 4D
inventory reserves right-drag for rotating its slot cube).

### 4D mode

| Input | Action |
|---|---|
| `W` / `S` | Move forward or backward along the current 4D viewing direction |
| `A` / `D` | Move along the first sideways direction, perpendicular to `W` / `S` |
| `Q` / `E` | Move along the second sideways direction, perpendicular to both other movement directions |
| `Space` | Jump 1.5 blocks along `Y` |
| Hold `Shift` | Sneak: crouch, move at 30% speed, and avoid walking off supported edges |
| `Up` / `Down` | Look up or down, clamped at straight up and straight down |
| Mouse left / right | Ordinary horizontal look in the 4D world |
| Mouse up / down | Fourth-dimensional look |
| Hold `Tab` + mouse up/down | Vertical look; horizontal mouse motion is ignored |
| Hold `Ctrl` + mouse | Orbit the solid 3D vision cube without changing the 4D view |
| Mouse wheel | Zoom the external view of the vision cube |
| `T` | Open the rotatable 3D inventory/crafting cube |
| `1`–`9` | Select a visible quick slot |
| Left mouse button | Mine the targeted tesseract |
| Right mouse button | Place beside the target or use a crafting table; rotate the open inventory while dragging |
| `Escape` | Close inventory, then quit |

The red, green, and blue arms at the center form the 3D crosshair. The status
bar shows `X`, `Y`, `Z`, and `W` coordinates and the ordinary horizontal (`H`),
vertical (`V`), and fourth-dimensional (`4D`) view angles.

### 3D mode

| Input | Action |
|---|---|
| `W` / `S` | Move forward or backward |
| `A` / `D` | Strafe left or right |
| Mouse | Look around; vertical look is clamped at straight up/down |
| `Space` | Jump 1.5 blocks along `Y` |
| Hold `Shift` | Sneak: crouch, move at 30% speed, and avoid walking off supported edges |
| `T` | Open the planar inventory/crafting interface |
| `1`–`9` | Select a visible quick slot |
| Left mouse button | Mine the targeted cube |
| Right mouse button | Place beside the target or use a crafting table |
| `Escape` | Close inventory, then quit |

The status bar shows `X`, `Y`, and `Z` plus horizontal (`H`) and vertical (`V`)
view angles. The crosshair and targeted-cube wireframe are white.

### 2D mode

| Input | Action |
|---|---|
| `W` / `S` | Move forward or backward in the current horizontal viewing direction |
| Mouse up / down | Look up or down, clamped at straight up and straight down |
| `Z` | Reverse the horizontal view and forward direction |
| `Space` | Jump 1.5 blocks along `Y` |
| Hold `Shift` | Sneak: crouch, move at 30% speed, and avoid walking off an edge |
| `T` | Open the line inventory/crafting interface |
| `1`–`3` | Select a visible quick slot |
| Left mouse button | Mine the targeted square |
| Right mouse button | Place beside the target or use a crafting table |
| `Escape` | Close inventory, then quit |

Horizontal mouse movement has no effect in 2D because a 1D view has only one
look angle. The white center mark is the crosshair. The status bar shows `X`
and `Y`, vertical angle `V`, and whether the horizontal direction is right or
left.

All three modes use the same Hypercraft terrestrial values: a 7-block-per-second
walk, gravity 36 blocks per second squared, a 1.5-block jump, 0.15-block body
radius, identical vertical body bounds, a 50-millisecond frame clamp,
0.25-block collision substeps, axis-separated wall sliding, and held-`Shift`
sneaking.

## Architecture

- `TerrainGenerator` owns the custom Flat plains and pinned Hypercraft Flat
  (Low) and Normal (High) generators.
- `Chunk` stores `16x16x16x16` tesseracts. All dimension modes use the same
  `BlockWorld`, lazy generation, bounded cache, and durable edit overrides.
- `BlockWorld2D` owns no terrain or chunks; it maps 2D operations directly to
  the authoritative `BlockWorld` plane at `z=0,w=0`.
- `BlockWorld3D` likewise maps 3D operations to the authoritative `w=0` slice.
- `GameSession` owns authoritative material mining/placement and the canonical
  dimensional inventory/crafting state. `world_save` serializes it with the
  three terrain worlds using validation and atomic replacement.
- `Camera4D` performs true 4D-to-3D perspective projection, `Camera3D` performs
  conventional 3D-to-2D perspective projection, and `Camera2D` performs
  true 2D-to-1D projection with bounded vertical pitch and reversible
  horizontal direction.
- Dimension-specific motion and exact grid traversal use shared authoritative
  physics constants while preserving real 4D, 3D, and 2D collision geometry.
- Analytical 4D occupied-path compositing and sightline-culling wireframes,
  standard 3D exposed-face meshing with batched
  depth-ordered triangles, and 2D nearest-hit sampling prevent hidden cavities
  and rear blocks from appearing through solid terrain.
- The SDL layer owns platform input, menus, final display projection, and
  drawing; simulation and projection truth stay in portable C++.
- Tests cover all three projections, shared 4D chunk ownership,
  cross-dimensional edits, save/reload round trips, terrain-save isolation,
  corruption rejection, exact pinned-Hypercraft terrain parity, material
  colors, dimensional inventories and recipes, tools, clouds, negative
  coordinates, infinite depth, bounded caches, occlusion, movement, sneaking,
  jumping, targeting, mining, and placement.

See the repository's
[Proj4D development skill](.codex/skills/proj4d-development-guardrails/SKILL.md)
for the invariants future changes must preserve.

## License

MIT
