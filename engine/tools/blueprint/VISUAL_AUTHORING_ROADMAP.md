# Native visual authoring after Beta 1

Both features are product requirements, not shipped Beta 1 capabilities. Development starts from the verified Beta 1 commit and leaves the published tag and release unchanged.

## 1. Native UI Designer (UMG-style)

Use Redux's existing widget, theme, Canvas and input runtime. The legacy browser UI Designer is a useful specification, not the implementation shipped to desktop users.

- Native palette, hierarchy, widget selection, drag/drop/reparent/reorder, inspector, undo/redo, import/export existing native JSON layouts and themes.
- Actual Redux renderer preview; current container layouts plus anchored/free-position HUD layout where necessary, safe area, 4:3 and 16:9 preview at target resolutions. Do not pretend an editor-only layout feature works in the runtime.
- Text, images, buttons, progress bars, sliders, toggles, fields, tabs, lists, custom drawing, themes, styles and focus/controller navigation.
- Typed widget references, click/change/focus/done/draw/list-row Blueprint event functions, event-driven value updates, reusable widget layouts, animation timeline.
- Save/reopen, recovery, dependency checking, project-local/shared assets and standard Redux export. Preserve imported data or explicitly diagnose unsupported fields; never silently discard them.
- No browser or Node runtime dependency for native editor or games.

### Acceptance
From a blank native project, visually build a main menu (Play, Settings), a settings panel and a HUD (health/ammo). Connect button events and live health/ammo variables without handwritten scripts. Play with the native renderer; verify mouse/controller interaction, focus, animation and aspect ratio changes. Save/reopen and export; import existing native UI JSON and verify semantics. Include document/runtime and real desktop input regression tests. Verify console builds and report real hardware tests separately.

## 2. Blueprint Classes (reusable actors)

Use Redux's existing World/Space/Entity/Behaviour lifetime, scene editor and native assets. Native 2D prefabs support children and behaviours; 3D Space loading has entity/behaviour support but does not provide the full class authoring and instance workflow.

- Project content library: searchable/folder-organized reusable actor definitions, stable IDs, thumbnails, create/duplicate/rename, create from scene selection and dependency diagnostics.
- Actor/component editor: root, child hierarchy, transforms, models/sprites, materials, collision/physics, animation, audio, attached Behaviour graphs, exposed typed properties and optional camera rig. Explicit player ownership and camera activation rather than arbitrary camera selection.
- Drag actor assets from library into native scene viewport; placement preview, transform editing, duplication, selection and undo/redo. Native scene instances reference definitions rather than flattening unrelated copies in authoring files.
- Class edits update linked instances except explicit per-instance overrides. Reset overrides, detach/unpack, stable instance IDs, independent runtime state, correct parenting and destruction. Detect cyclic nesting and bound expansion.
- Runtime Spawn Blueprint Class operation and references, standard Redux scene/runtime export, project-local/shared dependency resolution; reuse native prefabs where appropriate and extend 3D runtime only where required.
- Preserve existing scene files and assets. Save/reopen/recovery, portable export/import and missing-reference diagnostics. No separate game runtime.

### Acceptance
Starting blank, create BP_Player with model, collision, movement, animation and follow camera visually. Drag it into two distinct scenes; run and control the player and camera. Add two BP_Enemy instances; verify independent runtime state and property overrides. Change the class once, verify linked instances update and overrides survive save/reopen. Spawn and destroy instances from graphs. Export and run using Redux runtime. Test malformed/missing assets, reference cycles, nested hierarchy limits, actual desktop drag/drop and cross-platform builds. Assess 64 MB original Xbox resource budget and verify hardware separately.

## Implementation sequence

1. Shared project asset identity, reference/dependency validation and native authoring integration. Keep actor and widget asset kinds distinct; use the same library infrastructure where appropriate.
2. Native UI layout editing and preview, then event binding, live properties and reusable widget assets.
3. Actor definition/hierarchy editing and persistent scene placement, then overrides, runtime spawn and camera/animation integration.
4. End-to-end blank-project authoring tests combining both: player actor + enemy instances + HUD + pause/settings menu across two scenes.

Build in coherent vertical slices, testing actual save, play and export behavior at each stage. Keep existing Beta 1 regression coverage and release gates. No new beta tag until all intended release checks pass.

## Existing reference code

- `engine/ui/`, `engine/canvas/`
- `engine/tools/blueprint/ui_design.js`, `ui_design_editor.js`, `test_ui_design.js`
- `engine/scene/prefab.cpp`, `space_load.cpp`, `world.h`, `space.h`
- `engine/tools/blueprint/native_document.*`, `native_compile.cpp`, `native_assets.*`
- `engine/app/blueprint_workspace.*` and the original Redux scene editor

## First implementation slice

Native v2 project documents now persist bounded actor/widget library definitions with stable IDs, exact native JSON source strings, typed kinds and explicit dependency references. Loading and atomic replacement reject missing, duplicate, cyclic and over-deep dependencies; graph/document undo and redo retain the library. This is authoring metadata only: no actor placement, rendering, UI event integration or runtime export is claimed yet. The native document test suite exercises round trips, rollback and invalid references. The visual-authoring branch runs focused native verification without publishing a beta. GitHub artifact actions move to their Node 24 versions; legacy JS test removal is a separate migration, not a test skip.

## Content Browser first interactive slice

The native Assets window now has a Content tab. Create a project-wide actor starter or widget page, rename or remove definitions, add flat label/button/progress/toggle widgets with unique IDs, and copy a native page snapshot to a selected screen. Copy refuses to overwrite differing files; add Ui::load/Ui::draw nodes manually. Ctrl+Z/Y undo/redo document edits, and Ctrl+S saves the project. Existing media import and native Scene editor are unchanged. This is a structural palette, not a native-rendered UMG canvas or linked actor placement; the actor starter is only a native scene JSON definition. The real X11 test authors both assets and checks their saved source and published page. Snapshot files are separate from document undo; linked instances and stable non-recycled identities across deletions are required before scene placement.

## Linked UI page slice

The Content Browser Link to screen command adds a visible Load linked widget page node and a standard native UI draw call with Canvas connected. On Enter and Draw must be complete simple execution chains; existing branches or conflicting page loads are refused without editing. The current page JSON is compiled into an inline native Ui::loadPage call using the existing widget parser/builder, so preview/export no longer requires copying a snapshot. The old Copy JSON operation remains explicit. The runtime rolls back widgets added by an invalid page. One linked standalone page per screen is supported by the convenience command; event authoring, nested library dependencies and free-position layout remain future work. IDs allocated by the project advance a persisted high-water mark, including across deletion and save/reopen. Added native document/compiler/runtime and X11 authoring regressions.

## Upstream integration

Merged upstream through `ceeb83b4873806308f17fdd0c5ac5e8edc8bf786` with both histories and upstream submodule pins. The integration branch regenerates the script-reference catalogue and existing fixtures with their original generators, adds the new Playlist and WebSocket handle types, then verifies the exact generated revision. The working visual-authoring branch and Beta 1 tag are unchanged until verification. Do not confuse a source merge with a verified platform release.

## Native upstream API usability audit

The regenerated reference catalogue grew from 804 to 865 API nodes (61 net), but methods alone did not provide a usable workflow. Added native Playlist/WebSocket factory nodes, typed persistent field/global and local reference support for Playlist, WebSocket and Widget, matching array-object-to-array-handle connections for animation blend APIs, an explicit null Entity pin and native document migration for older join/hinge node IDs. Compiler and actual AngelScript binding regressions verify these changes. This is still not a blanket guarantee of every script API: delegate-driven APIs, runtime feature differences by GPU/platform, editor-specific animation authoring and actual device verification need separate work. 

## Upstream reverse-import coverage

Native import recognizes Playlist/WebSocket constructor expressions, explicit handle declarations and reassignment for the supported reference types, null references, and safe array-object borrowing for matching API handle parameters. Function signatures accept supported handles. Import remains transactional and rejects missing @, incompatible types, ambiguous overloads and unsupported expressions. Added document round-trip/negative cases and an actual native playlist execution regression (no external connection is executed). General delegate expressions and arbitrary AngelScript are not yet supported.

## First linked actor placement slice

The Content Browser can edit the starter primitive (box, sphere, capsule, plane) and insert a Spawn linked actor node into a screen's simple On Enter chain. The node compiles the current single-root native entity JSON into Space::make(Json(...), position), with a typed Entity result. Multiple placements across screens share the definition and have independent runtime entities/XYZ offsets. Deleting linked assets is rejected. An empty Draw graph receives the native Space renderer; nonempty Draw graphs are not silently reordered. Tests cover definition edits, multiple screens, serialization, undo and actual runtime entity coordinates. This is not viewport drag-and-drop, nested components, per-property overrides, camera configuration or full actor class support yet. Multi-root/dependency-bearing definitions are explicitly refused rather than flattened. 

## Actor component hierarchy slice

The native Content Browser supports selecting parent-first components, adding a child at local (0,1.5,0), editing its primitive shape and spawning linked instances. Native Space::makeActor delegates each component to Space::make and uses World parenting; malformed parent order, more than 32 components, depth beyond eight, or child terrain-relative placement are rejected. A failed component schedules the partially created hierarchy for destruction. Runtime tests assert parent relationships, local position and recursive destruction. Full transform editing, component removal/reparenting, arbitrary property overrides, component-specific Behaviour authoring and viewport drag/drop remain outstanding. The internal script binding is not added to the reference-generated catalogue; the supported authoring interface is Spawn linked actor. 

## Actor component inspector

The Content Browser exposes name, local XYZ position, Euler rotation and size, native model path, and shared Blueprint Behaviour assignment for each selected component. Model paths are validated for portable syntax, not automatically located or copied; native asset loading reports missing resources. A model replaces the primitive shape. Shared Behaviour references are checked against the actual screen/module at compile and placement; deleting referenced Behaviours is refused. Tests verify edited definitions, undo/redo and distinct runtime Behaviour objects for two actor instances. Property overrides beyond placement offsets, collision/animation-specific inspectors, cameras, asset picker and viewport drag/drop are still pending.

## Physics and per-instance root properties

Component inspector now edits one box/sphere/capsule collider, mass, friction, bounce, gravity and startup looping animation by clip name. Native physics dimensions are validated; collision none removes the existing native shape collider. Model clearing clears startup animation. Spawn linked actor has optional constant instanceName, rotationOverride and sizeOverride strings: empty/unset inherits, explicit values replace only the root property in that instance's generated JSON. Existing projects retain node IDs and defaults. Overrides are saved with the graph and resetting them uses normal clear-input/undo operations. Tests cover inheritance, validation, serialization and native physics/instance state. General per-component overrides, dynamic input values for these overrides, animation-state authoring, visual asset picking and viewport placement are not implemented by this slice.

## Native plan-view placement

The Content Browser actor tab offers a top-down X/Z plan with one-metre snapping and a live drag marker. Drag an actor from the existing library to place a linked On Enter spawn; drag an existing marker to change its constant position while preserving Y. Release outside cancels, Escape cancels, undo/redo uses normal graph history. It deliberately shows only constant linked On Enter placements in the selected screen, within a fixed grid; it does not show arbitrary native scene entities or runtime-generated objects. Existing native 3D scene editor remains unchanged. Added real X11 drag-and-move regression and fixed MSVC warnings discovered in component inspector verification. Full 3D viewport integration remains outstanding.

## Native UI Designer first canvas

Content Browser widget definitions now have a separate designer with a hierarchy, an expanded native widget/container palette, sibling reordering, a property inspector and 16:9/4:3 preview. Preview draws through a separate native Ui instance with the actual layout and styles; design-only loading suppresses gameplay delegates and input processing. Native page JSON is still the source of truth, and undo/redo applies to edits. Supports nested row/column/grid/stack/scroll containers; imported widgets lacking unique IDs are visible but cannot be selected reliably. JSON is reserialized by the native parser when edited (comments/formatting are not preserved). Event authoring, direct drag/reparent, theme editing, visual anchors/free positioning and animation timeline remain outstanding. Tests cover nested native runtime widgets and disabled design-only event binding.

## Widget event graphs

The designer Events tab creates or opens typed native screen function graphs for click, change, focus, done, draw and makeRow; entry pins carry Widget@, plus Canvas@ or row index where required. The linked page stores the actual emitted method name; linking a page to another screen creates compatible empty handler graphs for that screen without sharing runtime state. Unbinding preserves existing graph logic. Referenced functions cannot be removed, renamed, converted to macros or incompatibly retyped. Runtime load and compilation validate callback signatures; design preview still does not bind callbacks. Tests exercise a real controller Accept click and cross-screen graph persistence. The convenience command requires a linked page and does not automatically map arbitrary imported handler names or implement general delegates. 
