# Editor icons

These 55 icons are original Ursa interface artwork. `Sources/*.svg` contains the
24-unit vector originals generated from the primitives in `Tools/GenerateEditorIcons.py`.
They do not contain Unity artwork. They are distributed under the same license
as the first-party Ursa Editor source.

Run `python Tools/GenerateEditorIcons.py` with Python 3 and Pillow to regenerate
the SVG originals, `EditorIcons.png`, and `EditorIcons.json`. Run with `--check`
to verify byte-for-byte reproducibility. No generator dependency is required to
build or run the Editor. Generated resource metadata uses deterministic GUIDs;
existing metadata is never overwritten.

The atlas has eight columns of 48-pixel cells. Each icon has six transparent
pixels of padding around a 36-pixel drawing. UVs include this padding; the UI
helper compensates when drawing 16-unit row icons or 18-unit toolbar icons.
The rasterizer draws at 4x and downsamples with Lanczos filtering. Colors and
active/disabled appearance are applied by the UI, not baked into the artwork.

Engine resources load through `ursa:Icons/Editor/EditorIcons.png` and the matching
JSON manifest in both loose and packaged content. The manifest validates every
semantic name, nonempty UV rectangle, and pairwise non-overlap. A missing atlas
or invalid manifest produces one initialization warning and readable text
controls. Custom types use a generic icon; project icon assets are not required.

Use `Ui/EditorIcons.hpp` from native panels. Keep object/asset/component identity
independent of labels and icon names. Preserve existing dock window names.
Use `Icons::SameLine` for rows that need to wrap in narrow panels. Pure icon
buttons require a readable tooltip label; confirmation and destructive commands
retain visible text. Registered managed UI remains compatible and does not gain
an icon-registration requirement.
