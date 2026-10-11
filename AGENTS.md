# Code style

- Follow `.clang-format`, `.editorconfig`, and `CODING_STYLE.md` for first-party C++.
- Use clang-format 19. Run `Tools/Format.ps1 -Check` or build the `UrsaEngineFormatCheck` target after changing C++.
- Keep a blank line between method definitions. Keep opening braces on their own lines and braces around control-flow bodies.
- Use PascalCase for namespaces, types, enumeration values, and methods and camelCase for fields, parameters, and local variables, as specified in `CODING_STYLE.md`. Preserve serialized names and resource IDs when renaming C++ symbols.
- Do not format `ThirdParty`, generated assets, or build output.
