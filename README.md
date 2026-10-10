# DylanMayControl

DylanMayControl is a Devil May Cry-inspired directional-input combat overhaul and weapon swapping mod for CONTROL Resonant. The name is a play on Devil May Cry.

This repository publishes the exact 63-file src/ snapshot recorded for the public build102 / v1.0.0 release. The Nexus download remains the release artifact: [DylanMayControl on Nexus Mods](https://www.nexusmods.com/controlresonant/mods/181).

## Source and license

The author's original code and documentation are MIT licensed. You may reuse, modify, redistribute, and use that covered material commercially with attribution; keep the copyright and license notice. See LICENSE.

**The MIT grant does not cover src/ui/weapon_icons.js or its embedded six CONTROL Resonant weapon icons.** Those images are Remedy Entertainment/game assets and remain subject to their rights and fan-content rules. Do not extract, reuse, or redistribute them separately under this repository's MIT grant. This file is retained only as part of this free, unofficial fan-work source snapshot. This is not approved or endorsed by Remedy Entertainment Plc or its affiliates.

MinHook is a third-party dependency under its own BSD license; see third_party/minhook/LICENSE.txt. Its terms remain in force. No game binaries, compiler binaries, screenshots, saves, logs, private comparison experiments, or downloaded UI bundles are included.

## Build

See BUILDING.md for the pinned MinHook dependency, toolchain requirements, and a local source build command. The build creates files under ignored .work/; it does not install or modify the game.

## Release provenance

The published v1.0.0 DLL is build102, SHA256 3f703860d3f86f751cfd9c7357a0011b46f16d8550d1ae6fd6a4343df7f6f773. Source file hashes are listed in SOURCE-SHA256SUMS.txt and correspond to the build102 manifest used to approve the release. The release was tested on Linux with Proton and keyboard/controller input; native Windows gameplay has not been independently tested.
