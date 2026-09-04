---
name: proj4d-development-guardrails
description: Preserve Proj4D's infinite true 4D world with shared persistent 2D slice play, real 16x16x16x16 chunks, Flat/Low/High saves, native 4D-to-3D and 2D-to-1D vision, bounded streaming performance, tests, and public-repository privacy. Use whenever creating, changing, debugging, reviewing, testing, or releasing the Proj4D repository.
---

# Proj4D Development Guardrails

Apply these invariants to every change in the Proj4D repository.

## Preserve the Game

- Keep 4D mode genuinely four-dimensional and 2D mode genuinely
  two-dimensional across coordinates, blocks, cameras, targeting, collision,
  and interactions. Never implement 2D as a fixed camera over the 4D world.
- Keep both worlds unbounded horizontally and procedurally generated. Keep
  terrain infinitely deep along negative `y`.
- Keep the sole generation and storage unit as a real `16x16x16x16` chunk,
  with explicit 4D chunk and local coordinates and correct floor division for
  negative positions. Never restore separately generated or stored 2D chunks.
- Present a dimension menu with `4D` and `2D` before world creation, followed
  by a terrain menu with `Flat`, `Low`, and `High`. Support keyboard and mouse
  selection in both menus.
- Generate `Flat` as a four-dimensional superflat field: every block at
  `y <= 0` is solid, every block at `y > 0` is air, and the field is unbounded
  across `x`, `z`, and `w` and infinitely deep.
- Generate `Low` from Hypercraft Flat's exact seeded height-guide noise, 4D
  density noise, and 1000x vertical falloff. Preserve its varying raw density
  surface around the `y = 18` base level, but exclude caves, trees, ores,
  fluids, biome layers, and other decorations. Keep it unbounded across `x`,
  `z`, and `w` and infinitely deep.
- Generate `High` with the original deterministic seeded four-dimensional
  density and noise terrain through `TerrainMode::Density`. Retain
  `--normal-smoke-test` only as a compatibility command name.
- Implement each 2D terrain as a thin view of the authoritative 4D
  `BlockWorld`: `(x,y)` must read and edit `(x,y,0,0)` through the same 4D
  chunks and cache. Never copy or regenerate that plane into separate storage.
- Represent each block as a tesseract with eight cubic boundary cells.
- Render a native 4D perspective view into a solid 3D vision volume. Do not
  replace the view with 3D slices, independent 3D worlds, or fake depth.
- Keep the two-stage display boundary explicit: project the 4D world into the
  3D vision cube, then project that cube onto the user's 2D display.
- Cull every cubic face whose neighboring tesseract is solid, including across
  chunk boundaries. Preserve exposed faces even when geometry overlaps after
  projection.
- Keep the perfectly smooth `y=0` Flat surface legible with one bounded outer
  wireframe guide around the local render region. Never add that guide to Low
  or High; render their generated terrain feature edges. Do not restore
  internal grid lines between smoothly joined surface cells.
- Color each world edge by the 4D axis it actually follows: red `X`, green `Y`,
  blue `Z`, and purple `W`. Do not choose a color from an incident face or
  cycle colors decoratively.
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

## Preserve True 2D Vision and Play

- Render 2D mode through a native 2D perspective camera into a 1D first-person
  view. Never replace it with a side-on platformer, overhead camera, or 4D
  slice.
- Display the 1D view as a vertical strip spanning the screen height, centered
  horizontally, with width equal to one tenth of its height. Projected square
  sides must form readable rectangular intervals within this strip.
- Trace each displayed 1D sample through the 2D block grid and show only its
  nearest solid boundary. Never reveal rear blocks or underground cavities
  through nearer terrain.
- Color each visible boundary interval by the world direction along which it
  runs, using the reversed 2D palette: green `X` and red `Y`. Keep the center
  targeting mark visible. Give every block a stable coordinate-derived,
  visibly lighter or darker variation of its axis color; never use frame-time
  randomness that makes block colors shimmer.
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

- Keep exactly three persistent world identities: Flat, Low, and High. Both
  dimension modes must automatically continue the selected terrain's same
  save; never add separate 2D saves or require save/load menu buttons.
- Persist the authoritative seed, terrain identity, and all 4D edit overrides,
  including edits outside the 2D slice. A 2D edit must reload in 4D and a 4D
  edit at `z=0,w=0` must reload in 2D.
- Keep the save format versioned and bounded. Validate its signature, terrain,
  seed, size, coordinate ordering, block values, and checksum before mutating
  the in-memory world. Refuse damaged or mismatched saves without overwriting
  them.
- Save successful block edits promptly and again on clean exit. Install saves
  through a completed temporary file and atomic replacement, retaining the
  previous file if replacement fails.
- Keep graphical smoke modes isolated from real user saves so CI and
  screenshots remain deterministic.

## Keep Ownership Clear

- Keep game and projection truth in portable C++.
- Let the SDL layer own only platform input, windowing, and final drawing.
- Route building and breaking through dimension-correct chunk-backed world
  APIs and exact grid traversal. Preserve edits across chunk eviction and
  regeneration.
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
  isolation, 4D basis orthogonality, 2D pitch and direction, menu routing,
  Flat boundaries, Low golden parity with Hypercraft Flat, High density
  determinism, exact 2D terrain cross-sections, deep terrain, occlusion, cache
  limits, edit survival, both projections, shared physics, and ray interaction.
- Run the complete build, CTest suite, and headless graphical smoke tests for
  the dimension menu, both terrain menus, and Flat, Low, and High in both 4D
  and 2D before handoff.
- Treat broken native frame rate or unbounded geometry growth as regressions.

## Keep the Repository Public-Safe

- Do not commit names, private email addresses, local absolute paths, tokens,
  credentials, machine identifiers, private URLs, or user data.
- Use project-relative documentation and temporary paths for generated output.
- Keep build products, screenshots, logs, and editor state out of Git.
- Keep user save files and temporary/backup save artifacts out of Git.
- Develop on short-lived branches, open a pull request, and require CI to pass.
