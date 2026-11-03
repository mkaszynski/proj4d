---
name: proj4d-development-guardrails
description: Preserve Proj4D's infinite true 4D world with shared persistent 3D and 2D slice play, real 16x16x16x16 chunks, Flat/Low/High saves, dimension-native projections, bounded performance, tests, and public-repository privacy. Use whenever creating, changing, debugging, reviewing, testing, or releasing the Proj4D repository.
---

# Proj4D Development Guardrails

Apply these invariants to every change in the Proj4D repository.

## Preserve the Game

- Keep 4D, 3D, and 2D play dimensionally genuine across coordinates, blocks,
  cameras, targeting, collision, and interactions. Implement 3D and 2D as thin
  gameplay slices over shared 4D storage, never as fixed cameras over 4D play.
- Keep all terrain worlds unbounded horizontally and procedurally generated.
  Keep terrain infinitely deep along negative `y`.
- Keep the sole generation and storage unit as a real `16x16x16x16` chunk,
  with explicit 4D chunk and local coordinates and correct floor division for
  negative positions. Never restore separately generated or stored 2D chunks.
- Present a dimension menu with `4D`, `3D`, and `2D` before world creation,
  followed by a terrain menu with `Flat`, `Low`, and `High`. Support keyboard
  and mouse selection in both menus.
- Generate `Flat` as a perfectly level four-dimensional Hypercraft plains field
  with grass, dirt, stone, deterministic ores, and no trees. Keep its
  subsurface free of caves, its surface at `y=0`, and its extent unbounded
  across `x`, `z`, and `w` and infinitely deep.
- Generate `Low` from the complete pinned Hypercraft Flat generator. Preserve
  its exact material-valued chunks, biome layers, caves, trees, ores, fluids,
  and decorators rather than imitating its height guide.
- Generate `High` from the complete pinned Hypercraft Normal generator through
  the compatibility enum value `TerrainMode::Density`. Retain
  `--normal-smoke-test` only as a compatibility command name. Pin the upstream
  commit so saved procedural worlds and parity tests remain reproducible.
- Implement each 2D terrain as a thin view of the authoritative 4D
  `BlockWorld`: `(x,y)` must read and edit `(x,y,0,0)` through the same 4D
  chunks and cache. Never copy or regenerate that plane into separate storage.
- Implement each 3D terrain as the authoritative `w=0` slice: `(x,y,z)` must
  read and edit `(x,y,z,0)` through those same chunks and cache. Never add
  independently generated or stored 3D chunks.
- Represent each block as a tesseract with eight cubic boundary cells.
- Render a native 4D perspective view into a solid 3D vision volume. Do not
  replace the view with 3D slices, independent 3D worlds, or fake depth.
- Keep the two-stage display boundary explicit: project the 4D world into the
  3D vision cube, then project that cube onto the user's 2D display.
- Preserve the filled 4D projection as well as its crisp wireframe. Intersect
  display rays analytically with bounded projected convex boundary volumes,
  weight material colors by exact occupied path length inside the vision cube,
  weight empty in-cube length as blue sky, keep rays outside the cube black, and
  keep the result order independent. Never replace this with fixed-step
  traversal through the infinite world.
- Cache the filled projection until its camera, display-cube transform, visible
  topology, world revision, or target size changes. Keep its internal image
  resolution, projected-volume count, and per-volume pixel bounds explicit.
- Clip 4D cubic boundary volumes against the camera near and far planes in
  source space before projection. Never discard a whole nearby solid merely
  because one of its vertices crosses a depth plane.
- Cull every cubic face whose neighboring tesseract is solid, including across
  chunk boundaries. Preserve exposed faces even when geometry overlaps after
  projection.
- Resolve occlusion along projected feature-edge intervals. Never make an
  entire edge visible or hidden from one midpoint ray; retain visible
  fragments, hide blocked fragments, and keep the per-edge work explicitly
  bounded. Apply the same depth-tested visibility to selection wireframes.
- Do not restore internal grid lines between smoothly joined surface cells.
  Keep the visible feature-edge set bounded for Flat, Low, and High.
- Color projected filled shapes and wireframes by block material. Keep grass,
  dirt, stone, wood, leaves, ores, crafted blocks, and fluids distinguishable;
  do not restore axis-colored world edges.
- Shade projected 4D boundary volumes by their stable ±X/±Y/±Z/±W facing while
  preserving material hue. Keep the upward boundary brightest, downward
  boundary darkest, and all eight directions visibly distinct; do not use
  frame-time randomness or add a dynamic lighting system.
- Never treat a missing or not-yet-cached neighbor chunk as air. Generate it
  from the selected terrain mode under the bounded loading policy before
  deciding that a face is exposed.
- Keep the three independent look rotations: vertical, ordinary horizontal,
  and fourth-dimensional horizontal.
- Map unmodified horizontal mouse motion to ordinary horizontal world look and
  vertical mouse motion to fourth-dimensional world look. While `Tab` is held,
  use vertical mouse motion exclusively for vertical world look and ignore
  horizontal mouse motion. While either Ctrl key is held, preserve mouse motion
  as the external orbit control for the projected 3D vision cube instead of
  changing world look; Ctrl takes priority over Tab.
- Use `W/S` for forward/backward movement and `A/D` plus `Q/E` for two mutually
  perpendicular level strafing directions. Keep all three directions mutually
  orthogonal and remove keyboard look rotation from `A/D` and `Q/E`.
- Use either held `Shift` key for Minecraft-style sneaking: lower the real
  player pose, reduce movement speed to 30 percent, and prevent a grounded
  player from walking off supported edges along `x`, `z`, or `w`.
- Keep terrestrial player motion synchronized with Hypercraft's authoritative
  baseline: 7-block-per-second walking, gravity 36, a 1.5-block jump setting,
  0.15-block body radius on `x`, `z`, and `w`, 1.65 eye-to-feet and 0.18
  eye-to-head bounds, a 50-millisecond delta cap, 0.25-block collision
  substeps, and axis-separated 4D wall sliding.

## Preserve Conventional 3D Vision and Play

- Render 3D mode through an ordinary perspective camera into a 2D display on
  a blue-sky background. Use bounded exposed-face meshing, near-plane clipping,
  and batched triangle rendering; do not use per-screen-pixel world ray
  tracing or reveal occluded terrain.
- Color cube faces by block material. Give each cube a stable
  coordinate-derived lighter or darker shade without changing material
  identity. Draw a white wireframe around the selected cube.
- Use conventional mouse yaw and pitch, clamp pitch at straight up/down, use
  `W/S` forward/back, `A/D` strafe, `Space` jump, and held `Shift` sneak.
  Preserve the exact shared physics constants, axis-separated wall sliding,
  ledge protection, targeting, building, and breaking behavior.

## Preserve True 2D Vision and Play

- Render 2D mode through a native 2D perspective camera into a 1D first-person
  view. Never replace it with a side-on platformer, overhead camera, or 4D
  slice.
- Keep empty sky blue in 4D, 3D, and 2D while preserving material colors,
  cloud contrast, status bars, and selection marks.
- Display the 1D view as a vertical strip spanning the screen height, centered
  horizontally, with width equal to one tenth of its height. Projected square
  sides must form readable rectangular intervals within this strip.
- Trace each displayed 1D sample through the 2D block grid and show only its
  nearest solid boundary. Never reveal rear blocks or underground cavities
  through nearer terrain.
- Color each visible boundary interval by block material. Keep the center
  targeting mark visible. Give every block a stable coordinate-derived,
  visibly lighter or darker variation of its material color; never use
  frame-time randomness that makes block colors shimmer.
- Map vertical mouse motion to vertical 2D look, ignore horizontal mouse
  motion, and clamp look at straight up and straight down. Use `Z` to reverse
  the horizontal view and forward direction.
- Use `W/S` for forward/backward movement, `Space` for jumping, and held
  `Shift` for sneaking. Apply the exact same speed, gravity, jump height,
  vertical body bounds, radius, delta clamp, collision substeps, wall sliding,
  and sneak behavior as 4D mode, interpreted in `x/y`.
- Route 2D building and breaking through exact 2D grid traversal and the
  shared chunk-backed 4D world at `z=0,w=0`. Protect occupied player cells
  from placement.

## Preserve Shared World Saves

- Keep exactly three persistent world identities: Flat, Low, and High. All
  dimension modes must automatically continue the selected terrain's same
  save; never add separate 3D or 2D saves or require save/load menu buttons.
- Persist the authoritative seed, terrain identity, material-valued 4D edit
  overrides, canonical inventory/crafting fields, cursor, selected slot, stack
  counts, and tool durability. Include edits and inventory slots outside either
  slice. Slice edits must reload in 4D; 4D edits at `w=0` must reload in 3D,
  and edits at `z=0,w=0` in 2D.
- Keep the save format versioned and bounded. Validate its signature, terrain,
  seed, size, coordinate ordering, block values, and checksum before mutating
  the in-memory world. Refuse damaged or mismatched saves without overwriting
  them.
- Save successful block edits promptly and again on clean exit. Install saves
  through a completed temporary file and atomic replacement, retaining the
  previous file if replacement fails.
- Keep graphical smoke modes isolated from real user saves so CI and
  screenshots remain deterministic.

## Preserve Dimension-Native Survival Systems

- Keep one canonical three-axis inventory volume per terrain save. Expose its
  complete volume in 4D, centered plane in 3D, and centered line in 2D. Drops
  collected in a lower-dimensional mode must enter slots visible in that mode;
  hidden higher-dimensional slots remain intact and accessible in 4D.
- Keep crafting dimension-native: a line in 2D, conventional plane in 3D, and
  volume in 4D. Lower-dimensional patterns must match their displayed centered
  slice, accept translated patterns where they fit, consume ingredients
  transactionally, and never consume items in hidden slices.
- Preserve Hypercraft content identity, stack sizes, mining multipliers, tool
  speed, and durability for supported blocks and items. Prevent mining when the
  visible inventory cannot accept the drop, and consume placed materials.
- Keep inventory, crafting, tools, items/blocks, and clouds. Do not expand scope
  into lighting, mobs, combat, multiplayer, browser/WASM, or other unspecified
  Hypercraft systems without an explicit request.
- Generate clouds as one deterministic infinite 4D visual field. 3D must show
  its `w=0` slice and 2D its `z=0,w=0` slice. Keep cloud work bounded and clouds
  non-colliding and independent of terrain storage.

## Keep Ownership Clear

- Keep game and projection truth in portable C++.
- Let the SDL layer own only platform input, windowing, and final drawing.
- Route mining and placement through `GameSession` and dimension-correct
  chunk-backed world APIs and exact grid traversal. Preserve material edits
  across chunk eviction and regeneration.
- Avoid duplicated simulation or projection rules in presentation code.

## Protect Performance and Correctness

- Keep generation and rendering bounded per frame. Never scan the infinite
  world, generate an unbounded chunk set, or let the loaded-chunk cache grow
  without a fixed limit.
- Preserve explicit numeric guards for topology-region volume, loaded-chunk
  count, and measured smoke/startup time. Justify any increase with before/after
  evidence.
- Cache topology extraction independently from camera projection so ordinary
  look rotation does not regenerate chunks or rescan blocks.
- Add tests for every behavior change, especially shared 4D chunk ownership,
  cross-dimensional save round trips, corrupt-save rejection, terrain-save
  isolation, 4D basis orthogonality, 3D and 2D camera behavior, menu routing,
  Flat plains/no-cave boundaries, exact full-chunk Low parity with Hypercraft
  Flat, exact full-chunk High parity with Hypercraft Normal, material identity,
  dimensional inventory/crafting slices, mining/tool timing, cloud slices,
  occupied-path color averaging, exact 2D terrain cross-sections, deep terrain,
  occlusion, cache limits, edit survival, all projections, shared physics, and
  interaction.
- Run the complete build, CTest suite, and headless graphical smoke tests for
  the dimension menu, all three terrain menus, and Flat, Low, and High in 4D,
  3D, and 2D, plus all three inventory interfaces, before handoff.
- Treat broken native frame rate or unbounded geometry growth as regressions.

## Keep the Repository Public-Safe

- Do not commit names, private email addresses, local absolute paths, tokens,
  credentials, machine identifiers, private URLs, or user data.
- Use project-relative documentation and temporary paths for generated output.
- Keep build products, screenshots, logs, and editor state out of Git.
- Keep user save files and temporary/backup save artifacts out of Git.
- Develop on short-lived branches, open a pull request, and require CI to pass.
