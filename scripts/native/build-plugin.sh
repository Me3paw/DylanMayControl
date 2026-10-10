#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
for tool in git g++ node python3 x86_64-w64-mingw32-clang x86_64-w64-mingw32-clang++; do
  command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 1; }
done
BUILD=.work/native-build
MINHOOK=.work/upstream/minhook
MINHOOK_COMMIT=8af6b4acae5a9388fd742b56fa79ece89d96f823
mkdir -p "$BUILD" .work/upstream
if [[ ! -e "$MINHOOK" ]]; then
  git clone --no-checkout https://github.com/TsudaKageyu/minhook.git "$MINHOOK"
fi
if [[ ! -d "$MINHOOK/.git" ]]; then
  echo "$MINHOOK exists but is not a Git checkout; move it aside and retry." >&2
  exit 1
fi
git -C "$MINHOOK" fetch --depth=1 origin "$MINHOOK_COMMIT"
git -C "$MINHOOK" checkout --detach "$MINHOOK_COMMIT"
g++ -std=c++20 -O2 -Wall -Wextra -Werror scripts/native/emit_ui_bootstrap.cpp -o "$BUILD/emit_ui_bootstrap"
"$BUILD/emit_ui_bootstrap" > "$BUILD/loadout-bootstrap.js"
node --check "$BUILD/loadout-bootstrap.js"
python3 -B - <<'PY'
from pathlib import Path
script = Path('.work/native-build/loadout-bootstrap.js').read_text()
assert '\0' not in script and ')CR_UI_ASSET"' not in script
Path('.work/native-build/loadout_assets.h').write_text(
    '#pragma once\ninline constexpr char cr_loadout_script[]=R"CR_UI_ASSET(' + script + ')CR_UI_ASSET";\n')
PY
objects=()
for unit in buffer hook trampoline hde/hde64; do
  object="$BUILD/minhook-${unit##*/}.o"
  x86_64-w64-mingw32-clang -O2 -c "$MINHOOK/src/$unit.c" -o "$object"
  objects+=("$object")
done
x86_64-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -static -DCRMOD_PLUGIN -I"$MINHOOK/include" -shared src/native/xinput_proxy.cpp src/native/plugin.def "${objects[@]}" -lws2_32 -ladvapi32 -o "$BUILD/combatloadout.dll"
sha256sum "$BUILD/combatloadout.dll"
echo "Built $BUILD/combatloadout.dll; no game files were changed."
