# Post-Beta 2 native Blueprint Studio roadmap

Keep Beta 2 release source frozen. Use native Redux APIs, formats and generated AngelScript; no browser dependency or second runtime. Validate save/reopen, preview/export, regression tests and target hardware at each milestone.

| Phase | Deliverable | Acceptance |
|---|---|---|
| 1 | Dockable workspace, world outliner, asset folders/thumbnails, scene selection, transform gizmos, simulation | Author and edit a player and enemies; save/reopen and undo. First slice: stable-ID linked actor outliner and root instance inspector. |
| 2 | Class inheritance, defaults, exposed fields/on-spawn pins, nested actors, construction graphs, typed component references | Parent changes propagate except overrides; independent instance state and teardown. |
| 3 | Interfaces, dispatchers, custom events, delays/timers, gates, switch/foreach, enums, structures, maps/sets, libraries, async | Compare generated AngelScript behavior to native script fixtures; cancel pending actions on destruction. Audit legacy implementation first. |
| 4 | Optional gameplay/player/AI templates, input contexts, remapping and analog processing | Third-person control and menus on keyboard/gamepad. |
| 5 | Skeletal state machines, blend spaces, transitions, notifications, layers, IK, sockets, root motion | Native preview and movement/attack blend, notifications once per crossing. |
| 6 | Widget inheritance, per-instance properties, reactive binding, named/eased timelines, list templates, drag/drop, focus, safe zones, localization, world-space widgets | HUD, inventory, menu, settings; controller and 4:3/16:9 tests. |
| 7 | Instance debugger, stepping, conditions, native profiling, live inspection, remote debugging | Pause selected enemy, inspect health, step/resume; no debug overhead in release. |
| 8 | Bidirectional conversion, mixed code, dependencies, custom API discovery, diff/refactoring | Import existing project transactionally; retain/diagnose unsupported code; compile and compare behavior. |
| 9 | Platform budgets, native Cook, package/deploy and capability warnings | Desktop and OG Xbox runtime packages; separate emulator and hardware results. |
| 10 | AI behavior trees, blackboards, navigation/perception, tasks/debug | Patrol/chase/attack using actual Redux AI helpers. |
| 11 | Material, particle and audio designers | Use native APIs and expose platform limits. |
| 12 | Multiplayer authoring | Native Room/Message/shared entities; ownership and disconnect tests. |

Final acceptance: from blank, author third-person player and camera, animated movement/combat, enemies, interactables, inventory, menus/HUD, multiple levels and persistent saves. Run/export native desktop and OG Xbox-compatible runtime. Record actual console tests separately. No claim of Unreal parity until acceptance passes.

## Class inheritance first slice

Create child class from a selected actor. Child assets store only root overrides and a parent dependency, resolving the full inherited component hierarchy before native actor compilation and preview. Root edits are independent; reset restores inherited defaults. Inherited non-root components are read-only and child-specific component additions, typed field inheritance, function overrides and construction graphs are still outstanding. This first slice does not add an AngelScript runtime class hierarchy.

## Explicit camera selection

Activate actor camera is a typed execution node selecting a named camera within the supplied live Entity hierarchy. Duplicate names within one actor, missing cameras and ambiguous empty names return false without changing the view. Existing automatic camera activation behavior is retained for Beta 2 project compatibility; explicit selection can override it. Camera blending and automatic player ownership remain outstanding.

## Inherited component overrides and additions

Derived classes can edit inherited components and append class-specific children. Authoring references use declaring asset ID and append-only component ordinal; flattened indices are regenerated when resolving the hierarchy, so a parent adding components does not redirect existing child attachments. Root reset leaves component overrides and additions intact. No runtime class metadata is introduced. Component removal/reparenting and individual override resets still need stable-ID-aware editing.

Selected-component reset now also clears non-root override records independently, without removing appended components or unrelated overrides. For a locally added component the reset restores its original authored definition.

## Native component references and flow controls

Get actor component searches only the given live hierarchy and returns null for missing or ambiguous names; Is valid entity safely checks a handle before use. Do Once and Do N track counters per screen/Behaviour instance and per graph node. Their body runs when permitted; Then always continues. A true reset clears the counter before evaluating the current execution. Gate (condition) executes its body only when Open is true. These generate ordinary AngelScript; stateful nodes are rejected in inline macros rather than accidentally sharing call-site state. Multi-entry Unreal-style gates and latent continuations remain future work.

## Consolidated verification

Regression coverage includes generated-script spawning of derived actor hierarchies with parent edits, component overrides and independent instances; graph-level valid/missing/destroyed handles and condition gates; persistent and resettable Do Once/Do N counters on two independent native Behaviours; and rejection of parent edits that would exceed a descendant's component budget. Component lookup traverses only the native actor subtree without allocation or recursion. These tests are part of the native CI suite; confirm the exact revision's jobs before marking this phase verified.

## Actor class scripting and viewport transforms (integration candidate)

Class graphs create native Behaviour inheritance alongside the actor hierarchy. Typed primitive declarations can be inherited and given child defaults; exposed primitive fields add typed spawn pins, and linked spawns assign connected inputs before calling the optional void Construction graph. Child functions created with an inherited name preserve the parent signature. Native Behaviour lifecycle callbacks are inherited when child event graphs are empty. The 3D placement viewport has projected XYZ handles, with move/rotate/scale modes and a single undoable edit on release. This candidate requires native runtime and UI verification; construction is runtime spawn-time, not an editor-time script sandbox, and the rotation/scale handles use screen-projected axis drags rather than Unreal-style rings.

Class variable editor now displays inherited fields and defaults, toggles spawn exposure for declared primitive fields, and labels unconnected spawn pins with class defaults. Overrides have a typed Call parent function node; virtual dispatch cycles and linked spawns inside construction call chains are rejected. Resetting component overrides preserves the class's root graph binding. The original verified branch and Beta 2 remain untouched.

## Advanced scripting first slice (CI pending)

Native Flip Flop alternates A/B per instance, with reset choosing A. Sequential MultiGate has four fixed outputs, optional loop/reset, and Then continues even after exhaustion. Controlled Gate starts closed and has separate Open, Close, Toggle and Enter execution inputs; only Enter can trigger Body. Switch on int has four configurable case inputs, first matching branch wins, with Default and Then. Typed For Each supports the seven existing array element types, captures initial length, bounds iterations to 10,000, and checks current length to avoid invalid access after shrinking. Values are scoped to Body. Gate controls and other persistent state cannot be used in inline macros. Cross-graph control pins, random/dynamic MultiGate outputs, custom events, dispatchers, latent scheduling, collection types beyond arrays, project-wide libraries and class casts remain outstanding or require further audit. No browser runtime is used.

## Deferred execution first slice (CI pending)

Screen On Enter and Update graphs support Delay (ignore while pending), Retriggerable Delay (restart), and Set Timer (single/repeating with immediate Then), each with a separate Cancel execution pin. Deferred Completed paths run from the native screen update lifecycle using per-instance state, not a blocking call or a second runtime. Timer callbacks fire at most once per frame; no catch-up bursts. Scope is deliberately limited to Screen lifecycle graphs; actors, cross-graph cancellation, general async results, event dispatchers, and coroutine/delegate lifecycle integration remain future work. Continuations must read persistent fields or fresh expressions, not locals from the original event call. Added runtime tests for delay completion, cancellation and one-shot timers; flow assertions sample the fifteenth actual actor update instead of assuming the spawn frame is an update.

## Actor and callable deferred execution (CI pending)

Delay, Retriggerable Delay and Timer also work in actor Start/Update and callable void graphs. Parent timers keep ticking even if a child overrides Update; an empty child Update with its own timers preserves the inherited Update body. Native actor callbacks stop dispatching after their entity becomes invalid. Cancel all pending timers clears pending work across graphs and inherited declaring classes of the current instance (not other actors). Screen exit clears its own pending timers. Deferred function continuations reject reads of original parameters; persist data in fields instead. Construction call chains cannot schedule deferred work. Regression fixtures exercise inherited actor delays and callable void events. Dedicated custom event graph authoring and dispatchers remain outstanding; native coroutine integration is not claimed.

## Native custom event calls, dispatcher first slice and class checks (CI pending)

Callable void graphs can be invoked using Call custom event with typed pins. Bind, Unbind and Broadcast event dispatcher select one named void handler and hold its binding on the declaring class instance. Broadcast invokes the handler only while bound; inherited references preserve virtual dispatch. This is a single-handler named signal, not a multi-listener or cross-object event dispatcher. Native class checks/casts select a Behaviour class using the existing module scope; a valid Entity whose Behaviour derives from that class matches. Cast provides Success/Failure/Then and an Entity handle (null after failure). Direct typed derived Behaviour pins and arbitrary asset-class identity are not implemented. Added document and native runtime coverage; CI pending.

## Typed maps and sets (CI pending)

Native variable picker and node catalogue expose BP_MapInt, BP_MapString, BP_SetInt and BP_SetString. Maps use string keys and int/string values; sets hold unique int/string values. Nodes cover Length, Contains, Remove, Clear, Add/Set and Get with typed fallback. Map Set replaces an existing key; duplicate Set Add returns false. Each collection is an ordinary generated AngelScript class backed by the registered native array add-on; no dictionary dependency or second runtime is introduced. Capacity is limited to 4096 entries; new insertion returns false when full. Lookups are linear. General key/value types, custom enum/struct declarations and shared library assets still remain outstanding. Added round-trip and actual engine runtime fixtures for map replacement and set uniqueness.

## Project shared function and macro libraries (CI pending)

Publish a screen function or inline macro through Layout > Publish to project library. Library graphs use globally unique names, persist with the project and are callable from other screens and actors through Call shared library. Functions generate module-level native AngelScript; macros expand at call sites. The initial portable node subset includes typed parameters, local capture/assignment, literals, operators, branches, sequence, bounded repeat, value constructors and calls to other shared graphs. Instance fields, module globals, dispatcher bindings, latent actions and arbitrary engine APIs are deliberately rejected in published graphs. Rename/unpublish is guarded when references exist. External library import/export, arbitrary custom types and multi-listener dispatchers remain future work. Class cast helpers now emit only when referenced, preserving the direct attachment import regression.

## Custom data and explicit event declarations (CI pending)

Create an empty screen function named State, then edit its signature to enum(Idle, Running); create Stats with struct(health:int, name:string). The native graph browser marks the role. Search the context palette for Enum value (member name in Details), Enum equal, Enum to int, Make structure or Break structure. Pins use actual distinct generated AngelScript enum/class types BP_Data_State and BP_Data_Stats; callable and library signatures can use those types. Up to eight members; enum ordinals follow declaration order. Struct fields currently support primitive and vector/color values. Create a local callable graph with event(amount:int) to label it as a custom event, callable with Call custom event. Persisted references are validated during load. No custom type instance-field picker, nested structures, enum dropdown or multi-listener dispatcher is claimed. Added document round-trip and native runtime tests.

## Native coroutines and multiple local dispatcher listeners (CI pending)

A screen callable graph with signature coroutine() can use Wait in coroutine and Next update. Start coroutine launches it using Redux's own Coroutine delegate and start binding; execution state/locals live in the native script context, and Redux pauses/stops the coroutine with the screen. Direct synchronous calls to coroutine graphs are rejected. Actors continue using lifecycle-owned deferred nodes. Bind dispatcher listener / Unbind dispatcher listener select a named signal and a local void handler of matching parameter types; broadcasts evaluate typed arguments once and snapshot bound listeners before invoking them. Multiple local listeners and recursive-call validation are implemented. Cross-object subscriptions and arbitrary coroutine parameters are not included. Fixed MSVC shadowing in library/type lookup and collection generation. Added document and runtime coverage.

## Typed references and portable library packages (CI pending)

Cast Actor to Class now exposes a typed Behaviour handle as well as the Entity reference. Typed references can be stored in explicit fields/globals, passed in function signatures, widened to inherited parent classes and converted back to a valid entity with Get entity from class reference. The variable editor lists declared enums/structures and in-scope Behaviour handle types; typed get/set nodes preserve types. Layout > Export project library writes a non-overwriting .bplib.bp.json package containing published functions, macros and custom data declarations. Drop the package into Studio or select it via Open to merge shared graphs atomically; conflicts reject the entire import and undo restores the previous project. Packages do not copy project assets or actor class dependencies. Fixed JSON role parsing to compare std::string values rather than pointers. Added round-trip and typed reference runtime coverage.

## Custom type and dispatcher integration hardening (CI pending)

Switch on enum derives execution outputs from declared members plus Default/Then. Enum values default to the first member, reject unknown names and cannot be fed arbitrary runtime strings. Structures can include other declared enum/structure types, with dependency-cycle validation. Listener names are validated against compatible local handlers; inherited signals retain the original declaring class's listener fields and broadcast helper. Editing listener/enum declarations cannot silently invalidate existing saved references. Added native runtime tests for nested structure fields, assignment and enum switches. Actor casts, library packages and all new data features remain subject to full desktop CI verification.

## Compact native desktop workspace and Cook release gate

The editor now derives its logical canvas from the desktop client size above 1280×720, rather than enlarging a fixed game-sized canvas. Small-window compatibility is retained. The graph and palette gain working space and additional visible rows; centered modal hit testing shares the render transform, including the native 3D placement viewport. Default typography is smaller and chrome uses neutral grey panels. Windows Cook release packaging now requires Defender verification and records executable hashes and source commit. Native verification pending.

## Dedicated actor class editor

Open actor editor / viewport from a selected reusable actor in Content. A full-window editor presents the component hierarchy at left, isolated native 3D preview and perspective orbit/zoom in the centre, and a property inspector at right, including distinct XYZ transform fields. Create child boxes/cameras, switch component shape, reset inherited overrides, jump to the actor class graph, save, or place the class in the screen. The selected live component has an XYZ orientation indicator (not a draggable component gizmo); the preview strips runtime callbacks, sounds, cameras and physics. Collision wireframe and richer component editing remain outstanding. Verification pending.

## Actor viewport interaction and panel layout

Actor class editor supports parent-space projected translation, axis-driven numeric rotation/scale, live isolated preview with one undoable commit on release, Escape cancellation, mouse orbit and zoom. Native collision debug geometry is preserved for the isolated class preview and can be toggled. Graph and class viewport side panels can be resized using their dividers. The actor viewport remains accessible from its graph tab. Corrected X11 test driver option termination for negative axis values. Rotation uses axis handles rather than circular rings; panels resize but do not yet undock. CI pending.

### UI density and class tabs

Layout menu and actor toolbar cycle Auto / 100% / 125% / 150% (bounded by the minimum supported client). Layout > Reset editor layout restores default density and splitters. Construction and Event graph buttons open native class graphs; the Actor viewport tab returns directly to its class. Added X11 interaction coverage for transform dragging, one-step undo, inspector resizing and class tab navigation. Construction scripts are not executed in the authoring preview. CI pending.

## Grouped Details, rotation rings and content drawer

Actor Details now groups identity, transforms, rendering/animation, class/camera and collision/physics with independent collapse state and wheel scrolling. Rotation mode draws three projected parent-axis rings and selects the nearest ring tangent for angular drag; this edits the native Euler field and preserves a single undo step. The graph's bottom Content drawer shows reusable assets, opens actor viewports directly, and links actors into the active screen via drag/drop (default origin; use the placement viewport for world coordinates). Other asset types open the existing library. This is a docked drawer, not a general-purpose floating-panel/docking framework. Native desktop regression updated for category coordinates and drawer access; CI pending.

### Actor viewport polish

Explicit Translate/Rotate/Scale toolbar glyphs replace the mode-cycle button. Focus frames the selected component; clicking near a projected component origin also selects it and scrolls the hierarchy into view. Class preview asset resolution retains the originating screen when a class graph has no screen owner. Dragging an asset into a Behaviour graph is rejected rather than implicitly placing it in an unrelated screen. Verification pending.

## Persistent layout and graph tabs

Native editor density, panel widths, collapsed actor inspector sections and content drawer visibility persist in a versioned bounded editor-only layout file under own/ (not in exported game projects). Layout resets restore defaults. Up to 32 named graph tabs retain their independent pan and zoom; opening a graph from the hierarchy, function call or class navigation creates/reuses its tab. Tabs close without deleting the graph, scroll horizontally with the mouse wheel and are cleared when another project is loaded. A separate existing actor viewport return tab remains; multiple independent actor viewport sessions and floating OS windows are not yet supported. X11 test exercises tab selection and persisted layout after close. CI pending.

### Unified graph and actor viewport tabs

The native tab strip now also appears in actor viewports, with up to 16 independent actor sessions retaining selected component, hierarchy scroll, camera orbit, focus, zoom, inspector scroll and transform mode. Switching tabs releases the previous isolated preview and recreates only the selected class world. Graph keys and asset IDs are resolved rather than retaining stale graph indices. The existing toolbar viewport shortcut stays compatible. Layout drawer visibility is read as a JSON boolean; the compact regression restarts Studio to verify preference loading. Tabs are session-local, not a multi-project editor or floating window system. CI pending.

## Floating content panel and native asset previews

The content drawer can float within the Redux editor window, move by its title bar, redock at the bottom or via a Dock button, and remember its bounded position across restart. The docked graph tab strip moves above the drawer; a floating drawer overlays the graph without changing its tab position. Visible actor tiles use up to four isolated native 3D preview worlds (128 entities each), framed by component transforms, with runtime callbacks, audio, cameras and physics stripped. Worlds are cached per visible asset list and document revision, destroyed when hidden or leaving the project, and unsupported/over-budget assets retain icons. This does not implement detachable OS windows or widget thumbnails. Native desktop regression checks real preview creation and float/redock. CI pending.


## Native widget thumbnails and direct asset editing (verification pending)

Visible widget assets now use the same native Ui designer renderer, resolved nested widget page and theme as the full designer, scaled to a clipped 16:9 tile. Actor and widget previews share the existing four-item budget and document-revision cache. Failed or unsupported previews fall back to asset icons; all preview UI objects and worlds are released on cache invalidation or panel closure. Clicking a widget tile opens its designer directly. This is not yet a unified widget/scene tab strip or detached desktop window. Native desktop verification is required.


## Triangle-based actor viewport selection (verification pending)

After gizmo hit testing, the class viewport casts the native camera ray against actual mesh triangles, using the current native mesh detail, model part ranges, current skeletal pose, and world transforms. Procedural shape construction is shared with SolidMeshes rather than approximating a different bounding shape. Closest positive surface wins; only non-mesh component helpers retain origin selection. This is geometric rather than pixel-perfect picking (transparent texture cutouts and shader displacement are not evaluated). Scene placement viewport picking is unchanged. Desktop compilation and occlusion/selection tests are pending.


## Unified graph, actor, widget and scene-placement tabs (verification pending)

Opening a widget designer or linked screen placement view creates a bounded, session-local visual tab, keyed by stable asset/screen identifiers. Switch using the same native tab strip as graph and actor tabs. The widget hierarchy selection and aspect mode, and the scene selection, camera, placement mode and outliner position are restored; graph keys are resolved on return. Native preview objects are released when switching. The legacy centered editor layouts remain for compatibility; the separate original Redux scene editor still launches as a separate screen. This is not yet a general window docking framework.


## Opt-in native editor Construction preview (verification pending)

The actor viewport has a Construction preview toggle, off by default. It compiles the existing generated AngelScript for the screen and calls only its parameterless Construction function against a separate temporary actor world, never attaching the script object as a Behaviour. No start/update/onGone callbacks run. A fail-closed node allowlist permits synchronous values, bounded control flow, actor component lookup, primitive shape and transform/color/name changes; other function calls (including parent calls), globals, spawn, deferred actions and external APIs are rejected. Failure discards the candidate and retains authored defaults; successful changes affect only the preview, not the project or exported source. Authored transform gizmos are disabled while a successful constructed view is active to avoid writing derived values. Runtime compilation/desktop verification pending. This is a restricted preview rather than arbitrary editor-time script execution.


## Resizable top/bottom and floating content docks (verification pending)

Content panel supports top and bottom docking with visible drop guides, floating corner resize, dock-edge height resize and multiple asset rows. Editor-only layout persistence stores the last docking edge and height, with backward-compatible defaults. Graph clipping and Fit Graph use the available center area rather than placing nodes behind the docked drawer. Existing thumbnail budget remains four visible native previews, with icons for additional tiles. Compact desktop regression exercises top/bottom docking and the opt-in empty Construction graph. Other side panels remain fixed/resizable, so general-purpose docking and detached desktop windows are still incomplete.


## Construction preview call-tree validation (verification pending)

Editor-safe Construction now accepts synchronous reusable functions, custom events, shared libraries/macros and explicit parent calls. Validation lives in the native document layer, traverses nested calls and checks the concrete actor's virtual overrides, rejecting runtime-only nodes even when hidden behind inherited helpers. Native typed primitive/vector/color/entity pins, safe local fields, arithmetic and selected synchronous flow nodes are accepted. Global accesses, dispatchers, coroutines, arbitrary APIs and recursive calls remain excluded. Added document regressions for inherited unsafe virtual overrides and shared library/geometry calls. The preview still invokes only the generated Construction method on a disposable world.


## Responsive desktop widget workspace (verification pending)

At logical widths of at least 1500, widget tabs use a full-client native workspace with a fixed-density hierarchy/palette on the left, a large aspect-correct design canvas in the centre, and the existing property/anchor/event/timeline tools on the right. Input, widget picking, offset dragging, and side controls share the same responsive geometry. Small clients retain the 720p layout. Added actual X11 authoring and 1080p/4:3 screenshot coverage. This does not resize the separate scene editor yet or introduce a detached OS window.


## Responsive scene placement and surface selection (verification pending)

Large desktop scene tabs now use a full-width native viewport beside the content/outliner panel; compact clients preserve the existing arrangement. Both plan and perspective drawing, coordinate conversion, gizmos and input share responsive bounds. Native scene placement picks the closest visible mesh surface, maps the component to its owning linked actor, and retains explicit marker selection for non-mesh actors. Dragging a surface preserves the clicked world-plane offset. The native standalone scene tool can be opened directly from the scene tab; returning restores the placement tab instead of dropping the user back in the media browser. The original editor still runs as a separate screen and saves its own native scene files, not graph-linked placement data. No OS detachment is claimed.


## Visual comparison checkpoint (2026-11-28)

Inspected actual native 1920x1080 and 1280x720 desktop screenshots, not a mockup, from [successful CI run 46087884702](https://github.com/TroJanVirus/NexgenRedux-BlueprintStudio/actions/runs/46087884702) (compact-desktop-evidence artifact 15240371421). The subsequent [Construction/docking run](https://github.com/TroJanVirus/NexgenRedux-BlueprintStudio/actions/runs/46088206571) produced artifact 15240670632 with the isolated Construction preview and top/bottom docking logs. Compared against Epic's [Blueprint class editor](https://dev.epicgames.com/documentation/unreal-engine/blueprints-visual-scripting-user-interface-for-blueprint-classes-in-unreal-engine), [component viewport](https://dev.epicgames.com/documentation/unreal-engine/components-mode-viewport-in-the-blueprints-visual-scripting-editor-for-unreal-engine), and [UMG editor](https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-editor-reference-for-unreal-engine) references.

* Actor component hierarchy, grouped transform details, compact viewport controls and graph tabs now broadly follow the reference arrangement. Graph text and controls are more compact than the initial zoomed workspace.
* Remaining gaps seen in screenshots: toolbar buttons are large flat text blocks, graph navigation is a flat list rather than categorized My Blueprint, node headers lack richer iconography and inline pin affordances, and the reference grid draws over opaque actor geometry rather than being depth-tested. The content drawer remains a specialized dock rather than a universal dock framework.
* The newer large widget and scene layouts are not covered by these older screenshots; do not claim final visual parity. Added 1080p scene drag/drop, perspective capture and original native tool return coverage to the existing compact desktop test. Full review requires the latest screenshot evidence and a completed run.
* OS-level detachable editor panels require additional platform/GPU window, input routing and lifecycle work. Keep the one shared BlueprintDocument and editing lease; never open independent writable copies just to mimic tabs in multiple windows.


## Native visual refinement pass (verification pending)

Added an optional categorized My Blueprint navigation tree with collapsible screen/class owners and events, Construction, functions, macros, library and type sections. The list/tree preference persists in the existing layout file. Flat navigation remains the default to preserve existing authoring workflows and automation. Node headers now distinguish events, pure operations, flow and ordinary calls, and pins distinguish execution sockets and connected/unconnected value sockets without changing graph coordinates. Both native 3D preview types use an opt-in cached grid mesh instead of projected Canvas lines, so opaque actors occlude the grid using normal depth testing; ordinary game screens do not enable it. Added desktop tree navigation screenshot coverage. Native build and visual verification pending.


## Inline node fields and toolbar refinement (verification pending)

Selected nodes at readable zoom display explicit editable value fields for unconnected inputs on rows without competing output labels. Scalar values can be edited in place; Enter validates and commits through the existing document command, Escape or an outside click discards the draft. Compound values and named entity choices retain the established typed editor. Pins, Ctrl-disconnect, selection and graph hit targets are preserved. The graph toolbar now uses neutral chrome, native vector-drawn action icons and a distinct Play state. Added real desktop input/undo coverage. This is not yet universal inline editing of every pin layout or a finished visual parity pass.


## Four-edge content docking (verification pending)

Content can now dock against all four graph edges, float or hide; side panels have their own persisted width, compact header actions and resize handles. The available graph rectangle, fit-to-view, tab strip and drag hit targets exclude docked content. Added native left/right docking captures and a return to bottom docking regression. This remains content-specific docking, not a general multi-panel split tree or OS window detachment.


## Scene framing and Windows diagnostic fix

Inspected native scene/widget captures from artifact 15240686769, including the original native editor handoff and returned tab. The perspective screenshot revealed poor initial framing; the scene now frames constant placed actors when entering perspective and offers a Frame control for selected/all actors, including their preview component positions. Focus is retained per scene tab. This is editor camera framing, not gameplay camera changes. Removed a shadowed node spec variable rejected by MSVC /WX (C4456), confirmed in the Windows job logs. Full verification of the combined changes remains pending.


## Perspective authoring camera independence (verification pending)

Scene framing now changes only the editor camera target, never the placement plane. Moving existing instances retains their own Y coordinate, while new instances use the user-selected plane. Perspective view supports right-drag orbit, middle-drag pan, wheel zoom, proportional toolbar zoom and a reference grid centred near the camera target. The far plane expands for widely separated actors. Camera focus/pitch is retained per scene tab. The grid remains editor-only. No changes to exported gameplay camera behaviour.


## Cross-platform warning fix and scene navigation regression

The Windows compiler rejected two ternary integer widths passed to Canvas::rect (C4244 with /WX). Use explicit float widths in the compact content header. Linux and macOS passed the earlier node-editing/navigation changes; Windows needs a fresh run of this combined revision. Added real X11 orbit, pan and zoom input with a saved-document equality assertion to verify camera navigation leaves authored graph data untouched. Perspective panning now clamps the target to a finite editor working range.


## Verified visual baseline and graph overview

Workflow 46090383210 succeeded across Windows x64, macOS ARM64, Linux native desktop and sanitizer/document jobs. Artifact 15240918803 contains actual graph, categorized navigation, four-edge docking, scene navigation, widget and actor captures. The optional Graph Overview minimap added after this baseline is a native canvas rendering of the active graph with selected nodes, links and viewport outline. Click/drag recentres the graph without authoring mutations; its visibility is persisted, it follows docking bounds, and it can be disabled from Layout. The new native desktop regression asserts graph JSON equality after navigation. Combined minimap verification is pending. Genuine detached OS windows and generic panel docking remain outstanding.


## Native scene context integration

The placement viewport now reads the selected saved Redux scene from scenes.json and renders up to 512 supported visual entities as read-only context in the same preview World as linked Blueprint actor instances. The loader never writes the original scene, validates scene filenames, filters through a visual-only field allowlist and excludes behaviour, audio, physics, cameras and emitters. Native context participates in frame-all without becoming a selectable Blueprint instance. Unsupported entities are counted rather than silently converted. A runtime test verifies the filtered data and original bytes. Native terrain, triggers, direct editing of the original scene from the placement tab, and a true unified scene undo stack remain unimplemented; the original editor remains the authority for native scene edits. Verification pending.


## Graph panel visibility and full-width focus layout

The navigator and node library/details dock can independently hide or restore via Layout, preserving the original dock widths and graph coordinate system. Graph clip, minimap, tabs and content docking expand into reclaimed width; hidden panel input/resize targets are disabled. Visibility persists. Native desktop coverage exercises hiding/restoring both. This is a useful dock visibility feature, not universal drag docking or a detachable OS-window implementation. The engine still presents through one Platform native window and GPU Device; separate render targets, per-window input, resize/close lifecycles and shared resource ownership must be designed before true multi-window editing can safely ship.
