# C# Project Integration Verification

The project integration build uses the Engine CMake entry point:

```powershell
cmake -S Engine -B Engine/Build -DURSA_ENABLE_DOTNET=ON -DURSA_PROJECT_DIR=Demo
cmake --build Engine/Build --parallel --config Debug --target UrsaEditor UrsaScriptPlayer UrsaAssetImport UrsaPak
ctest --test-dir Engine/Build -C Debug --output-on-failure -R 'Ursa(Project|Snake)'
```

The current Debug integration suite covers managed Snake logic, project script compilation, loose and pak Player startup, gameplay pause/resume/restart, generic Editor scene opening, Scene/Prefab save and reopen with stable IDs, material sample rendering, editor extension registration/reload, texture/model import, and project packaging. `UrsaProjectPlaySessionTests` verifies that an unsaved C# field value reaches the play clone and that Stop leaves the edit document unchanged.

The full Debug/Release matrix with scripting both enabled and disabled is still tracked in `openspec/changes/csharp-project-boundary/tasks.md`. A hidden window smoke test checks startup and rendering paths; it does not replace manual verification of window interaction on a desktop session.
