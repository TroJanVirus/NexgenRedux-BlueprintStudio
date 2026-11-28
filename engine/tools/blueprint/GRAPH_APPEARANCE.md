# Native graph appearance

Node widths use the native font's measured titles, pin labels and visible values (196–640 design units). Long values remain in the inspector. Layout > Arrange graph accounts for measured node widths; existing manually positioned graphs retain their coordinates, and Arrange is undoable.

Glyphs scale with zoom; titles remain displayed at the minimum 35% zoom. Pin labels are hidden at low zoom to reduce clutter. Pin padding scales with zoom. Node bodies have 83% opacity.

Layout > Wire style cycles Curved (default), Orthogonal and Straight and persists the choice. Curved wires use cubic Bézier sampling even when endpoints face backwards. Only Orthogonal mode uses the automatic endpoint detour. Double-click a visible wire to insert a draggable reroute; Ctrl+right-click remains available. Other intervening nodes are not obstacle-routed. The same sampled geometry is used for reroute hit testing, preserving the original anchor interval.

When endpoint nodes have unequal widths, automatic detours use their combined left/right bounds. For very narrow forward gaps, the curve handles are constrained to the gap.

The node library starts with functional categories, shows their available node counts, and drills into the chosen group. Search remains global, exact node IDs rank first. Graph navigation defaults to flat category sections (Events, Construction, Functions, Macros, Shared library, Types); the Variables section remains directly beneath it. Groups/List toggles the navigator mode and persists the preference. Old Tree preferences migrate to Groups by default.

Native desktop authoring tests use the actual measured node/pin positions and allow sufficient space between nodes. The Timeline regression checks each expected wire in the recovery document after the corresponding real mouse gesture. It continues to require all nodes, literal values, connections, native source export and rendered camera motion. Overlapping nodes can still conceal pins; rearrange or drag them apart.


Graph interaction: drag empty canvas to marquee-select nodes, Ctrl-click to toggle individual nodes, drag any selected node to move the selection as one undoable edit, hold Shift to snap the dragged anchor to a 16-unit grid, and press Q to align selected nodes horizontally. Unconnected typed inputs and literal values are editable directly on visible nodes at readable zoom; Enter confirms and Escape cancels. Edit node opens the detailed input inspector. Project browsing does not require saving; switching projects saves named work or preserves a verified recovery archive for unnamed/conflicted work.

Double-click a node body or title to open Edit Node; single-click its visible value to edit inline. The detailed editor stages changes until Apply, validates all values together, and commits them in one undo step. Cancel, Escape and outside click discard the draft. Standard confirmation dialogs expose Apply and Cancel buttons with matching keyboard hints.

Value fields use stable graph-space bounds shared by rendering and hit testing, independent of selection or zoom. Labels never switch to an equals-sign presentation. Double-click a node outside fields and sockets to open Edit Node; repeated clicks within a field remain inline.

Right-click an empty graph for Add Node; right-click a node for its context menu (Edit Node, Rename Node, Delete Node, Disconnect, Change Colour, Add comment). Edit Node includes shared display name, instance comment and colour, and delete. Display names propagate to matching node identities across project graphs without changing native API names or function symbols; colours and comments are instance-local. Existing comment groups remain available through Layout. Double-click function/library/parent/event/coroutine calls to open their definition; Back to caller returns to the original graph.

Comment groups have independently saved rectangle bounds; drag any edge or corner to resize without moving nodes. Click the title for comment text and colour. Node and comment RGB selectors support every six-digit RGB value, optional # input, canonical uppercase hash-free persistence, explicit Default/Apply/Cancel. Theme hex input accepts either form while native theme assets retain their required # prefix.

Graph shortcuts: Alt-click a pin disconnects its links. Ctrl-drag a wire moves its nearer endpoint; invalid drops retain the old connection. Shift-click highlights a connection and dims others. Double-click a wire inserts and grabs its reroute handle. Ctrl+F searches the active project graphs, names, values and comments; C wraps the selection. The RGB spectrum picker uses a hue/saturation field, brightness slider, optional opacity slider, live preview and hash-optional hex text; alpha is supported for theme and runtime Color values.

Camera Sequencer: place Camera Sequence in a screen Update graph. Double-click its body or select Open Sequencer. Play, stop, scrub, add/delete camera keys, edit time/position/target/FOV, switch a key between cut and interpolation. Preview is a schematic XZ path, not a rendered scene. Apply stores keys atomically; runtime uses Space::camera and a per-screen rising-edge play input, stop input, active/finished/elapsed outputs. Existing screen graphs remain the scene ownership boundary. Keep gameplay camera writes gated while active.

Scene browser: use Scenes beside My Blueprint. Select a native screen and a reusable camera sequence; create, rename, edit, place or delete unreferenced assets. Place adds a reference to that screen's Update graph; only a blank Update is automatically wired. Existing inline sequences can be promoted with Save as reusable sequence. Editing a shared asset updates all compiled references; asset IDs remain stable when renamed. The project still stores actual Redux screens, not a second scene model.
