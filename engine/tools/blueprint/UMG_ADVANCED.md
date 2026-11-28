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
