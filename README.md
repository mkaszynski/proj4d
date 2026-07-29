# Proj4D

Proj4D is an intentionally small C++ block game set in a genuine
four-spatial-dimensional world. It does not reveal the world as a stack of 3D
slices. Instead, a native 4D perspective camera produces a solid 3D image:

```text
4D tesseract world -> 3D vision cube -> ordinary 2D display
```

At startup, a world menu offers two genuine four-dimensional terrain modes:

- **Flat** is a superflat field where every block at `y <= 0` is solid and
  every block at `y > 0` is air, without limits in `x`, `z`, or `w` and without
  a lower depth limit.
- **Normal** uses the original seeded four-dimensional density and noise
  terrain from before the superflat world was introduced.

The unit of generation and storage is a real `16x16x16x16` chunk containing
65,536 tesseracts. Nearby chunks generate lazily, a bounded least-recently-used
cache prevents infinite memory growth, and player edits survive eviction and
deterministic regeneration.

Every tesseract has eight cubical boundary cells. A cell is visible only when
the neighboring tesseract on that side is air, including across chunk
boundaries. The renderer projects those exposed cells into the vision cube and
suppresses wireframes across smooth faces and two-face ridges. Only true 4D
feature edges, where at least three boundary orientations meet, remain. Because
the superflat surface is perfectly smooth, the renderer also retains one
bounded outer guide around the nearby field instead of restoring a cluttered
internal block grid.

## Build

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
bitmaps of the menu or rendered vision cube:

```bash
./build/proj4d --menu-smoke-test --smoke-output proj4d-menu.bmp
./build/proj4d --smoke-test --smoke-output proj4d-smoke.bmp
./build/proj4d --normal-smoke-test --smoke-output proj4d-normal.bmp
```

## Controls

In the world menu, use the arrow keys and `Enter`, press `F` or `N`, or click a
choice with the mouse.

| Input | Action |
|---|---|
| `W` / `S` | Move forward or backward along the current 4D viewing direction |
| `Space` | Jump 1.5 blocks along the world's vertical axis |
| `A` / `D` | Turn left or right |
| `Up` / `Down` | Look up or down, stopping at straight up or straight down |
| `Q` / `E` | Turn through the fourth spatial dimension |
| Mouse movement | Orbit the solid 3D vision cube |
| Mouse wheel | Zoom the external view of the vision cube |
| Left mouse button | Break the targeted tesseract |
| Right mouse button | Build beside the targeted tesseract |
| `Escape` | Quit |

The red, green, and blue arms at the center form the 3D crosshair. Boundary
wireframes are tinted by which of the four world axes their cubic cell faces.
The top status bar shows live `X`, `Y`, `Z`, and `W` coordinates plus ordinary
horizontal (`H`), vertical (`V`), and fourth-dimensional (`4D`) view angles in
degrees. Ground movement and jumping use Hypercraft's terrestrial player
physics: a 7-block-per-second walk, 36-block-per-second-squared gravity, a
1.5-block jump setting, a 0.3-block-wide 4D collision body, collision substeps,
and axis-separated wall sliding.

## Architecture

- `TerrainGenerator` owns both the infinite superflat and original deterministic
  4D density modes selected by the startup menu.
- `Chunk` stores exactly `16x16x16x16` blocks.
- `BlockWorld` owns the selected terrain mode, lazy generation, durable edit
  overrides, and a bounded generated-chunk cache for the unbounded coordinate
  space.
- `Camera4D` owns an orthonormal four-axis camera frame and performs true
  4D-to-3D perspective projection with level yaw and bounded vertical pitch.
- Player motion uses Hypercraft's authoritative terrestrial constants,
  collision bounds, 50-millisecond frame clamp, substeps, grounding, and
  axis-separated collision response, extended across `x`, `z`, and `w`.
- View-dependent sightline culling prevents nearer solid terrain from exposing
  wireframes belonging to hidden player-made cavities.
- `buildVisionGeometry` examines a fixed local 4D region, rejects cubic faces
  blocked by solid neighbors, and clips projected feature edges to the cube.
- The SDL layer owns input, the final 3D-to-2D display projection, and drawing.
- Tests exercise 4D camera math, negative chunk coordinates, the infinite flat
  boundary, preserved density determinism, infinite depth, cache eviction, edit
  survival, occluded-face culling, ray targeting, and projected-volume clipping.

See the repository's
[Proj4D development skill](.codex/skills/proj4d-development-guardrails/SKILL.md)
for the invariants future changes must preserve.

## License

MIT
