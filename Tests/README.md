# Running tests

Run these commands from the Engine directory after building the selected configuration:

```powershell
ctest --test-dir Build -C Release -L fast --output-on-failure
ctest --test-dir Build -C Release -L slow --output-on-failure
ctest --test-dir Build -C Release --output-on-failure
```

Every registered test belongs to exactly one profile:

- `fast`: CPU tests with no resource preparation, GPU, window, import pipeline, or external build requirement.
- `slow`: extended regression, including GPU, window, project integration, compiler/generator, and fixture work.

Profiles describe execution requirements, not a guaranteed duration. Additional labels select
`gpu`, `window`, `import`, `build`, `integration`, or `fixture`; directory labels such as
`Tests/Engine/Scene` remain available. For example:

```powershell
ctest --test-dir Build -C Release -L gpu --output-on-failure
ctest --test-dir Build -C Debug -L '^Tests/Engine/Scene$' --output-on-failure
ctest --test-dir Build -C Release -L fast --show-only
```

CTest adds required fixture setup automatically. Keep fixture dependencies and resource locks
when changing a test's profile. `TestProfiles.cmake` classifies tests during
`ursa_finalize_test_directory()`; call that helper after setting fixture requirements.
Update its resource lists when adding GPU or window tests, including tests with indirect device creation.

Build and project integration scripts share `URSA_POWERSHELL_EXECUTABLE`. CMake prefers
`pwsh` and falls back to Windows PowerShell; an existing cache selection can be changed
with `-DURSA_POWERSHELL_EXECUTABLE=<path>` when configuring.

Public header examples share `UrsaPublicHeaderSmoke`. Each example remains a separate
translation unit with its original includes and runtime checks; the runner reports failed
examples by name. Editor examples are included only when `URSA_BUILD_EDITOR=ON`.

Logging tests use `ursa_add_output_test` to check the process exit status and every required
output pattern in one execution. Avoid registering another invocation merely to check another
line of output.

The .NET host smoke test keeps one process and its session order while its scenarios live in
`Integration/DotNetHost/{Rendering,PipelineReload,Serialization,Editor}.cpp`. This preserves
CoreCLR's process lifetime and the final unload-failure checks.

Benchmarks are excluded from the default build and CTest suite. Build them explicitly:

```powershell
cmake --build Build --config Release --target UrsaMemoryBenchmarks UrsaMemoryWorkloadBenchmarks
```
