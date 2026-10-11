# DylanMayControl v1.0.3

- Adds compatibility with CONTROL Resonant 0.564.629.5 for both native Original DLL and f2g routes.
- Updates native function/data references and graph-node/execution layouts; keeps exact executable, UI and ownership guards.
- Fixes an ambiguous UI listener signature by checking the complete native call.
- Brings f2g up to the optimized quiet production source used by the native route.
- Retains weapon slots/bindings, native queued handoffs, target-relative attacks, sustained charged Uppercut targeting, Split coexistence and resource loot settings.
- Preserves native movement, attack pace, cancels, acquisition checks and individual weapon upgrades.

Both routes passed build/export and isolated 15-second Linux/Proton startup checks. The author tested the native Original DLL route on the updated game and reported no obvious regression. The f2g route uses the same shared source and passed startup checks; separate f2g gameplay and native Windows gameplay have not been independently tested.

Production diagnostic logging is compiled out in both routes. The functionality/performance source and binary audit found no blocking issue or new steady-state regression in reviewed paths. This is not a controlled current-build FPS or 1% low measurement.

Supported CONTROLResonant.exe: FileVersion 0.564.629.5; ProductVersion 26.1.29.0; SHA256 9f56611d767c9227a8a1b250c01acf9478b50933c65f86a1e044fcc53df67cb1. v1.0.3 targets this exact updated executable. Earlier 1.0.2 native and 1.0.1 f2g releases target 0.563.737.9. Other executable revisions may be refused by the native guards.
