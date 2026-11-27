# Blueprint Studio

Make games for Nexgen Redux using connected nodes instead of writing every line of AngelScript by hand. Keep each job in a small function, reuse behaviours between entities, and organise graphs the way you like.

The editor runs in a desktop browser. The game runs in Nexgen as usual; it does not need the editor, Node.js or a new scripting engine. Studio is a supplement to Nexgen, not a replacement. See [Development rules](DEVELOPMENT.md) for what belongs in the editor and what stays in Nexgen.

## Getting started

The home-screen menu entry is available in Windows, Linux and macOS desktop builds, not console or UWP builds. It opens the local HTML bundle without an internet connection or Node.js at runtime. Source builds with Node installed generate the bundle automatically; otherwise run `node tools/blueprint/bundle.js` on a machine with Node and place the resulting HTML beside `nexgen` (macOS: `Nexgen.app/Contents/Resources`). `nexgen --blueprint-studio-check` verifies that the packaged file is present without opening a window. The menu currently opens the standalone editor; native Play test, debugging and Cook still require the opt-in local bridge described below. It is not an embedded editor window.

The toolbar keeps Save, Compile and Play test close at hand. Open **File** for documents and exports, **Game** for scenes, assets and project tools, and **Development** for debugging and recovery. Graph appearance and image export are under **View**. Choose **File > Show all toolbar commands** to use the full toolbar instead.

1. On desktop, start Nexgen, open its engine menu from the home screen and choose **Blueprint Studio**. The bundled editor opens in your default browser with the Arcade example. Alternatively open `NexgenBlueprintStudio.html` or `index.html` directly.
2. Choose an event from **My Blueprint**. Events run when something happens in the game, such as entering a screen, updating a frame or hitting another entity.
3. Right-click the canvas to find a node, or drag a node from the library.
4. Connect white execution pins to decide what runs next. Connect matching data pins to pass values between nodes.
5. Select a node to edit its values in Details. A connected input uses the wire instead of its default value.
6. Click **Compile** to check your graph. Click an error to find the offending node.
7. **Save graph** keeps the editable Blueprint. **Export .as** saves the script Nexgen runs.

**Screen** blueprints control menus, levels and other screens. **Behaviour** blueprints contain reusable entity logic, such as movement, damage or pickups. Import a Behaviour into a Screen and use its Attach node to add it to an entity. Each entity has its own copy of the behaviour's variables. Imported components and parents are saved copies by default. Use Shared behaviours to keep one Behaviour definition for several screens.

## Share behaviours between screens

Open a Screen in a project and select one of its Behaviours. **Share in project** adds it to the project library. On another screen, open **Shared behaviours** and choose **Link to screen**. The normal Attach node is available on both screens; each entity still has its own variables and state.

Double-click a linked Behaviour to edit it. Returning to the screen or saving updates all linked screens. Undo or redo the editing session from that screen to undo or redo the shared change. The library lists which screens use each definition. Choose **Make local copy** if a screen needs to keep a different version, or remove its component to stop using it. An unused library entry can be removed separately. Replacing an existing definition or a different local copy requires confirmation.

The project file keeps the shared definition and its links. Screen copies are refreshed from the library when opened, checked or packaged. Individual graph exports contain independent copies so they work outside the project. Sharing changes the editor's source management, not how Nexgen runs scripts.

**Check project** reports compile errors and warns about missing files in literal asset-loading calls. A shared Behaviour can use the current screen’s own `asset:` files or shared `global:` files from the project library. Paths built from variables or other code and files referenced inside models or scene JSON are not checked by this warning. The warnings do not prevent packaging. Existing projects with local components continue to work.

## Keep graphs small

A good event graph should read like a short list of instructions. For a player, for example, Update might call `ReadInput`, `MovePlayer` and `UpdateAnimation`. Put the details in their own functions. Use named variables to keep values you need in another event.

- Click **+** beside Functions to create one. Choose the name, folder, return type and any inputs using ordinary fields. A value-only function returns a result without action wires.
- Double-click a function or macro call to open its graph. Use the back arrow to return.
- Shift-click nodes or Shift-drag empty space to select several nodes. Drag a selected node to move the selection. Use Details to align, space or group nodes.
- **Move to function** turns a self-contained chain of actions into a new function and leaves one call behind. Include its value nodes; the selection needs one incoming execution wire and at most one outgoing execution wire. The editor adds inputs for values already available before the first action, and a return pin if one captured result is needed outside the function. Include live value nodes in the selection so they are still read at the right time. Selections that cannot be moved safely are refused.
- Give a group a name, then click its heading to select its nodes. Double-click the heading to change its name and colour. Remove the group without removing the nodes.
- Assign functions or macros to folders from Layout. Click a sidebar section heading to fold it away. Use **Focus** for a larger canvas.

## Find and tidy your work

- Search the node library using words in any order, such as `camera space` or `Scene::spawn`. Matching native functions appear first within the best matching categories. Click a category heading to fold its list. Use the arrow keys and Enter to insert a result, or Escape to close the right-click menu. Native API nodes have a link to the original Nexgen reference in Details. Value-only functions hide action nodes, and a function cannot insert a call to itself from the library.

- **Find** (Ctrl+F) searches function names, folders, node names, comments and default values across the current Blueprint. Click a result to jump to it.
- Open graphs appear as tabs. Switch between them without losing your zoom and position.
- The minimap shows the whole graph. Click or drag it to move around. Hide it from **Tools** if you prefer.
- **Tools → Arrange whole graph** lays out the nodes from left to right, following their connections. Undo if you prefer your original layout. The arrangement never changes a wire or the generated script.
- Copy selected nodes with Ctrl+C and paste with Ctrl+V, or use Tools. Internal wires and complete groups are kept; outside wires are left behind. You can paste into another graph or Blueprint in the same editor session, provided its required variables and functions exist.
- Select a node and use **Hide unused data pins** to keep just connected pins and action pins visible. Show them again to add new connections.
- Drag the inside edge of either side panel to resize it. You can also focus the divider and use the arrow keys.
- Save or load your own colour and layout theme from Tools.

### Fold groups and edit functions

Use **Fold** on a group heading to hide its nodes behind one block. Its connected inputs and outputs stay visible, so wires to other groups still work. **Expand** restores the original layout. Groups can contain smaller groups; partly overlapping groups must be separated before folding. Search and bookmarks open a folded group when you jump to one of its nodes.

Select the entry node of a function and choose **Edit function inputs**. Add, reorder or rename inputs, or change the return type. Wires follow renamed inputs at every call in the Blueprint. Disconnect a pin before removing it or changing its type. Parent overrides, interface methods and bound dispatcher handlers must keep their required signatures.

**Graph image** saves the whole visible graph as an SVG, including folded groups, without toolbars or editor panels. Use it for documentation or sharing. **Panels** offers standard, swapped and details-only layouts. These are presets, not free-form docking.

## Make it look the way you want

**Layout** controls the canvas, grid, node colours, wire colours and styles, text size, node width, panel colours and widths. Choose a light theme or set colours yourself. Hide the grid, category labels, default values or results panel. Enable snap to grid for precise placement.

Select one node to set its display title, header and body colours, title colour and width. Its real function and pins remain the same. Keep custom titles meaningful so other developers can understand the graph. Reset a node's style to use the Blueprint defaults again.

Graph positions, groups, folders and appearance are saved with the `.bp.json` file. Changing those settings does not change the generated gameplay script. Focus and panel visibility are editor controls, not game settings. Layout controls the Blueprint editor. Use UI Designer to change the appearance of a game menu or HUD.

## Editing and recovery

- Select a variable or a local variable and choose **Rename** in Details. To rename a function or macro, open its graph and select its entry node. Calls, reads, writes and local event handlers follow the new name. Parent overrides and interface methods must keep their original names.
- Use the star beside a library entry to keep it in Favourites. Bookmark important nodes in Details and jump to them from **Bookmarks**. Bookmarks are saved in the graph, while favourites belong to this browser.
- Click a wire and use **Add bend** in Details. Drag the blue handles to route it around other nodes. The bends move with the output pin. Reset the route to return to the selected wire style. Bends are saved and can be copied with a group of nodes.
- **Save project** appears in place of Save graph when a project screen is open. Ctrl+S then saves all screens and assets. To save just one graph, choose **Export current graph** in the Project window.
- **Recovery** keeps a local copy of unsaved graphs and projects (including assets), checking every two seconds. Open the Recovery menu to restore, export or delete a copy. A check mark means the last copy was saved; an exclamation mark means storage is unavailable or a write failed. Copies are local to the browser and address you opened Studio from. Clearing browser data can remove them. Keep regular graph or project files as your main backup.

## Work on a project

Open a screen's Behaviour Components list and double-click a behaviour to edit it without leaving the project. Use **New behaviour** to create one directly in the screen. **Back to screen** returns to the screen graph. Save keeps the whole screen or project, including your behaviour edits. Undo from the screen can undo the behaviour editing session. Components in different screens are still separate copies; editing one does not change the others.

Use **Assets** to import files or folders into the current screen. Search by name or folder, preview images, audio and text, copy the asset path or export a file. For an unconnected string input, open **Use a project asset** in Details to choose a file without typing its path. The editor prevents duplicate or unsafe file paths and asks before replacing files. Model files can be stored and exported but are not rendered in the browser.

### Share files between screens

In Assets, switch to **Shared project files** to import files that every screen can use. The project chooses a stable folder name from its name when you first add shared files. Select a shared file from a node's asset picker or the UI Designer's image picker; the resulting path starts with `global:blueprint_`, for example `global:blueprint_demo/textures/player.png`.

The package includes these files once under `media/global/blueprint_<name>/`. Nexgen already understands `global:` paths; no engine changes are needed. Replacing a shared file keeps its existing path (including letter case) and affects every screen that uses it. **Copy to shared** copies an existing screen file without changing or deleting the original. Update any old `asset:` references to use the new shared path before removing the local file. Shared files and local files together count towards the 64 MiB / 4096-file project limit. Save the project to keep the library; individual `.bp.json` exports do not include media.

Removing a shared file asks for confirmation and lists known references. Check project warns about missing managed global files in supported literal graph calls and checks registered UI image, font and sound paths. Other engine-wide global paths, computed filenames and arbitrary references inside models or scene files are not fully validated. Asset imports, replacements and removals are not graph undo steps; keep project backups.

**Check project** checks the graphs in every screen and lists any errors before packaging. Projects with unfinished graphs can be saved and reopened; only packaging requires every screen to compile. Checking a project does not run the game or prove that assets are valid for Nexgen or the Xbox.

## Design a menu or HUD

Open **UI Designer** while working on a Screen. The starter menu is ready to edit, or choose **Example HUD**, **New blank page** or open a native widget JSON asset from the project. Select a widget in the preview or tree. Add children to a container, drag a widget to another container or use Move up/down to change the order. Edit its text, ID, size, padding, alignment and other properties. The preview offers widescreen and 4:3 shapes; it is an approximation, not the actual Nexgen renderer.

Use **Game theme** to edit colours, named styles, text sizes, borders, padding and appearances for focus, hover and other states. A widget can use a built-in style or one you create. More native properties, fonts, images and sounds can be set in the advanced JSON fields. Import images through Assets first, then select them in the widget's image field. The designer works with the same native JSON that Nexgen reads, so the layout and themes need no extra runtime.

For a button, choose a click function or create one from **Widget events**. The function receives the actual Widget reference; wire its pins into normal Blueprint nodes. Save and open the function to edit it. Toggles, sliders, fields and choices can use change functions; custom widgets and lists can use native draw and row functions. List contents and custom drawings are not simulated by the browser. Use `Ui::find` with a widget ID and the Widget nodes to change text, progress, visibility and other properties from your gameplay graphs.

- **Save page** saves native JSON and any new functions with the project. It can save a draft even if a function is unfinished.
- **Save + connect to screen** adds native theme/load nodes to On Enter and a draw node to Draw if needed. It appends to simple action chains. If an event branches, or has disconnected matching nodes, save the page and connect those nodes yourself. The automatic command does not remove or replace existing gameplay logic.
- Save the project with Ctrl+S. The normal game package includes the page and theme in the screen's assets. Undo and redo UI Designer saves and handler renames restore both graph and UI files. Renaming a function updates registered pages; unlink a handler before deleting it or changing its required types.
- Check project reports missing page/theme files, missing local and project-managed shared image/font/sound paths, and invalid widget handlers or theme properties. It cannot check every dynamic asset path or guarantee how a screen will look. To remove a registered page or theme, first remove the matching load/theme node, then remove the asset in Assets.

Use Play test to see the actual game interface in a desktop Nexgen build. The null-GPU native tests load a generated page and theme and verify that gamepad Accept executes the connected function. Full rendering, custom fonts and on-console UI performance have not yet been verified.

## Run a game

Use **Project** to collect screens and their assets. **Package** exports a ZIP containing the scripts and media folders. Copy the `media/` contents into an existing Nexgen release. The package is game content, not an engine executable or XBE. Save the project separately if you want to edit it later.

For a quick test of the Arcade example, export the script as `media/showcase/blueprint_arcade/main.as` in a Nexgen release. Add a `screen.json` beside it:

```json
{"title":"Blueprint Arcade","description":"Arcade example","category":"Blueprints","order":1000}
```

Run `nexgen --screen blueprint_arcade`. Add `--dev` to use Nexgen's script reload support. On Xbox, copy the screen folder into the same media location. Keep the editable graph on your computer. Use File → Import AngelScript to review and convert supported handwritten scripts into graphs.

Most scripts work with the existing engine. Interface messages use the new `Entity.behaviourAt(uint)` binding; rebuild Nexgen from this branch before running them.

### Cook a release package

Use the original [Nexgen Cook](../cook-ui/README.md) command to prepare images and sounds for desktop or original Xbox. Start the local bridge with the `cook` executable from a Nexgen Cook release (not the `nexgen-cook` window):

```sh
node debug_bridge.js --cook /path/to/cook
```

Open the bridge address, choose **File > Package**, then choose a cooked desktop or original Xbox release. This compiles the graphs, stages a snapshot of the media in a private temporary folder and invokes the existing native Cook tool. The download contains cooked media, normal scripts, metadata and the unchanged editable Blueprint sources. A failed conversion does not produce a package. Original source assets, existing game installations and the open project are never changed. Normal release/debug exports still work without Cook. You can combine `--cook` and `--preview-engine` when starting the bridge.

Models are copied unchanged in this integration: the native model importer can follow external file references, so use the existing Nexgen Cook window or command separately for trusted models and their dependencies before importing them into Studio. No second image, sound or model converter is included. Output paths are preserved; the internal `.cooked` marker is not packaged. Output is limited to 128 MiB of cooked media, with a two-minute limit per run.

For the same workflow without the browser:

```sh
node cli.js project Game.bp.project.json --out Game-xbox.zip --cook /path/to/cook --platform xboxog
```

Use `--platform desktop` for a desktop package. Cooked packages are release builds, not debug builds. Check the resulting game on the target hardware; cooking does not guarantee that its assets fit in console memory.

### Play test from Studio

To avoid exporting files by hand each time, make a separate copy of a desktop Nexgen release for testing. Keep its `media/showcase` folder. Start the local bridge from the Blueprint tools directory:

```sh
node debug_bridge.js --preview-engine /path/to/test-copy/nexgen
```

On Windows use the path to `nexgen.exe`. For a macOS app bundle use the executable inside `Contents/MacOS`. Put quotes around paths with spaces. Open `http://127.0.0.1:8765/`, choose **Play test**, select the starting screen and click **Run game**. This launches the desktop engine, not an emulator inside the browser.

While it is running, edit your graphs and choose **Update scripts** to send the latest compiled scripts to Nexgen's existing `--dev` reload system. Changes to assets, screen names or the list of screens need a stop and restart. **Stop** closes the test and removes the screen folders and temporary saves created for that run. The bridge refuses to overwrite any existing screen folder or the project’s shared media folder. Stop and restart after changing shared files. If the bridge is killed abruptly, leftover test folders may need manual removal; it will not delete them on the next run. Do not put irreplaceable files in test folders.

The launcher and script updates have been tested with a real local child process. A full Nexgen window, live game reload and platform-specific launches still need testing. The standalone HTML editor can be used without the bridge, but cannot start a game process by itself.

### Profile scripts

The Debug panel uses Nexgen's built-in script profiler. Install the normal exported `.as` (without node tracing), start Nexgen with its debugger enabled, then use **Attach for profiling**. Click **Start profile**, play the game, then **Stop profile**. The report shows the native sample counts for functions and script lines. Where the reported function belongs to the open Blueprint, click it to open its graph. Save the native report as JSON. These counts are script samples, not GPU timing or total CPU usage. Profiling is also available while graph debugging, but trace logging adds overhead.

## Import existing AngelScript

Open **File → Import AngelScript**. Choose a UTF-8 `.as` file (up to 256 KiB), or paste a script, and choose **Review conversion**. Studio shows the generated script and graph/node counts. If the local debug bridge has a native validator, **Check with Nexgen** compiles both the original and converted scripts. Open the converted graph only when you are happy with the result. The original source is retained in the saved Blueprint and can be downloaded from the import window. Keep your original game files as well.

This first importer supports one Screen or Behaviour class with scalar fields, normal event signatures, typed functions, simple assignments, scalar local variables in functions, arithmetic, comparisons, braced `if`/`else` blocks, returns and native calls with exact argument types. It uses the existing node catalogue and graph compiler. Names and source-line references are retained; the original text retains comments and formatting. A function call that has side effects is only supported as a standalone action. In strict mode the importer refuses unsupported syntax, unknown or ambiguous calls, implicit type conversions, constructors, includes, extra classes, loops, reference parameters and other unsupported features. It does not guess at missing logic or drop unsupported functions. A rejected conversion leaves the open graph unchanged. It does not import a complete folder or assets, or execute user code.

This is a reviewed conversion, not lossless round-trip editing or proof of equal gameplay. The regenerated script has a new layout and may differ in internal variables and debug information. Keep the original script and test the game before replacing it. Source-line references refer to the original import, not later graph edits. Full AngelScript parsing is not supported yet. For importing multiple screens and their files, see Import existing Nexgen folders; for mixed code and graphs, see below.

## Other tools

| Tool | Use it for |
|---|---|
| Variables and arrays | Health, score, inventory and other saved values |
| Branches, loops and delays | Decisions, repeated jobs and waiting |
| Macros | Reusing a small group of nodes with custom pins |
| Parent Blueprints | Sharing common functions and variables between classes |
| Interfaces | Sending a named message to an entity with a matching behaviour |
| Event dispatchers | Notifying several handlers on the same object |
| Tween / Timeline | Native Tween for simple changes; Timeline for multiple tracks and graph events |
| State machines | Idle, walking, attacking and other gameplay or animation states |
| Entity hierarchies | Spawning a player, weapon or other group of related entities |
| Scenes | Open the original Nexgen 3D editor, link Behaviours and manage scene files; simple 2D layout |
| Debugger | Pausing at a node and inspecting values while the game runs |

See [Advanced use](ADVANCED.md) for the detailed instructions and limits of these tools.

## Mixed AngelScript and graphs

In **File → Import AngelScript**, enable **Keep unsupported function bodies as AngelScript**. Studio attempts the supported graph conversion method by method. A method that cannot be translated keeps its original body and parameter names; it appears as Script in the graph list. Open it to read or edit the body, and call it from ordinary function nodes in other graphs. The original source is still available separately. The game uses ordinary AngelScript for both kinds of function.

The import still requires one Screen or Behaviour class with supported scalar fields and supported method signatures. Includes, other classes, constructors, complex member declarations and unsupported signatures are not imported. Some errors in the complete converted graph still cause the entire import to be refused. Handwritten bodies are not interpreted by Studio; check the original and result with Nexgen before using them. Editing a body checks its boundaries and graph structure, not AngelScript semantics. Class names, member names and method signatures are protected because handwritten code may refer to them. Automatic scene and UI wiring leaves handwritten events alone; add those calls in code. Keep original files and assets.

To turn an individual retained function into a graph, open it, edit its body if necessary, then choose **Review conversion to graph**. Studio uses the current body and the current class members, not the original file. The review shows the body and generated script; the optional native check compiles both versions. Unsupported syntax or an invalid Blueprint leaves the project unchanged. Convert only when satisfied, and use Undo to return to the handwritten body. The saved graph keeps the exact pre-conversion body; select its entry node in Details to export that text. Original source-line links are not reused for the edited body. Other graphs and the original imported source stay unchanged. Imported signatures remain protected, even after the last retained body is converted, because other scripts may still refer to them.

## Build and test

From this directory, with Node.js 18 or newer:

```sh
node bundle.js NexgenBlueprintStudio.html
node cli.js check examples/arcade.bp.json
node cli.js screen examples/arcade.bp.json --out /path/to/media/showcase/blueprint_arcade
node cli.js project examples/showcase.bp.project.json --out BlueprintShowcase.zip
node test_compiler.js
node test_scene_editor.js
node test_layout.js
node test_workspace.js
node test_polish.js
node test_organise.js
node test_game_tools.js
node test_playtest.js
node test_ui_design.js
node test_shared_behaviours.js
node test_shared_media.js
node test_scene_viewport.js
node test_native_scene.js
node test_node_library.js
node test_script_import.js
node test_mixed_script.js
```

No npm packages are needed. To run the optional browser tests, install Python Playwright and Chromium, then run `python test_browser.py` and `python test_layout_browser.py` and `python test_workspace_browser.py` and `python test_polish_browser.py` and `python test_organise_browser.py` and `python test_game_tools_browser.py` and `python test_playtest_browser.py`, `python test_ui_design_browser.py` , `python test_shared_behaviours_browser.py` and `python test_shared_media_browser.py`, `python test_scene_viewport_browser.py` and `python test_native_scene_browser.py` and `python test_profile_browser.py`, `python test_node_library_browser.py` and `python test_graph_performance_browser.py` and `python test_command_bar_browser.py`. Set `CHROMIUM_PATH` if Chromium is installed outside Playwright's usual location. The recovery test uses real browser storage by default; `BLUEPRINT_MOCK_STORAGE=1` checks its UI with a simulated storage backend on restricted test hosts. Actual storage across a browser restart has not been verified on this branch.

The 804 engine API definitions are based on Nexgen's script reference. The compiler checks data types and graph structure; the native engine compiler checks the resulting AngelScript. Neither check guarantees that all assets are present or that a game performs well on real hardware.

## Build a scene

Open **Scenes** while editing a Screen. For 3D, choose **Open in Nexgen** to use Team Resurgent's [original scene editor](https://github.com/Team-Resurgent/NexgenRedux-Media/tree/main/media/showcase/editor). Studio does not have a second 3D renderer. Nexgen already handles models, terrain, lights, sky, water, roads, triggers and play modes, with a right-mouse fly camera.

Start the local bridge with `--preview-engine` pointing to a dedicated desktop Nexgen release that includes the media and original editor (see Play test above). Studio opens a temporary copy of the original showcase with your project files and isolated saves. The original editor is never overwritten. Conflicting file names are refused.

In Nexgen, hold the right mouse button and use the movement keys (W/A/S/D with the default bindings) to fly. Move the mouse to look, use the wheel to move in and out and middle-drag to pan. Save the level there, then choose **Import saved scene** in Studio. Select a named object in the list to connect its Behaviour or pick a project asset. Advanced native JSON remains available when needed.

Dropped files referenced with `own:` are brought back from the isolated saves folder and given project-local `asset:` paths. Self-contained models and common image/audio files are supported; use [Nexgen Cook](../cook-ui/README.md) for models with external dependencies. Other scenes created during the native editing session are not imported. If collection fails when Nexgen closes, the bridge keeps its scene and saves folders and reports their paths in the play-test log instead of deleting them. Keep a saved project as a backup; the original editor may normalise scene properties.

**Save to project** stores the native JSON and imported files. **Save + connect On Enter** also adds the native load node. Undo in Studio restores the changed files and graph together. The scene dialog also has local undo and redo. For manual editing, import/export JSON; the editor lists only scene JSON assets, not unrelated widget or theme files.

For 2D, Studio retains a simple layout for placing rectangles, discs and image bounds in native scene files. It is not a game renderer. Use native `prefabs` and `children` in scene JSON for reusable 2D objects. The existing `Scene::spawn(prefab, position)` node offers a picker for prefab names in project scene files; load the file with `Scene::load` first. Blueprint hierarchy templates remain available for graphs that need separate output pins for named children, and existing templates still work.

### Use a full level in a graph

A file made by the native editor can contain terrain, roads, sky, triggers and a HUD. `Space::load` creates entities only; it cannot run those extra features. **Save + connect On Enter** chooses **Load level** for a full native level. This uses the existing `Level` class from `global:scene.as`; it does not add a new level runtime. Existing plain 3D scene files still use `Space::load`.

The Native level nodes include Update level, Draw level HUD, Find level entity, Is level object on, Set HUD number/text, Show/Hide level entity and Clear level. Wire Update level to Update's dt and an optional player reference; it returns the next scene and arrival when the player enters a way out. Draw the space, then Draw level HUD using the Draw event's canvas. Studio creates these calls for empty Update/Draw graphs; it leaves existing graphs alone. If a basic Space load node already uses this file with default position/streaming and no data connections, Studio can replace it in place after confirmation. More complicated connections must be updated manually. Check project warns about rich levels connected to `Space::load`.

Generated scripts include the original `global:scene.as` and need the Nexgen media containing that script and `global:ai.as`. The local native compiler can read both from the configured desktop test copy. The test suite checks the emitted calls against the helper's method signatures, not the full helper's runtime. The original editor and actual graphical level rendering still need to be tested in a complete desktop release.

## Current limits

Studio uses the original editor for visual 3D editing; the native launch/import workflow has been tested with a substitute desktop process, not a full rendered Nexgen release. Timelines cannot yet be controlled from other graphs. Dispatchers work within one object, and there is no automatic network replication or remote procedure call system. Graph debugging uses Nexgen's existing debugger and currently highlights the active graph only. There is no skeletal blend-tree editor.

Linux headless tests and Chromium tests have been run. Windows, macOS, RXDK builds, graphical rendering and real Xbox gameplay are still unverified for this branch. No rebuilt XBE is included.

### Platform-specific nodes

The Xbox, Xbox 360, Xbox One, Windows, Mac and Linux categories expose Nexgen's own platform APIs, including temperature readings, console controls and desktop integration. Use the corresponding **Get available** node before platform-only operations. On other platforms, the original engine supplies the documented fallback values or no-op behaviour. These calls are not emulated by Studio.

Studio rejects a connection that would create an execution or data cycle before changing the graph. Use a loop node for repeating actions and a variable for retaining a previous value.

## Import an existing Nexgen project folder

Choose **File > Import Nexgen folder**. Select a single screen folder, `showcase`, `media`, or a project folder containing `media/showcase`. Studio lists the screens and checks which scripts can be converted with the current AngelScript importer. Review the generated code, then tick the screens you want to convert. At least one screen must be convertible and selected. Unchecked screens are kept as original files, even when they contain unsupported classes or includes.

The original files are never written to. The project retains screen metadata, assets, extra script files and selected `media/global` files under their original paths. The unconverted main script of each edited screen is kept in the package's `blueprints/` folder as an exact backup. Other files outside supported folders are listed as excluded. Limits are 128 screens and 64 MiB / 4096 files, including original source backups. Screen IDs must follow the existing Studio ID rules.

The project window lists unconverted screens separately and lets you export their original files. They are packaged unchanged, not compiled, edited or traced by Studio. Converted screens still need native compilation and gameplay testing. Play test can start either converted or original screens. It stages original screens and imported global files at their native paths in the dedicated test copy, with isolated saves. Any existing screen or top-level global file or folder (even a case-only match) is refused before staging; Studio never merges into an existing folder. Use a clean test copy or install a package manually if paths conflict. Update scripts only replaces generated Blueprint main scripts; changes to original scripts, dependencies, metadata or assets require a restart. Stop removes newly staged files and folders. Native scene editing still requires a manual setup for projects with imported global files. A complete project with no convertible screen, arbitrary directory layouts, and conversion of multiple classes or include chains are not supported yet.

To test the Cook integration with the actual native executable, set `NEXGEN_COOK` to its absolute path and run `node test_cook_package.js` and `python test_cook_browser.py`. The browser test can use `BLUEPRINT_FORWARD_HTTP=1` on a restricted host; that forwards the request through Python to the real local bridge.
