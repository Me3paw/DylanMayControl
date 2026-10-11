# Build DylanMayControl v1.0.3

Requirements: host C++20 g++, Node.js, Python 3, Git and LLVM-MinGW x86_64-w64-mingw32-clang/clang++ on PATH. Optional real-binary pin validation requires Python pefile. The build fetches MinHook commit 8af6b4acae5a9388fd742b56fa79ece89d96f823; its BSD notice is included.

From the repository root:

```sh
./scripts/native/build-plugin.sh
./scripts/native/build-plugin.sh --plugin
```

The first command builds `.work/native-build/xinput1_4.dll` (native Original DLL); the second builds `.work/native-build/combatloadout.dll` (f2g). Both compile production diagnostics out. `--with-helpers` additionally compiles Win64 fixture helpers without running them. The script generates UI assets under ignored `.work/` and never installs or launches the game. Compiler/toolchain differences can change rebuilt DLL bytes.

Focused layout regression:

```sh
mkdir -p .work/native-build
g++ -std=c++20 -O2 -Wall -Wextra -Werror -Isrc/native scripts/native/test_loadout_current_node.cpp -o .work/native-build/test_loadout_current_node
.work/native-build/test_loadout_current_node
python3 scripts/native/test_game_pins.py /path/to/your/CONTROLResonant.exe
```

The exact executable pin test reads your own game binary and never runs it. The optional `api_self_check.exe --event-replay` uses synthetic native events, including protected-memory boundary failures and the updated execution-row stride. Fixtures do not prove rendering performance or full gameplay. The supplemental PhysX fixture requires `-municode` and a path to your own compatible PhysXCommon_64.dll.

SOURCE-SHA256SUMS.txt covers code/test/UI source. Game-owned icons in src/ui/weapon_icons.js are excluded from the MIT grant. No game executable or runtime engine DLL is included.
