# Build the optimized Original DLL

Requirements: a C++20 host compiler (g++), Node.js, Python3, Git and LLVM-MinGW x86_64-w64-mingw32-clang/clang++ on PATH. The build fetches pinned MinHook commit8af6b4acae5a9388fd742b56fa79ece89d96f823 under ignored .work/upstream/minhook; its separate BSD license is included.

Run ./scripts/native/build-plugin.sh from this directory. It creates .work/native-build/xinput1_4.dll and generated UI assets without installing or launching the game. Optional --with-helpers builds Win64 fixtures; it does not run them. Production diagnostics are explicitly compiled0. Compiler/toolchain differences can change binary bytes.

The source also retains the developer --plugin build option, but this release distributes/tests the Original DLL route only; do not treat a newly rebuilt f2g binary as independently gameplay verified. Existing public Main/f2g stays1.0.1.

The included test sources exercise native contracts through simulated events and protected-memory failures; they cannot reproduce full rendering/streaming or prove a gameplay FPS target. Source checksums are in SOURCE-SHA256SUMS.txt. src/ui/weapon_icons.js embeds game-owned images excluded from the MIT grant.

The supplemental test_entity_sdk.cpp fixture requires -municode and an explicit path argument to a compatible PhysXCommon_64.dll from your own game installation. No game binary is bundled and no local installation path is hardcoded.
