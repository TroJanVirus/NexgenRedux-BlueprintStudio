# Changes

## Everyday editing

- Rename variables, local variables, functions and macros without breaking their existing references.
- Bookmark nodes, keep favourites in the node library, and add draggable bends to wires.
- Save the whole project from the main Save button or Ctrl+S.
- Keep local recovery copies of unsaved graphs and projects. Recovery UI tested with simulated browser storage; restart persistence still needs testing.
- Prevent graph shortcuts from acting behind open dialogs.

## Workspace

- Search across graphs, switch between open tabs, and use the minimap to navigate.
- Arrange a graph automatically, hide unused data pins, copy and paste node groups, and resize side panels.
- Save and load themes.
- Move action chains into functions with automatically added inputs and one return value when safe.
- Added tests for these tools and checked that visual changes leave gameplay code unchanged.

## Graph editing

- Select, move, align and space several nodes at once. Put related nodes in named, coloured groups.
- Move a self-contained action chain into its own function. Double-click calls to open their graphs.
- Create functions using name, type and input fields. Organise functions and macros into folders.
- Customise the canvas, nodes, pins, wires, colours, text and panel widths. Choose a light theme or reset to the original dark style.
- Added tests for graph appearance, function extraction, and browser editing. Appearance changes do not affect generated script code.

## Scene layouts

- Open native 2D/3D scene JSON, drag entity markers and edit properties.
- Save scenes with a project and add the native load node to a graph.
- The preview is top-down; a full rendered viewport is not included.

## v3 — 27 November 2026

- Interfaces for calling matching behaviours on other entities. Added `Entity.behaviourAt(uint)` to Nexgen; rebuild the engine to use these nodes.
- Timelines with several typed tracks, editable curves and playback modes.
- Gameplay and animation state machines for sprites and models.
- Reusable 2D/3D entity hierarchies.
- Includes the earlier graph compiler, reusable functions and macros, inheritance, local dispatchers, project packaging and debugger.

Upstream: `54bc84ce91be85d4c7e29b26f4d748547477266e`. Original Xbox, Windows and macOS builds and full graphical tests remain unverified. See README.md and ADVANCED.md for details.
