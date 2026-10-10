# DylanMayControl v1.0.1

DylanMayControl is a Devil May Cry-inspired directional-input combat overhaul and weapon swapping mod for CONTROL Resonant. The name is a play on Devil May Cry.

Release route: **Original DLL**. Build with `./scripts/native/build-plugin.sh`.

This patch lets one Split Pivot and one Split Orbit remain active simultaneously and survive other finishers. Native duration, expiry, cancels, same-move retrigger behavior, ownership and upgrades remain intact. See CHANGELOG.md.

## Install one route

Download the matching archive from [Nexus Mods](https://www.nexusmods.com/controlresonant/mods/181) and follow its INSTALLATION.txt and USER_GUIDE.txt.

- Main/f2g: `crmods/CombatLoadout/combatloadout.dll`; requires the separate [f2g DLL Mod Loader 1.0.0](https://www.nexusmods.com/controlresonant/mods/9). No loader is bundled.
- Optional/Original DLL: `xinput1_4.dll` beside `CONTROLResonant.exe`; f2g is not required for this mod.

Install only one DylanMayControl route. Remove the other copy when switching, preserve settings/presets and unrelated mods, and close the game before changing files.

## Source and build

Both branches contain the same 64 source files. The route changes only the build option and installation layout. See BUILDING.md; builds stay under ignored `.work/` and never install into the game.

- [f2g v1.0.1 source](https://github.com/Me3paw/DylanMayControl/tree/f2g-v1.0.1)
- [Original DLL v1.0.1 source](https://github.com/Me3paw/DylanMayControl/tree/original-dll-v1.0.1)

Published DLL SHA256:

- f2g: `4a29cc38f14758b4c4186fe4c7a7ceda15544fc0e202ab43fd87b8027ef500b3`
- Original DLL: `8ef95b46af7eb48529589cb78fe4aa8bfbe04608d99e520f9c17555b2c0a42b8`

Older tags `f2g-v1.0.0-build102` and `original-dll-v1.0.0-build103` preserve the v1.0.0 snapshots. SOURCE-SHA256SUMS.txt records this release source. Compiler/toolchain differences can change rebuilt DLL bytes.

## Verification

Both v1.0.1 routes passed build/export checks and bounded 15-second startup checks on Linux/Proton. The author confirmed the Split persistence fix working in gameplay with the Original DLL route. The f2g route contains the identical shared source fix but has not had a separate gameplay test. Native Windows gameplay has not been independently tested.

Possible stutter has been observed, but it is unmeasured and its cause is unconfirmed. Neither route has an established performance advantage.

## License and credits

The author's original code and documentation are MIT licensed, including commercial reuse with attribution and the copyright/license notice retained; see LICENSE and CREDITS.md.

**The MIT grant excludes `src/ui/weapon_icons.js` and its embedded CONTROL Resonant icons, other Remedy imagery, and gameplay screenshots.** Those remain subject to the rights holders and applicable fan-content rules. MinHook/HDE retain their separate BSD license in `third_party/minhook/LICENSE.txt`.

No game executable, compiler binaries, screenshots, saves, logs, personal settings or fetched tools are included. This is unofficial fan work not approved or endorsed by Remedy Entertainment Plc or its affiliates.
