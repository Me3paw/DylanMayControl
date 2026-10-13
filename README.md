# DylanMayControl v1.1.1-fix

Directional-input combat and weapon swapping for CONTROL Resonant. This release includes both **f2g** and **native Original DLL** builds from the same source, with general and focused audio logging compiled out.

## Install one route

Supported CONTROLResonant.exe: FileVersion0.564.629.5, ProductVersion26.1.29.0, SHA256`9f56611d767c9227a8a1b250c01acf9478b50933c65f86a1e044fcc53df67cb1`. Other versions may be refused by the compatibility guards.

Close the game and preserve saves, bindings, configuration and other mods before replacing a DLL.

- **f2g:** extract `DylanMayControl-1.1.1-fix-f2g.zip` into the game folder. Its only file is `crmods/CombatLoadout/combatloadout.dll`. Requires the separately installed [f2g DLL Mod Loader1.0.0](https://www.nexusmods.com/controlresonant/mods/9). On Proton, use `winmm=n,b` alongside your existing overrides.
- **Native Original DLL:** extract `DylanMayControl-1.1.1-fix-OriginalDLL.zip` into the game folder. Its only file is root `xinput1_4.dll`. On Proton, use `xinput1_4=n,b` alongside your existing overrides.

Never load the native DylanMayControl DLL and its f2g CombatLoadout plugin together. Other f2g plugins may remain when using the native route; disable only the other copy of this mod. No loader, settings, source or tests are bundled in either install ZIP.

## Audio repair

Fixes cue startup by using the game's24-byte Wwise PCM media header instead of ordinary WAV headers. Each fresh configured keyboard/controller swap press posts a separate cue; earlier cues can overlap and finish. Chosen samples remain audition01 for Strike/Cleave/Crush/Drill/Extend and audition02 for Glaive, with the approved quiet lead-ins trimmed. Native SFX/master volume routing is retained.

The cue path does not write VFX state, call global audio stops, or change native switches/RTPCs. It retains the working shared global audio object and Player/Game/Master buses; native audio policies and voice budgets can still affect the overlay. Existing native selection/handoff flow, combat tuning, fixed80cm Enemy Step, controls and menu are retained.

The author confirmed the corrected cue behavior works in gameplay. The traced development build demonstrated six simultaneous advancing native sources and repeated-cue overlap; this release removes the diagnostics. Strict builds, export/resource/native-format tests and isolated input-free Proton startup checks validate the artifacts. Startup is not a controlled VFX regression or performance test; native Windows and separate native gameplay acceptance are not claimed.

## Source and builds

[Release/source tag](https://github.com/Me3paw/DylanMayControl/tree/v1.1.1-fix). See [BUILDING.md](BUILDING.md) and [CHANGELOG.md](CHANGELOG.md). Old tags/releases remain preserved. Nexus publication is handled separately by the author.

Both logging flags default to0: `CRMOD_DIAGNOSTICS=0`, `CRMOD_AUDIO_TRACE=0`. The installed release does not create cue trace logs or run the diagnostic startup cue test. Diagnostic source remains available behind explicit build flags.

## License and credits

Original code/docs are MIT licensed; see LICENSE and CREDITS.md. **Game-owned icons, audio and other Remedy assets, including src/ui/weapon_icons.js and src/audio/weapon_cues, are excluded from the MIT grant.** MinHook/HDE retain their BSD notices in third_party/minhook/LICENSE.txt. Development is AI Assisted; no AI-generated media is included. This is unofficial fan work, not endorsed by Remedy Entertainment Plc or its affiliates.
