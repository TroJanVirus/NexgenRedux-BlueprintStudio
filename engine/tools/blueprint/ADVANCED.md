# Advanced Blueprint use

## Functions and shared logic

Functions can take typed inputs, return one value and have local variables. A value-only (pure) function cannot contain action nodes except Return; its result is calculated when used. Ordinary functions and event graphs can call other functions. Void functions can act as custom events.

A macro expands into the calling graph when compiled. Macros can have several execution exits or, with one exit, typed result pins. Use Import/Export library to share them. Variables or functions referenced by a macro must also exist in the new Blueprint. Macros and functions cannot recurse.

Import a parent of the same Blueprint kind to inherit its members. Select an inherited graph to override it. Use Call Parent to run the original implementation. The parent is embedded in the saved graph; reimport after changes.

## Interfaces and dispatchers

Create an interface under My Blueprint. Its methods are named signatures such as `ApplyDamage(amount:int):int; Interact():void`. Choose Reference only to send messages, or Implement here to make matching function graphs. Export/import interface files to share signatures. A Screen also sees interfaces from imported Behaviours.

Connect an Entity to the interface message's Target. The first attached Behaviour that implements the interface receives the call. Then runs if a receiver is found; Missing runs for an absent receiver or dead entity. Use the result only along Then. This uses the new `Entity.behaviourAt(uint)` binding, so rebuild the engine first. It is a direct message, not a network call.

Dispatchers bind matching void handlers, broadcast arguments, or unbind handlers. Handlers belong to the same Blueprint instance and run in graph declaration order. Membership and arguments are copied before the broadcast. External listeners are not supported.

## Timelines and animation

A Timeline advances during Update. Set duration, from/to and a curve. Update fires while playing; Finished fires when a one-shot timeline completes. Repeated starts while active are ignored. Add float, Vec2, Vec3 or Color tracks in Details; each track has its own output pin and two to 32 keys per channel. The extra tracks contain absolute values. Playback can run forward, reverse, loop or ping-pong. A looping timeline does not finish until stopped by teardown; a nonpositive duration finishes on the first tick. There are no separate Play/Stop/Seek nodes yet.

In an Update graph, add a State Machine, Sprite Animation State Machine or Model Animation State Machine. Connect delta time, choose an initial state, add states and make transitions. Each transition adds a Boolean input. The first true transition wins, at most once per call. A `*` transition can run from any other state. Reset enters the initial state without evaluating transitions; Entered and Elapsed describe the current state.

For sprites, set first/last frame, FPS and looping. For models, set the clip name, blend time, speed and looping. Set up the entity's sprite sheet or model first. Sprite clips are issued when the state or target changes, so a finished one-shot does not restart each tick. Models retry if a clip or asset is not ready. Reset does not forcibly rewind an identical running animation. Use the engine's Played/Animated properties for completion transitions. This is a clip controller, not a pose-blend or skeletal animation editor.

## Entities and scenes

Under Entity Hierarchies, create a 2D or 3D tree with one root and up to 32 entities. Set local transforms, pick a primitive or asset, and attach imported Behaviour classes. Export/import templates; import referenced Behaviours first. The Spawn node accepts an origin and outputs the root and named child references. Store those references if you need them later, rather than relying on repeated names. Spawning can fail; the Failed output attempts cleanup, but native errors may abort execution before it runs.

The Scenes button opens a top-down layout for the current Screen in a project. Create or import Nexgen scene JSON, drag entity markers, or set coordinates in Details. The 3D view uses X/Z and preserves Y height. Advanced JSON lets you edit fields the visual tools do not yet expose. Existing top-level fields, prefabs and groups are preserved. Choose the correct scene mode; switching mode does not convert the file. Save to project or Save + add load node to place the native Scene/Space load node with the asset path filled in. Connect it to an event such as onEnter. The preview is not rendered by Nexgen, and the editor does not validate every native JSON property.

## Projects and command line

Project files hold screens, their graphs and per-screen assets. Save the entire project when working across screens. Opening a standalone graph detaches the current document from its project. The project has a 64 MiB/4096-file asset limit; this does not mean the assets fit Xbox RAM. Release packages contain `media/showcase/<id>/main.as`, `screen.json`, assets and a separate `blueprints/` source directory. Debug packages add trace statements and source maps.

From this directory:

```sh
node cli.js compile examples/spinner.bp.json --out BP_Spinner.as
node cli.js screen examples/gameplay.bp.json --out /path/to/media/showcase/gameplay --debug
node cli.js project examples/showcase.bp.project.json --out BlueprintShowcase.zip
```

CLI script export refuses to overwrite existing output unless `--force` is supplied. Existing screen metadata is kept. Save graph JSON in version control; do not edit generated scripts if you intend to regenerate them. A standalone Behaviour can also be included from a handwritten screen with `#include "asset:BP_Spinner.as"` and `entity.add(BP_Spinner());`.

## Native compilation and debugging

Build the desktop `nx_blueprint_validate` target. Run the local bridge with Node.js:

```sh
node debug_bridge.js 127.0.0.1 4711 8765 /absolute/path/to/nx_blueprint_validate
```

On Windows the validator ends in `.exe`. Open `http://127.0.0.1:8765/`. Debug → Native compile checks the generated code against the real engine bindings without running the game.

1. In Details, set breakpoints on execution nodes.
2. Export a debug script or debug project and install the exact exported files in Nexgen.
3. Run `nexgen --screen gameplay --debug 4711 --debug-wait`, substituting your screen ID. For a networked Xbox, use Nexgen's existing debugger setup in `engine/tools/vscode/README.md`, then start the bridge with the console IP instead of 127.0.0.1.
4. Click Attach and enter the media-relative script path, such as `showcase/gameplay/main.as`. Pause, step, inspect locals, and watch expressions such as `this.score`.
5. Detach before changing or exporting code again. Source-line mappings depend on the exact script. Stops in embedded components show the class and line; only the active graph is highlighted.

The bridge listens only on the local computer and connects to the selected target. The engine debugger is not authenticated. Use a trusted local network and never expose its port to the internet. The bridge disconnects inactive sessions after 90 seconds. Debug tracing can be expensive on Xbox; release packages omit it.

## Compiler rules

Execution and data pins must match. Use an explicit conversion for numeric types. Values from action nodes are available only after the action runs along that path. Store values in instance variables to reuse them across events or delays. Delay/Timeline work in appropriate lifecycle events, not functions, drawing or teardown. They advance during Update, and state disappears when the object is destroyed. Event arguments and earlier action outputs cannot cross a Delay directly.

Cycles, invalid pins, recursion and missing returns are errors. Disconnected actions do not execute. Loops are capped at 10,000 iterations and array creation at 65,536 items. Nexgen's own statement and memory limits still apply. Be mindful of the original Xbox's 64 MiB RAM. Rendering calls, required assets, GPU capabilities and live entity references are still governed by Nexgen.

## Maintaining the tool

Regenerate the node list and native fixtures from the repository root when engine APIs change:

```sh
python3 engine/tools/blueprint/generate_catalog.py
node engine/tools/blueprint/make_examples.js
node engine/tools/blueprint/test_compiler.js
```

For a focused headless Linux build:

```sh
cmake -S engine -B build/blueprint -DNX_HEADLESS_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/blueprint --target nx_blueprint_tests nx_blueprint_validate nx_blueprint_debug_fixture --parallel
ctest --test-dir build/blueprint -R 'blueprint_|self_contained' --output-on-failure
```

The native tests exercise the engine bindings, lifecycle, collision, delays, functions, interfaces, timelines, state machines, parenting and the TCP debugger. The API checks compile signatures; they do not execute every API call. Model animation is compiled but has not been tested with loaded skeletal assets. The media and documentation submodules were not present in the supplied source archive. Console performance and platform builds remain to be verified.
