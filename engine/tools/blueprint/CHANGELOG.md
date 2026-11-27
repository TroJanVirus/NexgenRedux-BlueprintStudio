# Changes

## Game UI designer

- Build menus and HUDs with Nexgen widgets. Edit layout, colours, named styles and widget states; preview 16:9 and 4:3.
- Create Blueprint functions for widget events and add the native load and draw nodes to simple screen graphs.
- Save native pages and themes with the project. Undo/redo keeps those files and functions together; handler renaming updates connected pages.
- Check for missing UI files, assets and handlers before packaging. Added browser tests and a native gamepad callback test. The browser preview is approximate; actual rendering and Xbox testing remain outstanding.

## Play testing

- Run and stop a desktop game from Studio through the local bridge.
- Send updated scripts to Nexgen in dev mode without exporting a ZIP.
- Use a dedicated test copy and temporary saves; never overwrite an existing screen.
- The launcher is tested with a local process. Actual Nexgen window and reload tests remain outstanding.

## Project editing

- Edit or create behaviours inside their screen. Save the whole project without leaving the behaviour graph.
- Browse project assets, import folders, preview images/audio/text and choose an asset for a node input.
- Save and reopen unfinished projects. Check all screens before packaging.
- Reject conflicting file and folder paths.

## Organising graphs

- Fold nested groups into blocks with working external pins. Expand them to edit their contents.
- Edit function inputs and return types without losing compatible wires.
- Export the visible graph as an SVG image and choose a panel arrangement.
- Added tests for folded groups, function inputs and image export.

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
