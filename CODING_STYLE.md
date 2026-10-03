# C++ code style

Engine and Demo use their own matching `.clang-format` and `.editorconfig` files. The layout borrows familiar C++ conventions used by Unreal Engine without adopting its full naming system:

- Use tabs for indentation with a display width of four; use spaces only to align continued expressions.
- Save first-party C++ files as UTF-8 with LF line endings.
- Put opening braces on a new line for functions, types, namespaces, and control blocks. Keep braces around single-statement branches and loops.
- Leave one blank line between method definitions. Keep lines within 120 columns when practical.
- Attach `*` and `&` to the type (`Node*`, `const String&`). Keep include order intentional.
- Format only first-party C++ sources. Do not format `third_party`, generated build output, shaders, or serialized assets.

Use clang-format 19 with the repository's `tools/format.ps1`, or pass `-Check` to verify without edits. The script finds Visual Studio's clang-format when it is not in `PATH`, and accepts `-ClangFormatPath` for another installation. Run it separately in Engine and Demo. VS Code formats C++ on save using each repository's `.clang-format`.

The Engine CMake project exposes `UrsaEngineFormatCheck`. A Demo build exposes `UrsaDemoFormatCheck` and `UrsaFormatCheck`; the last target checks both repositories. These targets run only when selected and do not change source files.

## Naming

- Use PascalCase for types, methods, and free functions (`Scene`, `GetScene`, `BuildScene`).
- Use PascalCase for namespaces (`Ursa`, `Ursa::Rhi`, `Ursa::Editor`, `Ursa::Demo`).
- Use camelCase for fields, parameters, and local variables (`activeCamera`, `deltaSeconds`). Boolean values do not need a special prefix.
- Do not add category prefixes such as `F`, `E`, `T`, or `I` to type names. Use `URSA_` followed by uppercase words for macros.
- Keep spelling required by third-party hooks and APIs at integration boundaries.
- Keep serialized JSON keys, persistent component type strings, resource IDs, and file extensions stable when renaming C++ symbols.

Renaming existing public symbols changes downstream source code. Coordinate such migrations across Engine, Demo, tests, and examples rather than leaving two spellings of the same API.
