# Changes

## Import existing project folders

- Review each existing screen before converting it. Keep unchecked or unsupported screens as original files. Preserve metadata, assets, extra scripts and global paths, with exact original main-script backups. Refuse automatic test staging when it could overwrite installed files.

## Mixed scripts and graphs

- Optionally retain unsupported function bodies when importing a supported class. Call handwritten methods from graph nodes, edit their bodies, and keep the original source. Protect imported names and signatures while handwritten functions remain. Use Nexgen to check the resulting script.

## Import AngelScript

- Review supported handwritten Screen and Behaviour scripts as editable graphs, keep the original source, and refuse unsupported syntax without changing the current graph. Optionally compile both scripts with Nexgen.

## Native API coverage and safer connections

- Add 84 existing platform APIs from the Nexgen reference, including availability checks and console information.
- Check that the catalogue and native compiler fixtures are current.
- Reject circular connections before changing the graph or its undo history.

## A tidier toolbar

- Keep common actions visible and group the rest under File, Game, Development and View. All existing commands remain available.
- Menus support arrow keys, Enter and Escape, and stay inside the window. The full toolbar can still be shown.

## Large graphs

- Draw wires once after node layout, pin visibility and folded groups have been updated. Keep existing wire elements when moving nodes instead of recreating them.
- Cancel a wire drag when the window loses focus or the pointer is cancelled.
- Added a browser regression test with 401 nodes and 400 wires, including folded groups and unchanged script output.

## Node library

- Search native functions by name, category, description or pin, with words in any order. Fold categories and use the keyboard to insert nodes. Open the original Nexgen reference from Details. Hide action nodes in value-only functions and prevent direct self-calls from the library.

## Use the engine first

- Removed Studio’s duplicate 3D renderer and camera; open the original Nexgen editor for visual 3D work. Kept project scene links and simple 2D layout.
- Bring supported dropped media back with a native scene. Keep recovery files if collection fails.
- Replace simple Space load nodes with the native Level loader after confirmation, retaining execution wires. Check scene asset references.
- Pick native 2D prefabs directly from project files.
- Use Nexgen’s existing script profiler from the Debug panel, including a mode without Blueprint trace logging.
- Added development rules to avoid rebuilding systems Nexgen already provides.

## Fly camera and native level editing

- Added a perspective view with right-mouse WASD movement, Q/E height, speed controls and near-plane clipping.
- Open scenes in a temporary copy of Team Resurgent’s original scene editor; import its saved JSON and keep the original showcase untouched.
- Use the existing Level helper for full scene files. Added native level load, update, HUD and entity nodes and warnings for using the wrong loader.
- Tested camera input, staging, scene import, cleanup and the Level method signatures. Full native graphical testing is still needed.

## Scene editor

- View and place native 3D shapes from orbit, top, front and side views. Move on the ground or change height, snap to a grid, pan and zoom.
- Edit transforms, colours, lights and Behaviour assignments. Choose model, texture and sprite files from the project. The preview uses placeholders for imported models and sprites.
- Undo and redo scene edits. Save native JSON and connect its load node to On Enter; graph undo restores the saved scene too.
- Added tests for projection, object placement, scene editing, and native loading of shapes, a collider, a light and a Behaviour.

## Shared media

- Keep textures, sounds, models and other files in one project library. Use Nexgen’s native `global:` paths in graphs and UI pages.
- Import, preview, replace and remove shared files from Assets. Copy screen files into the library without breaking existing paths.
- Package shared files once. Play testing stages and removes only the project’s own shared folder and refuses collisions.
- Check managed global paths and total project asset limits. Added a native test that reads one shared file from two generated screens.

## Shared behaviours

- Keep one Behaviour definition in the project and link it to several screens.
- Edit a linked Behaviour once; all linked screens use the changes. Undo and redo keep the library and screen copies together.
- Make a local copy when a screen needs different logic. Import and export library entries and see where they are used.
- Warn about missing literal asset paths in screen and Behaviour graphs. Standalone graph exports keep their own copies.

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
