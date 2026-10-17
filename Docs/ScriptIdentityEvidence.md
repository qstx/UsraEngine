# Script identity migration verification

The metadata-backed script identity change was verified on Windows with the Release configuration.

## Results

- `ctest --test-dir Engine/Build -C Release -R '^(UrsaAssetDatabaseTests|UrsaScriptIdentityTests|UrsaScriptBuildTests|UrsaDotNetHostSmoke|UrsaManagedEditorWorkflowTests)$' --output-on-failure` passed all 6 tests (including test-content preparation), in the final rebuild 141.47 seconds. This covers sidecar preservation/copy behavior, legacy and automatic identity migration, stale writes and failed discovery, formal build invalidation, native session catalog resolution, and three Editor Run/Stop cycles with a class/namespace rename and field preservation.
- `cmake --build Engine/Build/BindingsFinalVerify --config Debug --target UrsaAssetDatabaseTests UrsaScriptIdentityTests UrsaScriptBuildTests UrsaManagedEditorWorkflowTests UrsaDotNetHostTests UrsaEditor -- /m:1 /nr:false` rebuilt all affected Debug targets, and the same focused Debug CTest selection passed all 6 tests in 157.95 seconds. The primary build directory had stale MSVC PDB state (`C1902` in third-party sources), so this verification used the separate `BindingsFinalVerify` build tree.
- `UrsaScriptCompile Demo Debug` reported that script assemblies were up to date. `UrsaScriptCompile Demo Release` built the Editor script assembly with 0 warnings and 0 errors.
- `UrsaAssetImport --project Demo --configuration Release --profile Both` imported the Editor and Player profiles. `UrsaScriptPlayer --project Demo --scene Snake/Snake.ursascene --hidden --frames 4` started the Vulkan renderer, initialized the managed runtime, and exited successfully. The Player check used a fresh workspace-local `URSA_USER_DATA_ROOT` to avoid an access-denied error in an older generated test user-data directory.
- With managed scripting disabled and Editor/tests enabled, `cmake --build Engine/Build/BindingsFinalOff --config Release --target UrsaAssetDatabaseTests -- /m:1 /nr:false` succeeded and the Release `UrsaAssetDatabaseTests` CTest passed.
- The Demo migration changed exactly six `.cs` files and their six `.meta` sidecars among the 64 files in the pre-migration source/asset hash snapshot. The 37 asset GUIDs in the earlier inventory are unchanged; all seven old script IDs, versions, and local file IDs match the new sidecars. The Scene, Prefab, and ScriptableObject files containing those serialized references are untouched. The source changes only remove the `UrsaScriptType` declarations.
- clang-format 19.1.1 formatting is idempotent for all 10 changed first-party C++ files.

