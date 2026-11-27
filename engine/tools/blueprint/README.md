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
- **Move to function** turns a self-contained chain of actions into a new function and leaves one call behind. Include its value nodes; the selection needs one incoming execution wire and at most one outgoing execution wire, with no data wires crossing its boundary. The editor refuses unsafe selections. More complex graphs can be split by creating functions with inputs and returns manually.
- Give a group a name, then click its heading to select its nodes. Double-click the heading to change its name and colour. Remove the group without removing the nodes.
- Assign functions or macros to folders from Layout. Click a sidebar section heading to fold it away. Use **Focus** for a larger canvas.

## Make it look the way you want

**Layout** controls the canvas, grid, node colours, wire colours and styles, text size, node width, panel colours and widths. Choose a light theme or set colours yourself. Hide the grid, category labels, default values or results panel. Enable snap to grid for precise placement.

Select one node to set its display title, header and body colours, title colour and width. Its real function and pins remain the same. Keep custom titles meaningful so other developers can understand the graph. Reset a node's style to use the Blueprint defaults again.

Graph positions, groups, folders and appearance are saved with the `.bp.json` file. Changing those settings does not change the generated gameplay script. Focus and panel visibility are editor controls, not game settings. Layout currently applies to the whole Blueprint, with per-node overrides; it is not a game UI or material editor.

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
```

No npm packages are needed. To run the optional browser tests, install Python Playwright and Chromium, then run `python test_browser.py` and `python test_layout_browser.py`. Set `CHROMIUM_PATH` if Chromium is installed outside Playwright's usual location.

The 720 engine API definitions are based on Nexgen's script reference. The compiler checks data types and graph structure; the native engine compiler checks the resulting AngelScript. Neither check guarantees that all assets are present or that a game performs well on real hardware.

## Current limits

The scene editor is a top-down layout tool, not a fully rendered 3D viewport. Timelines cannot yet be controlled from other graphs. Dispatchers work within one object, and there is no automatic network replication or remote procedure call system. Graph debugging uses Nexgen's existing debugger and currently highlights the active graph only. There is no skeletal blend-tree editor.

Linux headless tests and Chromium tests have been run. Windows, macOS, RXDK builds, graphical rendering and real Xbox gameplay are still unverified for this branch. No rebuilt XBE is included.
