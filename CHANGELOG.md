# DylanMayControl v1.0.1

- Split Pivot and Split Orbit can remain active together: one Pivot at enemies and one Orbit around the player.
- Triggering another finisher no longer removes either ongoing Split effect.
- Repeating a Split keeps its native retrigger behavior, with at most one of each type.
- Native duration, expiry, cancels, ownership and upgrades are retained.
- Existing weapon swapping, input/menu behavior and charged Uppercut fixes are retained.

Both v1.0.1 routes passed build/export checks and bounded 15-second startup checks on Linux/Proton. The author confirmed the Split persistence fix working in gameplay with the Original DLL route. The f2g route contains the identical shared source fix but has not had a separate gameplay test. Native Windows gameplay has not been independently tested.

Possible stutter has been observed, but it is unmeasured and its cause is unconfirmed. Neither route has an established performance advantage.
