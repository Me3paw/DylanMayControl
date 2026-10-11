# DylanMayControl v1.0.3

DylanMayControl is a Devil May Cry-inspired directional-input combat overhaul and weapon swapping mod for CONTROL Resonant.

Current route: **native Original DLL (Optional alternative)**. Both variants use identical shared native/UI source. Choose one route; never load both copies of this mod.

## Compatibility and installation

Supported CONTROLResonant.exe: FileVersion 0.564.629.5; ProductVersion 26.1.29.0; SHA256 9f56611d767c9227a8a1b250c01acf9478b50933c65f86a1e044fcc53df67cb1. v1.0.3 targets this exact updated executable. Earlier 1.0.2 native and 1.0.1 f2g releases target 0.563.737.9. Other executable revisions may be refused by the native guards.

- Optional native Original DLL: extract `DylanMayControl-1.0.3-OriginalDLL.zip` beside CONTROLResonant.exe; it installs root xinput1_4.dll. No f2g requirement. On Proton use `xinput1_4=n,b` in your game's DLL overrides.
- Main/default f2g: extract `DylanMayControl-1.0.3.zip` to install crmods/CombatLoadout/combatloadout.dll; requires the separate [f2g DLL Mod Loader 1.0.0](https://www.nexusmods.com/controlresonant/mods/9). No loader is bundled. On Proton use `winmm=n,b`.

Close the game before changing DLLs. Preserve crmod-bindings.ini and crmod.ini, saves and unrelated mods. When switching routes, back up/remove only the other DylanMayControl DLL; f2g may remain for unrelated plugins. Merge overrides with existing launcher settings. Follow INSTALLATION.txt and USER_GUIDE.txt in the release archive.

## Features and verification

Three primary/secondary slots, custom cycle bindings, queued native weapon handoffs, held lock and target-relative finisher commands, sustained charged Uppercut targeting, one owned Split Pivot/Orbit each, and adjustable resource loot. Native movement, attack pace, cancels, acquisition and per-weapon upgrades remain enforced. See CHANGELOG.md and BUILDING.md.

Both routes passed build/export and isolated 15-second Linux/Proton startup checks. The author tested the native Original DLL route on the updated game and reported no obvious regression. The f2g route uses the same shared source and passed startup checks; separate f2g gameplay and native Windows gameplay have not been independently tested.

Production diagnostic logging is compiled out in both routes. The functionality/performance source and binary audit found no blocking issue or new steady-state regression in reviewed paths. This is not a controlled current-build FPS or 1% low measurement.

## Source and release

- [Native Original DLL v1.0.3](https://github.com/Me3paw/DylanMayControl/tree/original-dll-v1.0.3)
- [f2g v1.0.3](https://github.com/Me3paw/DylanMayControl/tree/f2g-v1.0.3)
- [Nexus Mods](https://www.nexusmods.com/controlresonant/mods/181)

DLL SHA256: `2c7ac6c9adf0d983bef3fe42d8f0311768e503b9869f3a46aa70537ec026063f`. The source in each archive is flat under SOURCE/, with SOURCE-SHA256SUMS.txt. Older tags and Nexus files retain prior-build releases.

## License and credits

Original code/docs are MIT licensed, including commercial reuse with attribution and retained notices; see LICENSE and CREDITS.md. **src/ui/weapon_icons.js, embedded Remedy icons, other game imagery and gameplay screenshots are excluded from the MIT grant.** MinHook/HDE retain the BSD notice in third_party/minhook/LICENSE.txt. No game binaries, saves, logs, personal settings, fetched tools or compilers are included. This is unofficial fan work, not approved or endorsed by Remedy Entertainment Plc or its affiliates.
