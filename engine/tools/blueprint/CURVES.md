# Visual Bézier authoring

The native **Create cubic Bezier curve** node is an execution node returning a Redux Curve@. It replaces a constructor and four Add calls when a cubic path is appropriate.

1. Search for "Create cubic Bezier curve" in the node library and connect it to On Enter.
2. Select the node, then choose **Edit curve** in Details. Drag the green endpoints and blue control handles. The path is evaluated by Redux's own Curve implementation.
3. Apply commits the four values together; Undo restores all four. Cancel or Escape discards the draft.
4. Store the result in a Curve@ variable and sample it with Curve.at(t), using a Timeline's progress (0 to 1).
5. Use Get X (Vec2) and Get Y (Vec2) to feed Batch.move, or send the point to another movement node.

The editable sample is examples/visual-bezier-sprite-path.bp.json. It keeps an untextured sprite batch and displays a path. Its On Enter creates the curve once; Update samples it; Draw displays it. For many sprites, update a persistent batch rather than reconstructing it each frame.

The visual editor edits four constant Vec2 inputs. A curve whose points are driven by other graphs retains those wires and uses the normal input inspector; disconnect them to edit constant values visually. The editor supports coordinates within +/-1,000,000, while numeric pin input retains the normal finite-float range. Cubic curves are two-dimensional; for 3D positions, map the components into an appropriate plane or use the existing 3D Path API. No extra runtime extension is required by the generated script.

Do not confuse constant-speed movement with a linear change in t: uniform t on a Bezier curve does not imply uniform distance. An arc-length lookup or the existing distance-based Path is appropriate for constant speed.
