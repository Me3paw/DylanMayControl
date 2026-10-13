# Build DylanMayControl v1.1.1

Requirements: host C++20 g++, Node.js, Python 3, Git and LLVM-MinGW x86_64-w64-mingw32-clang/clang++ on PATH. Optional real-binary pin validation requires Python pefile. The build fetches MinHook commit 8af6b4acae5a9388fd742b56fa79ece89d96f823; its BSD notice is included.

From the repository root:

```sh
bash scripts/native/build.sh --plugin --combat-v1
bash scripts/native/build.sh --combat-v1
```

The first command builds the recommended `.work/native-build/combat-v1-optimized/combatloadout.dll` (f2g); the second builds the optional `.work/native-build/combat-v1-optimized/xinput1_4.dll` (native Original DLL). Both compile production diagnostics out. The script generates the Wwise PCM bank and UI assets under ignored `.work/` and never installs or launches the game. Compiler/toolchain differences can change rebuilt DLL bytes. The native route is retained as an optional source build; this 1.1.1 publication updates the f2g route only.

Focused layout regression:

```sh
mkdir -p .work/native-build
g++ -std=c++20 -O2 -Wall -Wextra -Werror -Isrc/native scripts/native/test_loadout_current_node.cpp -o .work/native-build/test_loadout_current_node
.work/native-build/test_loadout_current_node
python3 scripts/native/test_game_pins.py /path/to/your/CONTROLResonant.exe
```

The exact executable pin test reads your own game binary and never runs it. The supplemental PhysX fixture requires `-municode` and a path to your own compatible PhysXCommon_64.dll. Fixtures do not prove rendering performance or full gameplay.

SOURCE-SHA256SUMS.txt covers code/test/UI/audio source. Game-owned icons in src/ui/weapon_icons.js and the Remedy audio samples in src/audio/weapon_cues are excluded from the MIT grant. No game executable or runtime engine DLL is included.
