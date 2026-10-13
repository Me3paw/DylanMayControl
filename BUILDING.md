# Build DylanMayControl v1.1.1-fix

Requirements: C++20 g++, Node.js, Python3 with pefile, Git, and LLVM-MinGW x86_64-w64-mingw32-clang/clang++/windres on PATH. The portable builder fetches pinned MinHook commit8af6b4acae5a9388fd742b56fa79ece89d96f823; its BSD notice is retained.

From the checkout root:

```sh
bash scripts/native/build.sh --plugin --combat-v1
bash scripts/native/build.sh --combat-v1
```

Outputs are `.work/native-build/combat-v1-optimized/combatloadout.dll` and `xinput1_4.dll`. Both commands compile general and cue diagnostics out. They generate the native PCM bank/UI assets locally and never install or launch a game. Link timestamps/build IDs can change rebuilt DLL hashes; release manifests identify the tested binaries.

Focused source checks:

```sh
python3 scripts/native/test_weapon_cue_media.py
mkdir -p .work/native-build
g++ -std=c++20 -O2 -Wall -Wextra -Werror -Isrc/native scripts/native/test_loadout_control.cpp -o .work/native-build/test_loadout_control
.work/native-build/test_loadout_control
```

Windows native-audio helper:

```sh
x86_64-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -Werror -DCRMOD_AUDIO_TRACE=0 -static scripts/native/test_loadout_queue_audio.cpp scripts/native/test_loadout_queue_audio.def .work/native-build/combat-v1-optimized/weapon_cues.o -o .work/native-build/test_loadout_queue_audio.exe
```

This helper uses audio API test doubles and is not a live-game sound/VFX/performance test. Execute it only in your own test environment.

Optional `--audio-trace` enables bounded private cue diagnostics. Do not combine it with `--diagnostics`. These options are off in both published binaries. The optional six-cue startup self-check requires an explicitly enabled traced build and process-local environment flag; release builds compile it out.

SOURCE-SHA256SUMS.txt covers source/tests/UI/audio. Game-owned icons/audio are excluded from MIT. No game executable, runtime engine DLL, saves, diagnostic recordings or compilers are published.
