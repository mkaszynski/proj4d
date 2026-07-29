---
name: proj4d-development-guardrails
description: Preserve Proj4D's true four-spatial-dimensional C++ game architecture, projected 3D vision, bounded performance, tests, and public-repository privacy. Use whenever creating, changing, debugging, reviewing, testing, or releasing the Proj4D repository.
---

# Proj4D Development Guardrails

Apply these invariants to every change in the Proj4D repository.

## Preserve the Game

- Keep all world coordinates, blocks, camera state, targeting, collision, and
  interactions genuinely four-dimensional.
- Represent each block as a tesseract with eight cubic boundary cells.
- Render a native 4D perspective view into a solid 3D vision volume. Do not
  replace the view with 3D slices, independent 3D worlds, or fake depth.
- Keep the two-stage display boundary explicit: project the 4D world into the
  3D vision cube, then project that cube onto the user's 2D display.
- Cull the shared cubic boundary cell between adjacent tesseracts. Preserve
  exposed cells even when geometry overlaps after projection.
- Keep the finite flat-field world as the default unless explicitly changed.
- Keep the three independent look rotations: vertical, ordinary horizontal,
  and fourth-dimensional horizontal.

## Keep Ownership Clear

- Keep game and projection truth in portable C++.
- Let the SDL layer own only platform input, windowing, and final drawing.
- Route building and breaking through bounded-world APIs and exact 4D ray
  targeting.
- Avoid duplicated simulation or projection rules in presentation code.

## Protect Performance and Correctness

- Bound all per-frame world traversal by the finite world size.
- Add tests for every behavior change, especially 4D basis orthogonality,
  projection clipping, boundary culling, bounds, and ray interaction.
- Run the complete build, CTest suite, and headless graphical smoke test before
  handoff.
- Treat broken native frame rate or unbounded geometry growth as regressions.

## Keep the Repository Public-Safe

- Do not commit names, private email addresses, local absolute paths, tokens,
  credentials, machine identifiers, private URLs, or user data.
- Use project-relative documentation and temporary paths for generated output.
- Keep build products, screenshots, logs, and editor state out of Git.
- Develop on short-lived branches, open a pull request, and require CI to pass.
