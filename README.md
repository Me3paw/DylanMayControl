# DylanMayControl

DylanMayControl is a Devil May Cry-inspired directional-input combat overhaul and weapon swapping mod for CONTROL Resonant. The name is a play on Devil May Cry.

This repository contains source for two separately distributed routes. The exact build102 f2g source snapshot is preserved at tag `f2g-v1.0.0-build102`; `main` contains the original-DLL proxy source for the optional xinput1_4.dll alternative. The Nexus page has both release files: [DylanMayControl on Nexus Mods](https://www.nexusmods.com/controlresonant/mods/181).

## Source and license

The author's original code and documentation are MIT licensed. You may reuse, modify, redistribute, and use that covered material commercially with attribution; keep the copyright and license notice. See LICENSE.

**The MIT grant does not cover src/ui/weapon_icons.js or its embedded six CONTROL Resonant weapon icons.** Those images are Remedy Entertainment/game assets and remain subject to their rights and fan-content rules. Do not extract, reuse, or redistribute them separately under this repository's MIT grant. This file is retained only as part of this free, unofficial fan-work source. This is not approved or endorsed by Remedy Entertainment Plc or its affiliates.

MinHook is a third-party dependency under its own BSD license; see third_party/minhook/LICENSE.txt. Its terms remain in force. No game binaries, compiler binaries, screenshots, saves, logs, private comparison experiments, or downloaded UI bundles are included.

## Build

See BUILDING.md for requirements and commands. The default command builds the root `xinput1_4.dll` proxy alternative. Pass `--plugin` to build the f2g plugin instead. Build outputs stay under ignored `.work/`; the script does not install or modify the game.

## Release provenance

The original f2g v1.0.0 build102 `combatloadout.dll` SHA256 is `3f703860d3f86f751cfd9c7357a0011b46f16d8550d1ae6fd6a4343df7f6f773`; its source snapshot and all 63 expected file hashes are preserved by tag `f2g-v1.0.0-build102`. The optional original-DLL proxy route is separately tagged `original-dll-v1.0.0-build103`; its `xinput1_4.dll` SHA256 is `7c673c80a864ffbc7baca511892d0edc5bec5cf9c9078f915a5760d8c65840ff`. See that Nexus optional file for install details.

The optional proxy passed startup/helper checks and displayed the Remedy logo during a short boot check. Controller/combat play and performance comparison are still pending; no stutter improvement has been established. The original f2g release was tested on Linux with Proton and keyboard/controller input; native Windows gameplay has not been independently tested.
