# Portable Blueprint project packages

Use Layout > Export project ZIP to save a standard ZIP archive in own/ named blueprint-package-N.bp.zip. It contains the current editable NativeBlueprint JSON and all local per-screen and shared assets. It does not include the installed Redux global media, the application executable, or runtime save data. Save the document separately if desired.

Drop the .bp.zip on the graph workspace or choose it in Open. Import requires a clean current document, validates the archive and graph, creates an independent asset directory, then opens a recovered editable project. Save as gives it a managed project name. Existing projects and asset directories are never overwritten.

The initial format uses stored ZIP entries, 4096 assets, 64 MiB per entry and 256 MiB total payload. It rejects compressed, encrypted, ZIP64, duplicate, out-of-root or damaged archives and external asset roots. It is designed for Studio-generated packages, not arbitrary ZIP files. A failed restore due to disk errors can leave a new unused asset directory; the existing document is retained. Standard ZIP readers can inspect the package. Do not recompress it with a ZIP utility before reimporting.

## Bitmap fonts

Import a single-page text BMFont `.fnt` with the atlas named in its `page` entry. Stage both files in Redux's `own/` directory (or drop both into the asset browser); import the descriptor after the atlas is available. Studio puts both in `asset:font/` with their original portable names. Select `asset:font/<name>.fnt` in `Assets::font`. The descriptor uses the original Redux parser; incompatible page names, missing atlases, mismatched dimensions and conflicting existing files are rejected. An identical atlas may be shared by multiple descriptors. A failed disk write may leave an unused atlas, but not a newly imported descriptor without its atlas. Normal project export and portable ZIP packaging retain the pair. Image-strip fonts continue to use `Assets::imageFont` and ordinary image assets.
