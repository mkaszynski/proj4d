---
name: proj4d-development-guardrails
description: Preserve Proj4D's infinite true four-spatial-dimensional C++ world, 16x16x16x16 chunks, infinite superflat terrain, retained 4D density functions, projected 3D vision, bounded streaming performance, tests, and public-repository privacy. Use whenever creating, changing, debugging, reviewing, testing, or releasing the Proj4D repository.
---

# Proj4D Development Guardrails

Apply these invariants to every change in the Proj4D repository.

## Preserve the Game

- Keep all world coordinates, blocks, camera state, targeting, collision, and
  interactions genuinely four-dimensional.
- Keep the world unbounded and procedurally generated in all four axes.
- Keep the real world unit as a `16x16x16x16` chunk with explicit 4D chunk and
  local coordinates, including correct floor division for negative positions.
- Generate the active world as a four-dimensional superflat field: every block
  at `y <= 0` is solid, every block at `y > 0` is air, and the field is
  unbounded across `x`, `z`, and `w` and infinitely deep.
- Preserve the original deterministic seeded four-dimensional density and noise
  terrain as the explicitly selectable `TerrainMode::Density`, but keep the
  actual game on `TerrainMode::Flat`.
- Represent each block as a tesseract with eight cubic boundary cells.
- Render a native 4D perspective view into a solid 3D vision volume. Do not
  replace the view with 3D slices, independent 3D worlds, or fake depth.
- Keep the two-stage display boundary explicit: project the 4D world into the
  3D vision cube, then project that cube onto the user's 2D display.
- Cull every cubic face whose neighboring tesseract is solid, including across
  chunk boundaries. Preserve exposed faces even when geometry overlaps after
  projection.
- Keep the perfectly smooth superflat surface legible with one bounded outer
  wireframe guide around the local render region. Do not restore internal grid
  lines between smoothly joined surface cells.
- Never treat a missing or not-yet-cached neighbor chunk as air. Generate its
  superflat terrain under the bounded loading policy before deciding that a face
  is exposed.
- Keep the three look angles independent: `A`/`D` change only ordinary
  horizontal `H`, `Up`/`Down` change only vertical `V`, and `Q`/`E` change only
  fourth-dimensional `4D`.

## Keep Ownership Clear

- Keep game and projection truth in portable C++.
- Let the SDL layer own only platform input, windowing, and final drawing.
- Route building and breaking through chunk-backed world APIs and exact 4D grid
  ray traversal. Preserve edits across chunk eviction and regeneration.
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
- Add tests for every behavior change, especially 4D basis orthogonality,
  negative chunk math, the flat `y=0` boundary across distant `x/z/w`
  coordinates, preserved density determinism, deep terrain, chunk boundaries,
  occluded-face culling, cache limits, edit survival, projection clipping, and
  ray interaction.
- Run the complete build, CTest suite, and headless graphical smoke test before
  handoff.
- Treat broken native frame rate or unbounded geometry growth as regressions.

## Keep the Repository Public-Safe

- Do not commit names, private email addresses, local absolute paths, tokens,
  credentials, machine identifiers, private URLs, or user data.
- Use project-relative documentation and temporary paths for generated output.
- Keep build products, screenshots, logs, and editor state out of Git.
- Develop on short-lived branches, open a pull request, and require CI to pass.
