# Nexgen Blueprint Studio v3

**Upstream base:** Nexgen Redux `54bc84ce91be85d4c7e29b26f4d748547477266e` (27 November 2026). The four upstream D3D8/video changes are preserved. v3 adds interface messages, typed multi-track timelines, state machines and reusable entity hierarchies. Existing v1 graph files remain supported.

An offline, desktop visual scripting tool for Nexgen Redux. It is inspired by the event graphs, execution wires, typed data pins, variables, functions and reusable components of Unreal Engine Blueprints. It uses Nexgen's existing AngelScript runtime, rather than introducing a second interpreter or putting a visual editor on the Xbox.

## Open the editor

Open `index.html` in a modern desktop browser. No web server, npm install, account, network connection or engine build is needed. `node bundle.js NexgenBlueprintStudio.html` makes a portable single-file version of the same editor (Node.js is only needed to generate the bundle).

The Arcade example opens automatically. The **Examples** menu includes fourteen editable examples, including Interfaces, TimelineTracks, AnimationMachine, ModelAnimation and EntityHierarchies. The Arcade example creates a blue entity, rotates it by delta time, and increments a score when the player presses Accept. It has separate On Enter, Update, Draw and AddScore function graphs.

- **New**: choose Screen (a game screen) or Behaviour (a reusable entity script).
- **My Blueprint**: add lifecycle event graphs, typed variables and functions. Void functions can serve as custom events called by other graphs.
- **Node library**: search by node name, engine category, description or type. Click or drag a node into the graph. Right-click empty canvas to open the context menu.
- **Connect**: drag from an output to an input, or click both pins. Drag onto empty space to find compatible nodes. White pins are execution; coloured pins carry typed values. Right-click a wire or pin to disconnect. The Details panel edits unconnected data defaults.
- **Navigate**: drag empty space/middle mouse or hold Space to pan, scroll to zoom, and use Fit graph to frame the graph. Delete removes a selected node or wire. Ctrl+Z / Ctrl+Shift+Z undo and redo. Ctrl+S saves the editable `.bp.json` graph.
- **Compile**: validates the graph and generates readable AngelScript with node-ID comments. Click a diagnostic to locate the offending graph/node. Generated source can be inspected and copied. Export .as downloads the runtime script.
- **Behaviour components**: in a Screen graph, use the plus button beside Behaviour Components to import another saved Behaviour `.bp.json`. Use its `Attach BP_...` node with an Entity reference. Reimport the same class name to update its embedded graph. Each attached behaviour has its own variables and state. An imported graph is a copy, not a live reference to a file. Edit the component in its own Blueprint document and reimport it to update.

## Projects and packaging

**Project** collects multiple screens, their editable graph sources, screen IDs/titles, and per-screen asset files. Add existing Screen graphs, create new screens, or import a saved `.bp.project.json` project. Switching a project screen retains its graph; save the entire project to keep all source edits. Opening a standalone graph detaches it from the active project screen. The browser asset picker imports files into the current screen's `asset/` directory (nested paths can be specified in the project JSON). Project asset limits are 64 MiB and 4096 files; these are editor limits, not console memory guarantees. No external dependencies are used for ZIP packaging.

**Package** compiles all screens first and generates a ZIP with `media/showcase/<id>/main.as`, `screen.json`, `asset/`, and a separate `blueprints/` source folder. Copy only the `media/` contents into an existing engine release. This is a game-content package, not a rebuilt XBE or a full engine executable. Debug packages add trace statements and source maps; release packages omit both.

Use the supplied `examples/showcase.bp.project.json` for a multi-screen project with nine example screens, or use the prepackaged ZIP. The example screens demonstrate real script operations, but not all are visually rendered games: several deliberately use saved values to verify logic in native tests.

## Run your Blueprint in Nexgen

The editor itself is not a game engine or emulator. It compiles a screen to the normal `main.as` that Nexgen already understands. Most generated scripts run with an existing Nexgen binary. **Interface Message nodes require the v3 engine binding `Entity.behaviourAt(uint)`**, so rebuild the engine from this source before running those graphs. Other v3 features use existing APIs.

With an existing Nexgen release, create `media/showcase/blueprint_arcade/`, export the Arcade screen's `main.as` into it, then add `screen.json` containing:

```json
{
  "title": "Blueprint Arcade",
  "description": "Created with Nexgen Blueprint Studio",
  "category": "Blueprints",
  "order": 1000
}
```

Run Nexgen with `--screen blueprint_arcade` (or open the screen from its showcase menu). `--dev` enables the existing script reload mechanism. The `main.as` file is ordinary AngelScript, so the existing VS Code script debugger can set breakpoints and inspect generated code. Save the `.bp.json` separately; a handwritten `.as` file cannot be imported back into a graph. On original Xbox, copy the generated screen directory to the release's media folder (or package it with the regular RXDK build). The editor, browser and Node.js are never required on the console. Use a rebuilt v3 engine executable for interface-message content.

A standalone Behaviour exports a class that can be included in a normal screen, for example with `#include "asset:BP_Spinner.as"` when the file is in that screen's `asset/` directory. Attach it to an entity with `entity.add(BP_Spinner());`. Alternatively import its graph into a Screen Blueprint; the generated `main.as` then includes both classes and does not need a separate include file.

## Command line and automation

Node.js 18 or newer, no dependencies. From `engine/tools/blueprint`:

```sh
node cli.js check examples/arcade.bp.json
node cli.js compile examples/spinner.bp.json --out BP_Spinner.as
node cli.js screen examples/arcade.bp.json --out /path/to/media/showcase/blueprint_arcade
node cli.js project examples/showcase.bp.project.json --out BlueprintShowcase.zip
node cli.js screen examples/gameplay.bp.json --out /path/to/media/showcase/gameplay --debug
node bundle.js NexgenBlueprintStudio.html
```

The CLI refuses to overwrite scripts unless `--force` is specified. Screen export creates `screen.json` only if absent; existing metadata is preserved. Do not hand-edit generated source if you intend to regenerate it. All asset paths and script APIs follow the engine's script reference. Save graph JSON in version control alongside (or outside) generated runtime files.

## Blueprint interfaces, timelines, state machines and entity hierarchies

### Blueprint interfaces

Use **My Blueprint → Blueprint Interfaces → +** to define a named contract. The method syntax in the creation form is `ApplyDamage(amount:int):int; Interact():void`. Choose **Reference only** to make message nodes available, or **Implement here** to create matching function graphs. Existing signature-compatible functions are reused. Implementations are ordinary AngelScript interface methods, not a dynamic interpreter. Export a `.bp.interface.json` file and import it into another class to reuse the same contract. A Screen also sees interfaces declared by embedded Behaviour components.

Search for the interface name in the node library, wire an Entity reference into Target and execute the message. It searches the target entity’s attached behaviours in attachment order, calls the first implementation, and fires **Then** with the typed result. **Missing** fires for a null/dead target or an entity without a matching behaviour. The result is valid only along Then. This is a targeted synchronous message, not a multicast or a network RPC; no external receiver is called if no match exists. A new native `IBehaviour@ Entity.behaviourAt(uint index)` accessor supports this lookup. Scripts which use interface messages need the v3 executable. For best performance on Xbox, keep the number of behaviours per entity small; indexed lookup walks the native behaviour list.

### Multi-track timelines

Select a Timeline node and add named float, Vec2, Vec3 or Color tracks in Details. Each channel has 2–32 normalized keyframes with linear, smoothstep or step interpolation. Each added track creates a typed output pin on the timeline. The original Value output still uses its from/to parameters and original curve. Additional track values are absolute, not scaled by from/to. Playback can be forward, reverse, loop, or ping-pong, with excess time reduced mathematically on a large tick. A nonpositive duration completes on the first tick; looping and ping-pong never fire Finished while duration is positive. Playback advances only on engine Update. There are no cross-graph Play/Stop/Seek/Set Playback Position commands yet.

### Visual state machines

In an Update graph, search for **State Machine**, **Sprite Animation State Machine**, or **Model Animation State Machine**. Wire event dt, configure the initial state, add states, and create named transitions between them. Each transition adds a Boolean input pin. The first eligible transition in the displayed priority order wins; at most one occurs per invocation. `*` transitions can start in any state except their own destination. The first invocation and a true Reset input enter the initial state without evaluating transitions. Entered is true on entry, Elapsed counts time since entry, and ActiveState is the state’s name. Each state has its own execution output; Then runs after the selected state output. The editor shows the state diagram and allows states, transitions and priority to be edited.

Sprite states configure first/last frame, FPS and looping, using Entity.play. Model states configure clip name, blend duration, speed and looping, using Entity.animate. Attach the target Entity reference and set up its sprite sheet or model normally. Sprite clips are issued once per state/target change, so a finished non-looping clip stays finished. Model animation requests retry until the asset/clip becomes available. Wire the engine’s Played/Animated properties into a transition to exit on completion. State reset does not forcibly rewind an identical engine animation already in progress. This is a state-based clip controller, not a skeletal pose-blend graph, retargeting system, or asset animation editor.

### Entity hierarchy templates

A Screen’s **Entity Hierarchies** section defines reusable 2D or 3D trees (one root, up to 32 entities, up to 16 templates). Add children, edit local transforms, choose a rectangle/disc/image or 3D primitive/model, and attach already imported Behaviour classes. The schematic previews projected transform positions and parent links; it is not a live textured/model viewport. Export/import `.bp.hierarchy.json` templates; import their referenced Behaviour assets separately first. The Spawn node accepts a world origin and emits Root and a typed Entity reference for every named child. Store needed handles in variables for later event graphs. Each execution creates new entities, applies parent transforms, and attaches behaviours after the tree exists. Names are reused on repeated spawns, so retain the explicit output handles instead of relying on global name lookup. A null spawn triggers the Failed branch and best-effort cleanup; native script errors (such as exhausted world capacity) can abort execution before that branch. There is no transactional allocator.

2D and 3D transforms and native behaviour startup are covered by the native tests. Textured/model asset visuals require the appropriate files in the project asset folder, and loading/drawing those assets on console remains to be tested.

## Inheritance, macros, pure functions and dispatchers

- **Parent Blueprint** imports a same-kind Screen/Behaviour graph. Inherited variables and functions are available in the node library. Click an inherited event or function to create a signature-compatible override with a Call Parent node. The compiler uses actual AngelScript inheritance, marking embedded base classes abstract so the engine selects the concrete Screen. Parent and component assets are embedded snapshots, not live links. Update them by reimporting the saved source. Parent latent jobs are advanced by the concrete update even when its Update event does not call the parent's event graph.
- **Macros** have typed inputs, one to eight named execution exits, and (for a single execution exit) typed result pins. They expand at compile time with per-call-site state; no runtime macro interpreter exists. Export/import `.bplib.json` libraries to share macros. References to class variables/functions must exist in the importing class. Macro results that are pure expressions are evaluated when consumed; stateful outputs retain ordinary execution-path availability checks. Recursive macro graphs and cycles are rejected. Macro-internal generated nodes have generated IDs; the debugger stops and highlights the macro invocation anchor, not a separate expanded macro editor.
- **Pure functions** have a non-void return and no side-effecting execution nodes other than Return. Calls expose data pins without execution wires and are evaluated on consumption. Functions can also declare typed local variables, allocated per invocation.
- **Event Dispatchers** have typed argument signatures. Create a matching void handler, bind/unbind it, broadcast, or unbind all. Each object holds its own membership; broadcasts snapshot membership and arguments before calling handlers. Handlers run in graph declaration order, not bind order. This implementation supports local-instance handlers, not arbitrary cross-object delegates or dynamically bound external callbacks. Recursive calls through dispatchers are rejected conservatively.
- **Timeline** plays curves from an event graph. Set duration, from/to and smoothing. Update fires on ticks; Finished fires at completion for one-shot playback. Alpha is normalized playhead time. Repeated starts while active are ignored. Delay/timeline state is per object and discarded at teardown. See the multi-track timeline guide above.

## Native compilation and visual debugging

A standalone HTML editor needs no external runtime. The optional local Node.js bridge supports native validation and the engine's existing TCP debugger. Node.js 18+ is needed only on the development computer. Build the `nx_blueprint_validate` target or use the binary included in desktop builds that package this tool. Start Studio from the tool directory:

```sh
node debug_bridge.js 127.0.0.1 4711 8765 /absolute/path/to/nx_blueprint_validate
```

On Windows the last argument is the path to `nx_blueprint_validate.exe`. Open `http://127.0.0.1:8765/` in your browser. **Debug → Native compile** sends the generated source to the validator, which compiles against Nexgen's actual registered bindings and does not execute Screen lifecycle methods. It reports native compiler errors. Native validation does not verify assets, rendering or gameplay.

To debug a graph:

1. Open the desired Screen graph in Studio, select execution nodes, and choose **Set breakpoint** in Details.
2. In the Debug tab choose **Export debug .as** and install that exact `main.as` into the target screen directory. Alternatively export a debug project package. Use the exact exported version; line mappings depend on it.
3. Run desktop Nexgen with `--screen gameplay --debug 4711 --debug-wait`, adjusting the screen ID. For a networked original Xbox, follow Nexgen's existing remote debugger instructions in `engine/tools/vscode/README.md` (put a `debug` folder beside the title or use `App::debugger()`) and start the bridge with the console's IP instead of 127.0.0.1.
4. Choose **Attach**, and set the media-relative script path, for example `showcase/gameplay/main.as`. The bridge installs breakpoints before sending Ready. Pauses and exact execution node breakpoints highlight the graph node. Native statement stepping, call stack, expandable locals, and watch expressions such as `this.total` work through Nexgen's debugger. Running trace highlights show the most recent observed execution anchor; not every frame is visually displayed. When a stop occurs in an embedded parent/component, its class and source line are shown; the editor currently highlights the active graph only.
5. Detach before changing/re-exporting code. Release builds never include trace statements. Debug tracing can be expensive on an original Xbox, especially at high frame rates, and should be used only during development.

The bridge binds its web server only to 127.0.0.1, restricts requests to its own origin and a random session token, and connects only to the explicit target supplied at startup. The native engine debugger itself is not authenticated; use only a trusted local network, never expose its port to the internet. Closing the bridge disconnects the engine; it also disconnects inactive sessions after 90 seconds. No Node/browser dependency is added to console gameplay.

## What is implemented

The generated API catalogue has 720 overload-specific definitions in this snapshot. The same catalogue and compiler are used by the editor and CLI. It covers typed calls/properties for Input, Scene, Entity, Space, Canvas, UI, Audio, Assets, Save, App, JSON, networking, paths, math and related supported types. Calls with output reference parameters expose output pins (for example, Space raycast's hit position and normal). The catalogue is derived from `docs/ScriptReference.md`, not an independently duplicated list of engine functions. Every imported signature is compiled by the native integration test, without executing side-effecting APIs. Unsupported declarations are skipped rather than exposed as broken nodes.

| Feature | Behaviour |
|---|---|
| Screen events | onEnter, update (dt), draw (Canvas), onSuspend, onResume, onExit |
| Behaviour events | start, update (dt), onHit and onLeave (other Entity), onGone |
| Variables | Per class instance; primitives, vectors, colors, enums, handles, arrays and supported value objects |
| Functions | Typed parameters/return, pure calls, local variables, void functions as custom events |
| Inheritance | Same-kind abstract parents, inherited members, event/function overrides and explicit parent calls |
| Macros | Compile-time expansion, reusable libraries, multi-exec exits and single-exit typed outputs |
| Dispatchers | Typed instance-local multicast bind/unbind/broadcast |
| Execution | Branch, Sequence, For/For Each/While (bounded), break/continue, typed Switch, Gate, Do N, Do Once, Flip Flop |
| Latent execution | Delay and multi-track Timeline (float, Vec2, Vec3, Color; four playback modes); per-instance state |
| Interfaces | Typed contracts, native entity Behaviour lookup, safe missing-receiver branch |
| State machines | Named states, priority transitions, per-state outputs, sprite/model clip controllers |
| Entity hierarchies | Reusable 2D/3D parent trees with transform editing, primitive/asset visuals and imported behaviours |
| Data | Literals, math, comparisons, Boolean logic, Select, conversions, vector/color construction and component access, object validity |
| Arrays | Construction, length, index, append, set and remove; native array-returning functions |
| Components | Import reusable Behaviour graphs, instantiate and attach to a Screen's entity |
| Editor | Context-filtered node picker, typed wires, drag/pan/zoom, comments/reroute, details, undo/redo, save/load and source preview |
| Debugging | Optional local bridge, native compile, node breakpoints and trace highlighting, native stepping, stack, locals, watches |
| Projects | Multi-screen source management, per-screen assets, release/debug content ZIP exports and CLI |

## Compiler contract and constraints

A graph document is versioned JSON: `{version:1, name, kind, variables, graphs, components?, parent?, dispatchers?, interfaces?, hierarchies?}`. `kind` is `Screen` or `Behaviour`. Each graph has a unique name, `kind` (`event`, `function` or `macro`), optional typed `params` and `returns` for functions, `nodes`, and `links` between named pins. A node has a stable ID, kind, position, optional type/ref/defaults; links contain `{from:{node,pin}, to:{node,pin}}`. Node ID comments in generated source help correlate logs with nodes. Generated output is deterministic for the same graph.

- Execution and data pins must match exactly. Explicit conversion nodes are available for numeric types. Pure nodes evaluate when their output is consumed; impure output values are captured in locals, available only after the node executes on that path. Store values in instance variables to carry them between graphs, branch paths, loop iterations or Delay continuations.
- Delay and Timeline are available on appropriate lifecycle event graphs (including expanded macros), not functions, Draw or teardown events. Delays advance only when Nexgen calls update, so pauses/suspension pause them. State is discarded on screen/behaviour destruction. Event arguments and earlier impure outputs cannot cross a Delay directly in this version; save needed values into variables first.
- Exec cycles, pure data cycles, recursive function graphs, invalid targets/pins, incompatible types and missing non-void return paths are rejected. Disconnected nodes warn; they do not execute. The For, For Each and While loops are capped at 10,000 iterations, array construction at 65,536 items, and Nexgen's existing statement/exception limits still apply. Do not allocate large arrays or issue expensive work per frame on a memory-constrained console.
- Engine-dependent restrictions (for example, Canvas calls only during drawing, required assets, supported GPU features or null/stale references) still apply. `Compile` in Studio checks the graph and emits source; only Nexgen's native AngelScript compiler proves a particular project's code and engine bindings. All catalogue signatures in this snapshot have been tested against that compiler.
- The system does not yet offer full Unreal parity: no automatic property replication/RPCs, cross-instance multicast delegates, full interactive scene/level viewport, skeletal pose-blend graphs, timeline seek/stop controls, or multi-object graph debugging. Engine networking, animation, asset and scene APIs remain available through ordinary typed nodes. This is a working visual scripting and content packaging system, not a drop-in Unreal replacement.

## Maintenance and tests

From the repo root, regenerate the catalogue and committed native fixtures when the script reference changes:

```sh
python3 engine/tools/blueprint/generate_catalog.py
node engine/tools/blueprint/make_examples.js
node engine/tools/blueprint/test_compiler.js
```

Normal desktop CMake/CTest builds now include `blueprint_runtime`. For a source-only archive with no media submodule or window SDK, the following builds only the focused null-GPU test executable on a Linux host:

```sh
cmake -S engine -B build/blueprint -DNX_HEADLESS_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/blueprint --target nx_blueprint_tests nx_blueprint_validate nx_blueprint_debug_fixture --parallel
ctest --test-dir build/blueprint -R 'blueprint_|self_contained' --output-on-failure
```

The engine tests cover normal screen management, collision lifecycle, per-instance state, all 720 API signatures, delays, inherited latent state, dispatchers, macros, pure functions, locals, loops, switch, timeline curves, interface dispatch across components, native 2D sprite state changes, model-animation script compilation, and 2D/3D entity parenting. `test_compiler.js` covers deterministic output, validation and project ZIPs. `test_debug_bridge.js` starts a real native fixture and verifies TCP attach, exact node breakpoints, locals/watch values, stepping and resume. The debugger test target is enabled if Node.js is installed; the native compiler/runtime themselves do not depend on Node. The source archive used for this work lacks the media submodule, so the full showcase tour and platform-specific builds are outside the verified test scope.

## Compatibility and release verification

The original Xbox RXDK, Windows and macOS configurations remain in place. v3 was compiled and exercised on the Linux null-GPU test target, and the browser suite uses Chromium. The upstream archive does not contain its media/docs submodule contents. Model animation playback, graphical rendering, real original Xbox hardware performance and platform-specific compilation have not been verified. The included source packages do not contain a rebuilt XBE.
