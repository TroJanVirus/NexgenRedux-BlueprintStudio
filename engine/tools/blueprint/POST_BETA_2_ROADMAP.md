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
