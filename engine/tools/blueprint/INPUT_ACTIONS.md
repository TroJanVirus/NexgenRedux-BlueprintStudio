# Input Action Editor

Open Scenes & Cinematics > Input Action Editor. Enter an action name such as Jump, a Redux key name and a controller button. Click Apply. Use the shared function Input_Jump in any scene or behaviour and connect its boolean result to a branch. The function reads either binding while held. Reapply the same name to change bindings; graph calls remain intact. Actions are ordinary project functions and travel with the document/package. Undo restores the previous binding.

This is the held-query foundation, not an enhanced-input runtime: there are no pressed/released events, axes, chords, multiple controller slots, or runtime remapping yet. The global Redux menu controls are not changed. Changing the generated graph topology removes it from the managed-action list rather than overwriting custom logic.
