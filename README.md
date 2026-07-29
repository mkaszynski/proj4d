# Proj4D

Proj4D is an intentionally small C++ block game set in a genuine
four-spatial-dimensional world. It does not reveal the world as a stack of 3D
slices. Instead, a native 4D perspective camera produces a solid 3D image:

```text
4D tesseract world -> 3D vision cube -> ordinary 2D display
```

The finite world begins as a flat field of tesseract blocks. Every tesseract has
eight cubical boundary cells. The renderer projects the wireframe of each
exposed cell into the vision cube, omits shared cubic boundaries, and suppresses
wireframes across smooth faces and two-face ridges. Only true 4D feature edges,
where at least three boundary orientations meet, remain. The resulting image
stays readable while retaining real 4D coordinates, movement, rotations,
targeting, building, and breaking.

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
| `Space` | Jump along the world's vertical axis |
| `A` / `D` | Turn left or right |
| `Up` / `Down` | Look up or down |
| `Q` / `E` | Turn through the fourth spatial dimension |
| Mouse movement | Orbit the solid 3D vision cube |
| Mouse wheel | Zoom the external view of the vision cube |
| Left mouse button | Break the targeted tesseract |
| Right mouse button | Build beside the targeted tesseract |
| `Escape` | Quit |

The red, green, and blue arms at the center form the 3D crosshair. Boundary
wireframes are tinted by which of the four world axes their cubic cell faces.

## Architecture

- `BlockWorld` owns the bounded 4D block grid.
- `Camera4D` owns an orthonormal four-axis camera frame and performs true
  4D-to-3D perspective projection.
- `buildVisionGeometry` generates only exposed cubic boundary cells and clips
  their projected edges to the vision cube.
- The SDL layer owns input, the final 3D-to-2D display projection, and drawing.
- Tests exercise 4D camera math, finite bounds, boundary culling, ray targeting,
  and projected-volume clipping.

See the repository's
[Proj4D development skill](.codex/skills/proj4d-development-guardrails/SKILL.md)
for the invariants future changes must preserve.

## License

MIT
