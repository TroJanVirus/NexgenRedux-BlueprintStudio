# Development rules

Blueprint Studio supplements Nexgen Redux. The aim is to make game logic easier to write, read, organise and reuse without replacing the engine or its tools.

Before starting a feature, read the [engine guide](../../../docs/Guide.md), [script reference](../../../docs/ScriptReference.md) and the relevant original tool. If Nexgen already does the job, use its API, file format or tool. Put visual controls in Studio only where they make that workflow easier. Do not add a second runtime for graphics, physics, animation, networking, audio, assets or UI. Keep changes to the native engine small and explain why they are needed.

## Keep up with upstream

At the start of every Blueprint Studio development request, even a short "continue", check the current `main` of [Team Resurgent's Nexgen Redux](https://github.com/Team-Resurgent/NexgenRedux), the fork's `main`, and the latest Blueprint review branch. Do this before changing any code.

- Keep the fork's `main` a clean upstream mirror. Fast-forward it only when GitHub confirms the old head is an ancestor of upstream. Never force-push it or mix Blueprint changes into it.
- If upstream has changed, inspect its commits, API changes, build files and pinned submodule revisions. Keep the media and documentation submodules at the revisions chosen by upstream, not their independent latest branches.
- Integrate upstream into a separate review branch from the current Blueprint head, preserving both histories and all existing Blueprint work. Resolve conflicts against the real source; do not blindly take one side.
- Refresh the native node catalogue and integrations when upstream APIs change. Check generated scripts, old project files, native tools and platform build compatibility.
- Run the compiler, JavaScript, browser and native tests where available. Do not publish an unverified integration as a working build; explain any failed or unavailable tests and retain the previous working review branch.
- Report the upstream and Blueprint revisions and whether an update was necessary. If they are already aligned, avoid unnecessary merge commits or builds.

Do not push changes to Team Resurgent's repository. If a safe merge cannot be completed with the available tools, keep the current working branch intact and report the blocker instead of overwriting source.

## Access from Nexgen itself

Blueprint Studio is a development tool for the desktop Nexgen application, not just a separate download. Users should be able to open it from Nexgen's own graphical interface, with project creation, existing project import, native scene editing, play testing and packaging available as one connected workflow.

- Use Redux's own native rendering, input, JSON and platform services. No external browser, embedded webview, Node.js or HTTP server is required by the native editor.
- No projects have been made with the pre-release browser prototype. Prefer a clean native versioned schema over legacy compatibility. Keep compiler fixtures as behavioural references, not file-format obligations. Do not silently replace unsupported logic or claim feature parity prematurely.
- Native v2 is the canonical authoring model. Use the generated script-reference catalogue, typed pins, stable node IDs, structured execution, per-instance variables, acyclic function graphs and bounded undo/redo. Keep editor and compiler on the same model; legacy browser files are not supported. Reuse the engine's JSON parser and writer.
- Do not ship the reference HTML/bridge as part of the desktop application; desktop development tooling stays out of Xbox game packages.
- Reuse the native scene editor, debugger, profiler, asset cooker and runtime. Do not create duplicate implementations or put the desktop editor into Xbox game packages.

## What Studio adds

| Area | Use from Nexgen | What Studio adds |
|---|---|---|
| Gameplay | AngelScript, Screens, Behaviours | Visual functions, graphs, variables and reusable native Behaviour classes. Ordinary compiled scripts; per-entity state and lifetime are managed by the original World. |
| 3D levels | Original scene editor, Space, Level helper | Launch the original editor, bring its files into the project, assign Behaviours and connect load/update/draw nodes. Removed the duplicate browser 3D renderer and camera. |
| 2D scenes | Scene, prefabs, children | Simple layout and native prefab selection. Prefer native prefabs to graph spawn trees unless named child output pins are needed. |
| Animation and waiting | Sprite/model animation, Tween, coroutines | Native API nodes and visual state/curve graphs. Prefer native Tween for simple animation. Existing graph Delay and Timeline have their own continuation and multi-track semantics; keep old graphs compatible and use native helpers wherever equivalent. |
| Menus and HUD | Native widgets, JSON pages and themes | Visual authoring of the same files and links to graph handlers, not a new widget runtime. Browser previews are approximate. |
| Media | Native asset paths, loader, cache and Nexgen Cook | Project file library and reference checks. Use the existing cook tool to convert assets, not another converter. |
| Play testing | Native engine and dev reload | Multi-screen in-process preview uses the original screen stack, isolated screen registry and transient Save. Project asset staging and full deployment remain separate work. |
| Debugging and profiling | Native TCP debugger, VS Code extension, native sampling profiler | Node breakpoints and graph navigation; display the native profile and report without writing a second profiler. VS Code remains supported. |
| Multiplayer | Room, Message and native shared entities | Expose existing API nodes. Do not add a second transport or entity sync system. |

References: [original scene editor](https://github.com/Team-Resurgent/NexgenRedux-Media/tree/main/media/showcase/editor), [native Level helper](https://github.com/Team-Resurgent/NexgenRedux-Media/blob/main/media/global/scene.as), [Cook](../cook-ui/README.md), [VS Code debugger](../vscode/README.md).

## Review checklist

- Does the change solve a real problem for someone making a game or keeping a graph tidy?
- Is there already a native API or tool that does the work? Can Studio call it or hand off its native files instead?
- Are native v2 projects still editable and runnable? Before release, schema changes are allowed when they improve the design; avoid silent changes to gameplay semantics.
- Can users change layout and appearance without changing the generated code?
- Is the documentation simple, with clear instructions and honest test limits?
- Test the graph compiler and real engine where possible. Report substitutes and untested platforms as such.

## Native verification branch

The dedicated draft pull request from `blueprint/review-native-ui` into `blueprint/verification-base` runs only the focused native Blueprint workflow. Keep this PR open for ongoing verification; do not merge it into the upstream mirror. Use the PR's actual head SHA and job logs to distinguish failed builds from missing results. One branch update should trigger one focused workflow, not a duplicate push workflow or the full release matrix.
