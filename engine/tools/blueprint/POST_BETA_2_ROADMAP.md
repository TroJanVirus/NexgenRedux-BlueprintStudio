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
