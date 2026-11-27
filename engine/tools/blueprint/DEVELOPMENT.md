# Development rules

Blueprint Studio supplements Nexgen Redux. The aim is to make game logic easier to write, read, organise and reuse without replacing the engine or its tools.

Before starting a feature, read the [engine guide](../../../docs/Guide.md), [script reference](../../../docs/ScriptReference.md) and the relevant original tool. If Nexgen already does the job, use its API, file format or tool. Put visual controls in Studio only where they make that workflow easier. Do not add a second runtime for graphics, physics, animation, networking, audio, assets or UI. Keep changes to the native engine small and explain why they are needed.

## What Studio adds

| Area | Use from Nexgen | What Studio adds |
|---|---|---|
| Gameplay | AngelScript, Screens, Behaviours | Visual functions, graphs, variables and reusable logic. Ordinary compiled scripts. |
| 3D levels | Original scene editor, Space, Level helper | Launch the original editor, bring its files into the project, assign Behaviours and connect load/update/draw nodes. Removed the duplicate browser 3D renderer and camera. |
| 2D scenes | Scene, prefabs, children | Simple layout and native prefab selection. Prefer native prefabs to graph spawn trees unless named child output pins are needed. |
| Animation and waiting | Sprite/model animation, Tween, coroutines | Native API nodes and visual state/curve graphs. Prefer native Tween for simple animation. Existing graph Delay and Timeline have their own continuation and multi-track semantics; keep old graphs compatible and use native helpers wherever equivalent. |
| Menus and HUD | Native widgets, JSON pages and themes | Visual authoring of the same files and links to graph handlers, not a new widget runtime. Browser previews are approximate. |
| Media | Native asset paths, loader, cache and Nexgen Cook | Project file library and reference checks. Use the existing cook tool to convert assets, not another converter. |
| Play testing | Native engine and dev reload | Start and stop a dedicated test copy, stage project files safely, use the native reload system. |
| Debugging and profiling | Native TCP debugger, VS Code extension, native sampling profiler | Node breakpoints and graph navigation; display the native profile and report without writing a second profiler. VS Code remains supported. |
| Multiplayer | Room, Message and native shared entities | Expose existing API nodes. Do not add a second transport or entity sync system. |

References: [original scene editor](https://github.com/Team-Resurgent/NexgenRedux-Media/tree/main/media/showcase/editor), [native Level helper](https://github.com/Team-Resurgent/NexgenRedux-Media/blob/main/media/global/scene.as), [Cook](../cook-ui/README.md), [VS Code debugger](../vscode/README.md).

## Review checklist

- Does the change solve a real problem for someone making a game or keeping a graph tidy?
- Is there already a native API or tool that does the work? Can Studio call it or hand off its native files instead?
- Are existing projects still editable and runnable? Avoid silent changes to game logic or file formats.
- Can users change layout and appearance without changing the generated code?
- Is the documentation simple, with clear instructions and honest test limits?
- Test the graph compiler and real engine where possible. Report substitutes and untested platforms as such.
