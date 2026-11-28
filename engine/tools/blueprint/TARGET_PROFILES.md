# Project target profiles (initial implementation)

New and legacy native projects default to original Xbox, 64 MiB unified RAM, 640 x 480. Build & Package contains platform, Xbox RAM and resolution selectors. Windows, Linux, macOS, Xbox 360 and UWP (Xbox One/Series Developer Mode and Windows) are selectable metadata, not claims of platform feature parity. Xbox 360 and UWP console hardware are not certified.

Original Xbox reference: 733 MHz CPU, 64 MiB unified DDR, nominal 200 MHz clock / 400 MT/s, 128-bit bus, 6.4 GB/s theoretical peak. 128 MiB and experimental 256 MiB profiles do not imply stock compatibility or a known memory clock. The 256 MiB profile warns about possible reduced bandwidth; actual clocks depend on the modification.

Widget preview now uses the saved target resolution (640x480, 720x480 anamorphic 16:9, 1280x720 or 1920x1080). The latter is an authoring size for OG Xbox 1080i, not 1080p capability. The anamorphic canvas displays at the intended aspect with matching pointer transforms. Resolution selection does not configure the runtime video mode yet.

The build manager compiles and validates AngelScript and displays an illustrative surface cost (two 32-bit colour buffers plus one 32-bit depth buffer, excluding pitch, MSAA, tiling, runtime and assets). A successful script check never implies hardware fit. The desktop profiler uses the selected RAM reference, not console measurements. A target-aware asset dependency/cooked-size and capability audit, per-scene resource budgets, device deployment, real-console telemetry, runtime video configuration, and native hardware verification are follow-up work. No CPU/GPU/FPS predictions are made.
