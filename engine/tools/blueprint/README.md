# Blueprint Studio

Make games for Nexgen Redux using connected nodes instead of writing every line of AngelScript by hand. Keep each job in a small function, reuse behaviours between entities, and organise graphs the way you like.

The editor runs in a desktop browser. The game runs in Nexgen as usual; it does not need the editor, Node.js or a new scripting engine.

## Getting started

1. Open `index.html` in a desktop browser. The Arcade example is loaded for you.
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

**Check project** reports compile errors and warns about missing files in literal asset-loading calls. A shared Behaviour uses the current screen's asset folder; add the files it needs to each linked screen. Paths built from variables or other code and files referenced inside models or scene JSON are not checked by this warning. The warnings do not prevent packaging. Existing projects with local components continue to work.

## Keep graphs small

A good event graph should read like a short list of instructions. For a player, for example, Update might call `ReadInput`, `MovePlayer` and `UpdateAnimation`. Put the details in their own functions. Use named variables to keep values you need in another event.

- Click **+** beside Functions to create one. Choose the name, folder, return type and any inputs using ordinary fields. A value-only function returns a result without action wires.
- Double-click a function or macro call to open its graph. Use the back arrow to return.
- Shift-click nodes or Shift-drag empty space to select several nodes. Drag a selected node to move the selection. Use Details to align, space or group nodes.
- **Move to function** turns a self-contained chain of actions into a new function and leaves one call behind. Include its value nodes; the selection needs one incoming execution wire and at most one outgoing execution wire. The editor adds inputs for values already available before the first action, and a return pin if one captured result is needed outside the function. Include live value nodes in the selection so they are still read at the right time. Selections that cannot be moved safely are refused.
- Give a group a name, then click its heading to select its nodes. Double-click the heading to change its name and colour. Remove the group without removing the nodes.
- Assign functions or macros to folders from Layout. Click a sidebar section heading to fold it away. Use **Focus** for a larger canvas.

## Find and tidy your work

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

**Check project** checks the graphs in every screen and lists any errors before packaging. Projects with unfinished graphs can be saved and reopened; only packaging requires every screen to compile. Checking a project does not run the game or prove that assets are valid for Nexgen or the Xbox.

## Design a menu or HUD

Open **UI Designer** while working on a Screen. The starter menu is ready to edit, or choose **Example HUD**, **New blank page** or open a native widget JSON asset from the project. Select a widget in the preview or tree. Add children to a container, drag a widget to another container or use Move up/down to change the order. Edit its text, ID, size, padding, alignment and other properties. The preview offers widescreen and 4:3 shapes; it is an approximation, not the actual Nexgen renderer.

Use **Game theme** to edit colours, named styles, text sizes, borders, padding and appearances for focus, hover and other states. A widget can use a built-in style or one you create. More native properties, fonts, images and sounds can be set in the advanced JSON fields. Import images through Assets first, then select them in the widget's image field. The designer works with the same native JSON that Nexgen reads, so the layout and themes need no extra runtime.

For a button, choose a click function or create one from **Widget events**. The function receives the actual Widget reference; wire its pins into normal Blueprint nodes. Save and open the function to edit it. Toggles, sliders, fields and choices can use change functions; custom widgets and lists can use native draw and row functions. List contents and custom drawings are not simulated by the browser. Use `Ui::find` with a widget ID and the Widget nodes to change text, progress, visibility and other properties from your gameplay graphs.

- **Save page** saves native JSON and any new functions with the project. It can save a draft even if a function is unfinished.
- **Save + connect to screen** adds native theme/load nodes to On Enter and a draw node to Draw if needed. It appends to simple action chains. If an event branches, or has disconnected matching nodes, save the page and connect those nodes yourself. The automatic command does not remove or replace existing gameplay logic.
- Save the project with Ctrl+S. The normal game package includes the page and theme in the screen's assets. Undo and redo UI Designer saves and handler renames restore both graph and UI files. Renaming a function updates registered pages; unlink a handler before deleting it or changing its required types.
- Check project reports missing page/theme files, missing local image/font/sound paths, and invalid widget handlers or theme properties. It cannot check every dynamic asset path or guarantee how a screen will look. To remove a registered page or theme, first remove the matching load/theme node, then remove the asset in Assets.

Use Play test to see the actual game interface in a desktop Nexgen build. The null-GPU native tests load a generated page and theme and verify that gamepad Accept executes the connected function. Full rendering, custom fonts and on-console UI performance have not yet been verified.

## Run a game

Use **Project** to collect screens and their assets. **Package** exports a ZIP containing the scripts and media folders. Copy the `media/` contents into an existing Nexgen release. The package is game content, not an engine executable or XBE. Save the project separately if you want to edit it later.

For a quick test of the Arcade example, export the script as `media/showcase/blueprint_arcade/main.as` in a Nexgen release. Add a `screen.json` beside it:

```json
{"title":"Blueprint Arcade","description":"Arcade example","category":"Blueprints","order":1000}
```

Run `nexgen --screen blueprint_arcade`. Add `--dev` to use Nexgen's script reload support. On Xbox, copy the screen folder into the same media location. Keep the editable graph on your computer. An existing `.as` script cannot be converted back into a graph automatically.

Most scripts work with the existing engine. Interface messages use the new `Entity.behaviourAt(uint)` binding; rebuild Nexgen from this branch before running them.

### Play test from Studio

To avoid exporting files by hand each time, make a separate copy of a desktop Nexgen release for testing. Keep its `media/showcase` folder. Start the local bridge from the Blueprint tools directory:

```sh
node debug_bridge.js --preview-engine /path/to/test-copy/nexgen
```

On Windows use the path to `nexgen.exe`. For a macOS app bundle use the executable inside `Contents/MacOS`. Put quotes around paths with spaces. Open `http://127.0.0.1:8765/`, choose **Play test**, select the starting screen and click **Run game**. This launches the desktop engine, not an emulator inside the browser.

While it is running, edit your graphs and choose **Update scripts** to send the latest compiled scripts to Nexgen's existing `--dev` reload system. Changes to assets, screen names or the list of screens need a stop and restart. **Stop** closes the test and removes the screen folders and temporary saves created for that run. The bridge refuses to overwrite any existing screen folder. If the bridge is killed abruptly, leftover test folders may need manual removal; it will not delete them on the next run. Do not put irreplaceable files in test folders.

The launcher and script updates have been tested with a real local child process. A full Nexgen window, live game reload and platform-specific launches still need testing. The standalone HTML editor can be used without the bridge, but cannot start a game process by itself.

## Other tools

| Tool | Use it for |
|---|---|
| Variables and arrays | Health, score, inventory and other saved values |
| Branches, loops and delays | Decisions, repeated jobs and waiting |
| Macros | Reusing a small group of nodes with custom pins |
| Parent Blueprints | Sharing common functions and variables between classes |
| Interfaces | Sending a named message to an entity with a matching behaviour |
| Event dispatchers | Notifying several handlers on the same object |
| Timelines | Moving, fading or changing values over time |
| State machines | Idle, walking, attacking and other gameplay or animation states |
| Entity hierarchies | Spawning a player, weapon or other group of related entities |
| Scenes | Placing entities in a native 2D/3D scene file |
| Debugger | Pausing at a node and inspecting values while the game runs |

See [Advanced use](ADVANCED.md) for the detailed instructions and limits of these tools.

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
```

No npm packages are needed. To run the optional browser tests, install Python Playwright and Chromium, then run `python test_browser.py` and `python test_layout_browser.py` and `python test_workspace_browser.py` and `python test_polish_browser.py` and `python test_organise_browser.py` and `python test_game_tools_browser.py` and `python test_playtest_browser.py`, `python test_ui_design_browser.py` and `python test_shared_behaviours_browser.py`. Set `CHROMIUM_PATH` if Chromium is installed outside Playwright's usual location. The recovery test uses real browser storage by default; `BLUEPRINT_MOCK_STORAGE=1` checks its UI with a simulated storage backend on restricted test hosts. Actual storage across a browser restart has not been verified on this branch.

The 720 engine API definitions are based on Nexgen's script reference. The compiler checks data types and graph structure; the native engine compiler checks the resulting AngelScript. Neither check guarantees that all assets are present or that a game performs well on real hardware.

## Current limits

The scene editor is a top-down layout tool, not a fully rendered 3D viewport. Timelines cannot yet be controlled from other graphs. Dispatchers work within one object, and there is no automatic network replication or remote procedure call system. Graph debugging uses Nexgen's existing debugger and currently highlights the active graph only. There is no skeletal blend-tree editor.

Linux headless tests and Chromium tests have been run. Windows, macOS, RXDK builds, graphical rendering and real Xbox gameplay are still unverified for this branch. No rebuilt XBE is included.
