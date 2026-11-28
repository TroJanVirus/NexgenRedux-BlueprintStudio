# First Person starter template

Create through the **Blueprint Studio** tile in the desktop Redux library, choose First Person, enter a unique project name, then Create & open. Studio compiles the candidate through the native graph compiler and AngelScript host before writing it. Play test launches the native engine; Escape returns to Studio. Export project emits ordinary Redux screens and media, not a proprietary runtime.

## Controls

- WASD / arrows or left stick: move relative to the camera.
- Mouse / right stick: horizontal yaw and vertical pitch.
- Space / controller A (Accept): jump while grounded.
- Escape / Start: leave Studio play test (normal exported game uses the native menu).

## Graph organization

| Graph | Responsibility |
|---|---|
| onEnter | BuildArena, then SpawnCharacter once. |
| update | UpdateLook(dt), UpdateMovement(dt), UpdateCamera in order. |
| draw | Set virtual canvas, draw Space, then DrawHUD. |
| onSuspend / onExit | Release the mouse. |
| onResume | Restore mouse capture. |
| BuildArena | Establish native collision ground, visual floor, sky, sun, coloured markers and perimeter. |
| AddObstacle(name, place, size, color) | Spawn a SolidBox and give it a static 3D cube collider matching its visual dimensions. Reusable level-building function. |
| SpawnCharacter | Store Player entity; set name, capsule radius 0.35 m, height 1.7 m, collider centre 0.85 m above feet, walker enabled and mass 60 kg; attach robot GLB and capture mouse. |
| UpdateLook(dt) | Read per-frame mouse delta and right stick, update yaw in degrees and clamp pitch to ±85°. Sensitivity and per-second stick speed are fields. |
| UpdateMovement(dt) | Read native velocity, move input, ground state, and Accept edge; calculate yaw-relative motion, normalize diagonal input, blend horizontal velocity with ground/air grip and set jump velocity only when grounded. Native physics integrates gravity, collision and motion. |
| UpdateCamera | Set body facing, compute eye above feet and a look target, set camera FOV to 72°. |
| DrawHUD(surface) | Draw two lines centered on the virtual 1280×720 canvas; extend with health/interaction widgets. |

Each graph includes an editable comment group. The character math nodes expose all their inputs, produce values and have no side effects. Their generated AngelScript is included in exported modules. The inputs and physics are taken from Redux's Blocks and Guards/Explore demonstrations; the robot is the installed Redux global asset, copied byte-for-byte into the new project's shared media root. No upstream media submodule is changed.

## Editable settings

`walkSpeed` 4.6 m/s, `jumpSpeed` 6.4 m/s, `eyeHeight` 1.55 m, `mouseSensitivity` 0.2 degrees/pixel, `gamepadYawSpeed` 130 and `gamepadPitchSpeed` 100 degrees/second, `groundGrip` 14 and `airGrip` 4 per second. `yaw`, `pitch` and `hero` retain per-screen state. Change the FOV on the camera node. The first-person view uses the attached body model, not a separate animated arms rig; skeletal locomotion animation and weapon systems are future work.

The arena is deliberately small for original Xbox memory and rendering budgets. This is a starter, not a full FPS game. Hardware performance and visual quality require real hardware testing.
