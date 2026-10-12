# Engine Python runtimes

This directory contains versioned, portable CPython runtimes shared by Engine tools. Each runtime directory includes the upstream license and a `runtime-manifest.json` with its source and archive checksum.

The Tracy MCP release build provisions `Python/3.11.17/` from the pinned `python-build-standalone` archive. Tool-specific Python dependencies stay with their owning tool instead of being installed into the shared runtime.
