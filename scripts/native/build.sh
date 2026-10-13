#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
plugin=false
diagnostics=0
combat_v1=0
for flag in "$@"; do
    case "$flag" in
        --plugin) plugin=true ;;
        --diagnostics) diagnostics=1 ;;
        --combat-v1) combat_v1=1 ;;
        *) echo 'Usage: build.sh [--plugin] [--diagnostics] [--combat-v1]' >&2; exit 2 ;;
    esac
done
mode=optimized
if ((diagnostics));then mode=diagnostic;fi
if ((combat_v1));then mode="combat-v1-$mode";fi
build_dir=".work/native-build/$mode"
mkdir -p "$build_dir"
for tool in git g++ node python3 x86_64-w64-mingw32-windres x86_64-w64-mingw32-clang x86_64-w64-mingw32-clang++; do
    command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 1; }
done
minhook=.work/upstream/minhook
minhook_commit=8af6b4acae5a9388fd742b56fa79ece89d96f823
mkdir -p .work/upstream
if [[ ! -e "$minhook" ]]; then
    git clone --no-checkout https://github.com/TsudaKageyu/minhook.git "$minhook"
fi
if [[ ! -d "$minhook/.git" ]]; then
    echo "$minhook exists but is not a Git checkout; move it aside and retry." >&2
    exit 1
fi
git -C "$minhook" fetch --depth=1 origin "$minhook_commit"
git -C "$minhook" checkout --detach "$minhook_commit"
python3 -B scripts/native/build_weapon_cues.py "$build_dir"
x86_64-w64-mingw32-windres "$build_dir/weapon_cues.rc" -o "$build_dir/weapon_cues.o"
g++ -std=c++20 -O2 -Wall -Wextra -Werror -DCRMOD_DIAGNOSTICS="$diagnostics" scripts/native/emit_ui_bootstrap.cpp -o "$build_dir/emit_ui_bootstrap"
"$build_dir/emit_ui_bootstrap" > "$build_dir/loadout-bootstrap.js"
node --check "$build_dir/loadout-bootstrap.js"
python3 -B - "$build_dir" <<'PY'
from pathlib import Path
import sys
build=Path(sys.argv[1])
script=(build/'loadout-bootstrap.js').read_text()
assert '\0' not in script and ')CR_UI_ASSET"' not in script
(build/'loadout_assets.h').write_text(
    '#pragma once\ninline constexpr char cr_loadout_script[]=R"CR_UI_ASSET(' + script + ')CR_UI_ASSET";\n')
PY
minhook_objects=()
for unit in buffer hook trampoline hde/hde64; do
    object="$build_dir/minhook-${unit##*/}.o"
    x86_64-w64-mingw32-clang -O2 -c "$minhook/src/$unit.c" -o "$object"
    minhook_objects+=("$object")
done
definition=src/native/xinput_proxy.def
output="$build_dir/xinput1_4.dll"
defines=(-DCRMOD_DIAGNOSTICS="$diagnostics" -DCRMOD_COMBAT_V1="$combat_v1" -I"$build_dir")
if "$plugin"; then
    definition=src/native/plugin.def
    output="$build_dir/combatloadout.dll"
    defines+=(-DCRMOD_PLUGIN)
fi
x86_64-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -static -I"$minhook/include" -shared "${defines[@]}" src/native/xinput_proxy.cpp "$definition" "${minhook_objects[@]}" "$build_dir/weapon_cues.o" -lws2_32 -ladvapi32 -o "$output"
python3 -B - "$output" "$plugin" <<'PY'
from pathlib import Path
import pefile,sys
p=Path(sys.argv[1])
pe=pefile.PE(p)
exports={s.ordinal:s.name.decode() if s.name else None for s in pe.DIRECTORY_ENTRY_EXPORT.symbols}
required={2:'XInputGetState',3:'XInputSetState',4:'XInputGetCapabilities',5:'XInputEnable',7:'XInputGetBatteryInformation',8:'XInputGetKeystroke',10:'XInputGetAudioDeviceIds',200:'CrModStop'}
if sys.argv[2]=='true':required={200:'CrModStop'}
assert exports==required, exports
assert pe.FILE_HEADER.Machine==0x8664
print('Win64 diagnostic exports verified:',exports)
print('Imports:',[d.dll.decode() for d in pe.DIRECTORY_ENTRY_IMPORT])
PY
