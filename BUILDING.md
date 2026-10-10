# Building DylanMayControl

Build either supported route from the same source tree. The default is the original-DLL proxy route; `--plugin` builds the f2g plugin. This does not build/package the game, f2g, UI assets, screenshots or game installation files, and does not deploy output.

## Requirements

On Linux: Git, a C++20 host compiler (`g++`), Node.js, Python 3, LLVM-MinGW (`x86_64-w64-mingw32-clang` and `x86_64-w64-mingw32-clang++`) and Python package `pefile` if running export verification. The build script expects the compiler commands on PATH.

## Build the original-DLL proxy alternative

From the repository root:

    ./scripts/native/build-plugin.sh

This writes `.work/native-build/xinput1_4.dll`. Install this route only if you understand the root-DLL conflict and follow the optional Nexus file instructions. The proxy loads the system xinput1_4 backend and the mod's native service.

## Build the f2g plugin

    ./scripts/native/build-plugin.sh --plugin

This writes `.work/native-build/combatloadout.dll`, for installation as an f2g plugin.

## Optional validation helpers

    ./scripts/native/build-plugin.sh --with-helpers

This additionally compiles helper executables into ignored `.work/native-build/`; it does not run gameplay tests. The script checks out MinHook at commit `8af6b4acae5a9388fd742b56fa79ece89d96f823` under ignored `.work/upstream/minhook`. Its separate BSD terms are included in this repository.

Build reproducibility depends on host compiler/toolchain versions. The MinHook revision and source are pinned; resulting PE/DLL bytes may differ between compiler versions. Published artifact hashes are recorded in README.md and the corresponding Nexus release files.
