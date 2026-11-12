# Bundled Dear ImGui

This directory is tracked as ordinary source files, not a Git submodule.
Project generation and builds do not download dependencies.

- Upstream: https://github.com/ocornut/imgui
- Branch at import: docking
- Commit: b48d1afbe8ee8b238e2961dc363a949dd7304e23
- Header version: 1.92.9b
- License: MIT, retained in LICENSE.txt

The engine builds the core sources and the Win32/DirectX 11 backends through
premake5.lua. Existing upstream documentation and examples are retained.
To update, replace the source files together and update this provenance record.
