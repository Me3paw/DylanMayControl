DylanMayControl v1.0.2 — Original DLL performance update

- Normal builds no longer collect or write optional diagnostic data.
- Reduced repeated native data checks and avoided copying a large loadout snapshot during attack validation.
- Removed duplicate refreshes for stable UI updates while preserving picker/input transition order.
- Kept world tracking independent of diagnostic limits and prevented diagnostic writes from blocking attack corrections.
- Throttled failed settings-save retries while retaining pending edits.
- Directional combat, weapon swaps, presets, native unlocks/upgrades and Split persistence are retained.

The author reports that this Original DLL build feels smoother and close to native, with mod functions appearing normal. Build/export checks, native event replay, UI checks and a 20-second input-free Linux/Proton startup check passed. Captures differed in actions, warm-up and training-room activity, so no controlled FPS improvement or fixed frame-rate guarantee is claimed. Native Windows gameplay has not been independently tested.

This update applies to the Original DLL route. The separate Main/f2g download remains v1.0.1. Install only one DylanMayControl route.
