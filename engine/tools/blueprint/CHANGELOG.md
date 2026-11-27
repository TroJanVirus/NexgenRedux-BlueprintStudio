# Blueprint Studio v3 — 27 November 2026

Upstream source: `54bc84ce91be85d4c7e29b26f4d748547477266e`. The graphics/video update is retained unchanged. This release includes all previous v1/v2 Blueprint work.

## New

- Typed Blueprint interface assets, implementation function scaffolding, reusable contracts, and safe entity-to-behaviour message nodes. The runtime adds `Entity.behaviourAt(uint)` for indexed lookup. Interface-message graphs require a rebuilt v3 engine binary.
- Multi-track timelines: float, Vec2, Vec3, Color, independently editable channel curves, forward/reverse/loop/ping-pong playback.
- Generic, sprite and model animation state machines: visual transition diagram, ordered conditions, per-state execution, reset, entered and elapsed outputs; one-shot sprite clips no longer restart every update.
- Reusable 2D/3D entity hierarchy templates: parent tree, local transforms, primitive/image/model visuals, attached imported behaviours, root and per-entity output pins, import/export and schematic editor.
- Six additional editable examples, expanded native and browser integration tests; nine-screen example project.

## Verification

- 53 JavaScript compiler/project checks.
- 15 focused native tests, 136 checks, 0 failures (720 API signatures compiled in 15 suites).
- All 14 standalone example graphs compiled with native AngelScript.
- CTest: Blueprint runtime, real TCP debugger, self-contained repository checks.
- Chromium: editing interface contracts, track curves, state transitions and entity hierarchies, plus previous project and debugger workflows.

## Limits

No full graphical rendering test or RXDK/Windows/macOS build was performed; external media and docs submodules are empty in the supplied archive. Model animation code was native-compiled but not tested with loaded skeletal assets. Hierarchy preview is schematic, not a 3D scene viewport. No automatic replication/RPCs, cross-instance multicast delegates, pose-blend editor, timeline seek/stop, or multi-object debug selection yet. No XBE is supplied. See README.md for integration and feature details.
