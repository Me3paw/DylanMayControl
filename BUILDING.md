# Building the v1.0.0 plugin

This builds the f2g plugin DLL from the public build102 source snapshot. It does not build or package f2g, the game, game UI files, screenshots, or any game installation assets. It does not deploy the output.

## Requirements

On a Linux host, install Git; a C++20 host compiler (g++); Node.js; Python 3; and the LLVM-MinGW cross-toolchain commands x86_64-w64-mingw32-clang and x86_64-w64-mingw32-clang++. The script expects these on PATH.

## Build

From the repository root, run:

    ./scripts/native/build-plugin.sh

The script checks out MinHook at commit 8af6b4acae5a9388fd742b56fa79ece89d96f823 under ignored .work/upstream/minhook, builds the required objects, generates the UI asset header from the included emit_ui_bootstrap.cpp, and writes .work/native-build/combatloadout.dll. The MinHook source is fetched from its upstream Git repository at build time; its separate BSD terms are included in this repository.

Build reproducibility depends on the host compiler/toolchain versions. The source snapshot and dependency revision are pinned; the resulting PE/DLL bytes may differ between compiler versions. The published release DLL hash is recorded in README.md.
