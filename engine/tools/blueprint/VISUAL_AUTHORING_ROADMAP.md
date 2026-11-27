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
