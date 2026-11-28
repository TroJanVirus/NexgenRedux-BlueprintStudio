# Flight and Rolling Ball starter projects

Both starters are generated as native Blueprint documents, compiled, serialized and reopened before project creation. They use Redux primitives and have no external media dependencies.

## Flight
- WASD / left stick: move relative to heading. Mouse / right stick: steer.
- Point up or down and move forward to climb or descend.
- Edit speed and cameraDistance. Fly integrates position from input and delta time; this is an arcade flight starter, not a rigid-body aircraft simulation.
- Course markers are visual only. Camera has no collision avoidance.

## Rolling Ball
- WASD / left stick: apply a frame-rate-scaled impulse on the horizontal plane.
- A native 1 kg sphere with radius 0.5 uses Redux gravity and collision.
- Navigate the fixed gate and slalom obstacles. Edit driveStrength and cameraDistance.
- No scoring, checkpoint logic, reset or camera collision avoidance is included yet.

Each graph has a task-level comment. The native additional-template runtime test checks document generation, compile, scene initialization, actual movement and portable package round-trip. Passing that test is required before calling either starter verified.
