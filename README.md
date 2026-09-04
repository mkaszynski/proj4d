# Proj4D

Proj4D is an intentionally small C++ block game with genuine 4D and 2D
worlds. The first startup menu selects the number of spatial dimensions; the
second selects Flat, Low, or High terrain for that dimension.

The 4D mode does not reveal its world as a stack of 3D slices. A native 4D
perspective camera produces a solid 3D image:

```text
4D tesseract world -> 3D vision cube -> ordinary 2D display
```

The 2D mode follows the same idea one dimension lower. It is not a side-on
platformer or an overhead view:

```text
2D square world -> 1D first-person vision line -> vertical display strip
```

The vision line spans the screen vertically and is drawn as a centered strip
whose width is one tenth of its height. This makes projected block edges
readable as rectangular intervals without widening the line to half of the
display. Only the nearest solid edge along each sight ray is shown, so terrain
cannot be seen through other blocks. Red intervals run along `X` and green
intervals run along `Y`.

Both dimensions offer the same terrain choices:

- **Flat** is a superflat field where every block at `y <= 0` is solid and
  every block at `y > 0` is air. It is unbounded horizontally and has no lower
  depth limit.
- **Low** reproduces the undecorated terrain shape of Hypercraft's Flat mode,
  centered around `y = 18`. It excludes caves, trees, ores, fluids, biome
  layers, and other decorations, and remains infinitely deep.
- **High** is the original deterministic density-and-noise terrain retained
  from before superflat was introduced. It is represented internally by
  `TerrainMode::Density`.

The 4D unit of generation and storage is a real `16x16x16x16` chunk containing
65,536 tesseracts. The 2D unit is a real `16x16` chunk containing 256 squares.
In both modes, nearby chunks generate lazily, a bounded least-recently-used
cache prevents infinite memory growth, and player edits survive eviction and
deterministic regeneration. Each 2D terrain is the exact `z=0, w=0`
cross-section of its corresponding 4D generator.

In 4D, every tesseract has eight cubical boundary cells. A cell is visible only
when the neighboring tesseract on that side is air, including across chunk
boundaries. The renderer projects exposed cells into the vision cube and
suppresses wireframes across smooth faces and two-face ridges. Only true 4D
feature edges remain. The perfectly smooth Flat surface retains one bounded
outer guide around the nearby field rather than a cluttered internal grid.
High and Low render only generated terrain features. Edge colors identify the
world direction: red `X`, green `Y`, blue `Z`, and purple `W`.

## Build and run

Proj4D requires CMake 3.24 or newer, a C++20 compiler, and SDL2.

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
./build/proj4d --2d-terrain-menu-smoke-test --smoke-output 2d-terrain-menu.bmp
./build/proj4d --smoke-test --smoke-output 4d-flat.bmp
./build/proj4d --low-smoke-test --smoke-output 4d-low.bmp
./build/proj4d --high-smoke-test --smoke-output 4d-high.bmp
./build/proj4d --2d-smoke-test --smoke-output 2d-flat.bmp
./build/proj4d --2d-low-smoke-test --smoke-output 2d-low.bmp
./build/proj4d --2d-high-smoke-test --smoke-output 2d-high.bmp
```

`--normal-smoke-test` remains the compatibility name for the 4D High terrain
smoke path.

## Controls

In the first menu, use the arrow keys and `Enter`, press `4` or `2`, or click a
dimension. In the terrain menu, use the arrow keys and `Enter`, press `F`, `L`,
or `H`, or click a choice. `N` is retained as an alias for High.

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
| Left mouse button | Break the targeted tesseract |
| Right mouse button | Build beside the targeted tesseract |
| `Escape` | Quit |

The red, green, and blue arms at the center form the 3D crosshair. The status
bar shows `X`, `Y`, `Z`, and `W` coordinates and the ordinary horizontal (`H`),
vertical (`V`), and fourth-dimensional (`4D`) view angles.

### 2D mode

| Input | Action |
|---|---|
| `W` / `S` | Move forward or backward in the current horizontal viewing direction |
| Mouse up / down | Look up or down, clamped at straight up and straight down |
| `Z` | Reverse the horizontal view and forward direction |
| `Space` | Jump 1.5 blocks along `Y` |
| Hold `Shift` | Sneak: crouch, move at 30% speed, and avoid walking off an edge |
| Left mouse button | Break the targeted square |
| Right mouse button | Build beside the targeted square |
| `Escape` | Quit |

Horizontal mouse movement has no effect in 2D because a 1D view has only one
look angle. The white center mark is the crosshair. The status bar shows `X`
and `Y`, vertical angle `V`, and whether the horizontal direction is right or
left.

Both modes use the same Hypercraft terrestrial values: a 7-block-per-second
walk, gravity 36 blocks per second squared, a 1.5-block jump, 0.15-block body
radius, identical vertical body bounds, a 50-millisecond frame clamp,
0.25-block collision substeps, axis-separated wall sliding, and held-`Shift`
sneaking.

## Architecture

- `TerrainGenerator` owns Flat, Hypercraft-compatible Low, and original High
  density terrain. `TerrainGenerator2D` exposes exact 2D cross-sections.
- `Chunk` stores `16x16x16x16` tesseracts; `Chunk2D` stores `16x16` squares.
- `BlockWorld` and `BlockWorld2D` own lazy generation, bounded caches, and
  durable edit overrides in their respective unbounded spaces.
- `Camera4D` performs true 4D-to-3D perspective projection. `Camera2D` performs
  true 2D-to-1D projection with bounded vertical pitch and reversible
  horizontal direction.
- Dimension-specific motion and exact grid traversal use shared authoritative
  physics constants while preserving real 4D and 2D collision geometry.
- 4D sightline culling and 2D nearest-hit ray sampling prevent hidden cavities
  and rear blocks from appearing through solid terrain.
- The SDL layer owns platform input, menus, final display projection, and
  drawing; simulation and projection truth stay in portable C++.
- Tests cover both projections, both chunk layouts, terrain parity, negative
  coordinates, infinite depth, bounded caches, edit survival, occlusion,
  movement, sneaking, jumping, targeting, building, and breaking.

See the repository's
[Proj4D development skill](.codex/skills/proj4d-development-guardrails/SKILL.md)
for the invariants future changes must preserve.

## License

MIT
