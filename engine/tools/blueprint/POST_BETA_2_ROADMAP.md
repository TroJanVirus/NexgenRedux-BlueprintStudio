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
