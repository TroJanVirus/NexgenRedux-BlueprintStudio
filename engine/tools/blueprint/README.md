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

**Screen** blueprints control menus, levels and other screens. **Behaviour** blueprints contain reusable entity logic, such as movement, damage or pickups. Import a Behaviour into a Screen and use its Attach node to add it to an entity. Each entity has its own copy of the behaviour's variables. Imported components and parents are saved copies; reimport them to pick up edits.

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

Graph positions, groups, folders and appearance are saved with the `.bp.json` file. Changing those settings does not change the generated gameplay script. Focus and panel visibility are editor controls, not game settings. Layout currently applies to the whole Blueprint, with per-node overrides; it is not a game UI or material editor.

## Editing and recovery

- Select a variable or a local variable and choose **Rename** in Details. To rename a function or macro, open its graph and select its entry node. Calls, reads, writes and local event handlers follow the new name. Parent overrides and interface methods must keep their original names.
- Use the star beside a library entry to keep it in Favourites. Bookmark important nodes in Details and jump to them from **Bookmarks**. Bookmarks are saved in the graph, while favourites belong to this browser.
- Click a wire and use **Add bend** in Details. Drag the blue handles to route it around other nodes. The bends move with the output pin. Reset the route to return to the selected wire style. Bends are saved and can be copied with a group of nodes.
- **Save project** appears in place of Save graph when a project screen is open. Ctrl+S then saves all screens and assets. To save just one graph, choose **Export current graph** in the Project window.
- **Recovery** keeps a local copy of unsaved graphs and projects (including assets), checking every two seconds. Open the Recovery menu to restore, export or delete a copy. A check mark means the last copy was saved; an exclamation mark means storage is unavailable or a write failed. Copies are local to the browser and address you opened Studio from. Clearing browser data can remove them. Keep regular graph or project files as your main backup.

## Run a game

Use **Project** to collect screens and their assets. **Package** exports a ZIP containing the scripts and media folders. Copy the `media/` contents into an existing Nexgen release. The package is game content, not an engine executable or XBE. Save the project separately if you want to edit it later.

For a quick test of the Arcade example, export the script as `media/showcase/blueprint_arcade/main.as` in a Nexgen release. Add a `screen.json` beside it:

```json
{"title":"Blueprint Arcade","description":"Arcade example","category":"Blueprints","order":1000}
```

Run `nexgen --screen blueprint_arcade`. Add `--dev` to use Nexgen's script reload support. On Xbox, copy the screen folder into the same media location. Keep the editable graph on your computer. An existing `.as` script cannot be converted back into a graph automatically.

Most scripts work with the existing engine. Interface messages use the new `Entity.behaviourAt(uint)` binding; rebuild Nexgen from this branch before running them.

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
```

No npm packages are needed. To run the optional browser tests, install Python Playwright and Chromium, then run `python test_browser.py` and `python test_layout_browser.py` and `python test_workspace_browser.py` and `python test_polish_browser.py` and `python test_organise_browser.py`. Set `CHROMIUM_PATH` if Chromium is installed outside Playwright's usual location. The recovery test uses real browser storage by default; `BLUEPRINT_MOCK_STORAGE=1` checks its UI with a simulated storage backend on restricted test hosts. Actual storage across a browser restart has not been verified on this branch.

The 720 engine API definitions are based on Nexgen's script reference. The compiler checks data types and graph structure; the native engine compiler checks the resulting AngelScript. Neither check guarantees that all assets are present or that a game performs well on real hardware.

## Current limits

The scene editor is a top-down layout tool, not a fully rendered 3D viewport. Timelines cannot yet be controlled from other graphs. Dispatchers work within one object, and there is no automatic network replication or remote procedure call system. Graph debugging uses Nexgen's existing debugger and currently highlights the active graph only. There is no skeletal blend-tree editor.

Linux headless tests and Chromium tests have been run. Windows, macOS, RXDK builds, graphical rendering and real Xbox gameplay are still unverified for this branch. No rebuilt XBE is included.
