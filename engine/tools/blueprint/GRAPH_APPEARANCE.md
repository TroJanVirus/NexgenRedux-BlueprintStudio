# Native graph appearance

Node widths use the native font's measured titles, pin labels and visible values (196–640 design units). Long values remain in the inspector. Layout > Arrange graph accounts for measured node widths; existing manually positioned graphs retain their coordinates, and Arrange is undoable.

Glyphs scale with zoom; titles remain displayed at the minimum 35% zoom. Pin labels are hidden at low zoom to reduce clutter. Pin padding scales with zoom. Node bodies have 83% opacity.

Layout > Wire style cycles Curved (default), Orthogonal and Straight and persists the choice. Curves use cubic Bézier sampling; backward connections without manual reroutes detour around the endpoint nodes. Manual reroutes override automatic detours. Other intervening nodes are not obstacle-routed. The same sampled geometry is used for reroute hit testing, preserving the original anchor interval.
