# Proj4D

Proj4D is an intentionally small C++ block game set in a genuine
four-spatial-dimensional world. It does not reveal the world as a stack of 3D
slices. Instead, a native 4D perspective camera produces a solid 3D image:

```text
4D tesseract world -> 3D vision cube -> ordinary 2D display
```

The world is unbounded in all four axes and is generated deterministically from
a four-dimensional density field. It becomes solid without limit at deep
negative `y`, while the surface can contain overhangs and multiple solid/air
transitions rather than one height per horizontal position.

The unit of generation and storage is a real `16x16x16x16` chunk containing
65,536 tesseracts. Nearby chunks generate lazily, a bounded least-recently-used
cache prevents infinite memory growth, and player edits survive eviction and
deterministic regeneration.

Every tesseract has eight cubical boundary cells. A cell is visible only when
the neighboring tesseract on that side is air, including across chunk
boundaries. The renderer projects those exposed cells into the vision cube and
suppresses wireframes across smooth faces and two-face ridges. Only true 4D
feature edges, where at least three boundary orientations meet, remain.

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

The deterministic graphical smoke path uses SDL's headless driver and saves a
bitmap of the rendered vision cube:

```bash
./build/proj4d --smoke-test --smoke-output proj4d-smoke.bmp
```

## Controls

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
degrees.

## Architecture

- `TerrainGenerator` owns the deterministic 4D density field.
- `Chunk` stores exactly `16x16x16x16` blocks.
- `BlockWorld` owns lazy generation, durable edit overrides, and a bounded
  generated-chunk cache for the unbounded coordinate space.
- `Camera4D` owns an orthonormal four-axis camera frame and performs true
  4D-to-3D perspective projection with level yaw and bounded vertical pitch.
- View-dependent sightline culling prevents nearer solid terrain from exposing
  wireframes belonging to hidden caves.
- `buildVisionGeometry` examines a fixed local 4D region, rejects cubic faces
  blocked by solid neighbors, and clips projected feature edges to the cube.
- The SDL layer owns input, the final 3D-to-2D display projection, and drawing.
- Tests exercise 4D camera math, negative chunk coordinates, procedural
  determinism, infinite depth, cache eviction, edit survival, occluded-face
  culling, ray targeting, and projected-volume clipping.

See the repository's
[Proj4D development skill](.codex/skills/proj4d-development-guardrails/SKILL.md)
for the invariants future changes must preserve.

## License

MIT
