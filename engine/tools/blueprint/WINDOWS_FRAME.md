# Native window styling

The Windows frame bindings are exposed as typed Blueprint nodes. In On Enter, branch on Windows.titleColors before changing titleBar colours, corners or border; branch on Windows.backdrops before setting Mica or Acrylic. TitleLook supports System, Light and Dark. Title look is available on supported Windows 10 versions; custom colours and corners require Windows 11, and backdrops require Windows 11 build 22621 or newer. On other platforms capability getters report false and frame operations do not change the OS window.

Enum inputs now list valid engine tokens directly in the Details input editor. These choices come from the same reference as the API catalogue, not separate editor-only enums. Invalid tokens and injected expressions are rejected. Existing graphs retain their IDs.

Blueprint Studio itself uses a temporary dark Windows frame while authoring, with matching charcoal caption and a subtle border on Windows 11. This is separate from the game's script-owned title bar. Entering Play or the native scene editor suspends the override; returning to Blueprint Studio restores it. Leaving Studio removes the override. Other desktop platforms use their existing native chrome.

The editor does not force a game to use its own style. Exported projects use the normal Redux script API.
