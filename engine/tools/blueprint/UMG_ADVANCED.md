# Advanced native widget authoring
The designer uses Redux native layout, styles and callbacks, not Unreal Slate. Linked pages use the saved authoring coordinate system and a centered aspect-preserving fit. A different aspect may introduce unused margins rather than crop or distort the layout.
## Slots
- Row and Column: grow=0 (Auto), positive grow divides free space as relative Fill weights. Use self for cross-axis alignment, align/justify on the container, gap between children and padding (one value, X,Y or left,top,right,bottom).
- Stack: point/stretch anchors, XY offsets and explicit size. The root is a column; set Stack grow=1 and stretch alignment for a full-page HUD.
- Grid: columns (0=automatic), columnWidth and gap; List has rowHeight and count. Redux uses ordered cells, not independent row/column spans.
- Properties/Advanced exposes constraints, range, step and state flags. Themes support shared styles and sliced artwork; linked components have qualified widget IDs.
- Use event graphs instead of per-frame polling. Ui::find resolves widget IDs; Widget child/children, grow, visible, value, width, height and style are native operations.
## Segmented health bar recipe
Create a Row with gap and three child Rows for base health, temporary armour and shield. Set group grow equal to capacity (150,75,0 for example); hide groups with zero capacity. Keep base capacity fixed as damage is taken; update progress.value or segment tint/visibility to show current health. Shrink temporary armour capacity/weight as it expires and let base health fill the freed space. Divide capacity into 25 HP segments, including a partial final segment; handle zero total before dividing. Use a reusable segment with sliced artwork. Update on health changes, not by rebuilding every frame. This is an authoring recipe, not a prebuilt Blueprint asset.
Original Xbox: keep expanded authored pages within 256 widgets, use small textures and avoid per-frame allocation. Test overscan, controller navigation, video mode and GPU/RAM on real hardware.

## Editable example project
Choose **Health HUD** in the native project library. The template contains a linked widget page, editable theme, three slider Change callbacks, and two documented shared function graphs. Six permanent health slots and four slots for each temporary capacity are created once. The sliders demonstrate damage (permanent capacity stays 150), temporary armour and temporary shield (capacity and segment widths change together). Partial segments use relative Fill weight; base segments use progress. The demo does not implement gameplay damage rules or shield regeneration: connect your own game events to these graphs. No textures or external media required.

## Runtime Stack slots
Use Widget `Get anchor`, `Get offset` and `setAnchor(anchor, x, y)` Blueprint nodes. Supported point anchors are top-left, top-center, top-right, middle-left, center, middle-right, bottom-left, bottom-center, bottom-right; stretch-x, stretch-y and stretch-both use symmetric insets. An empty anchor clears offsets. A wrong parent or invalid value returns false without mutating layout. Existing grow, self, align, justify, pad and width/height nodes control other slot properties. These are native Redux Widget operations, not Unreal Slot cast objects.
