# Portable Blueprint project packages

Use Layout > Export project ZIP to save a standard ZIP archive in own/ named blueprint-package-N.bp.zip. It contains the current editable NativeBlueprint JSON and all local per-screen and shared assets. It does not include the installed Redux global media, the application executable, or runtime save data. Save the document separately if desired.

Drop the .bp.zip on the graph workspace or choose it in Open. Import requires a clean current document, validates the archive and graph, creates an independent asset directory, then opens a recovered editable project. Save as gives it a managed project name. Existing projects and asset directories are never overwritten.

The initial format uses stored ZIP entries, 4096 assets, 64 MiB per entry and 256 MiB total payload. It rejects compressed, encrypted, ZIP64, duplicate, out-of-root or damaged archives and external asset roots. It is designed for Studio-generated packages, not arbitrary ZIP files. A failed restore due to disk errors can leave a new unused asset directory; the existing document is retained. Standard ZIP readers can inspect the package. Do not recompress it with a ZIP utility before reimporting.
