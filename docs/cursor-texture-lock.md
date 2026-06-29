# Cursor Texture Lock

## Purpose

Cursor Texture Lock is an opt-in native hook for mods that replace the mouse cursor texture. Some cursor mods update `r_MouseCursorTexture` correctly, but the engine can still briefly reapply its built-in cursor while the mouse is moving. This feature keeps mod-controlled cursor textures active by blocking known engine fallback cursor paths after a mod cursor path has been observed, and by applying mod cursor frames through a cached texture path that does not clear the active cursor texture first.

The feature is disabled by default.

## Launch Flags

Enable diagnostic hook installation without fallback blocking:

```text
-kcd2dbCursorHook
```

Enable cursor fallback blocking:

```text
-kcd2dbCursorLock
```

Enable sampled path tracing:

```text
-kcd2dbCursorTrace
```

Add an extra mod-controlled cursor texture directory:

```text
-kcd2dbCursorPrefix Data/Textures/MyCursor
```

The prefix flag can also be written as `-kcd2dbCursorPrefix=Data/Textures/MyCursor` and may be repeated.

## Runtime Behavior

The hook validates the game cursor setter by signature scanning `WHGame.dll`, then cross-checking the function address against the `IHardwareMouse` vtable found from `gEnv`. After validation, it installs a function-entry detour on the resolved cursor setter rather than only replacing the vtable slot. This is required because the engine has internal direct calls to the same setter that bypass the vtable.

When `-kcd2dbCursorLock` is active:

- Paths under `Data/Textures/Cursor`, `Data/Textures/CursorMouse`, `Data/Textures/CursorHand`, and `Data/Textures/CursorDagger` are treated as mod-controlled cursor textures by default.
- Paths passed through `-kcd2dbCursorPrefix` are also treated as mod-controlled cursor textures.
- The most recent mod-controlled cursor path is remembered.
- The concrete `ICVar::Set(const char*)` slot for `r_MouseCursorTexture` is hooked so known fallback writes are substituted with the last remembered mod path before they mutate the cvar.
- Calls to the cursor setter with known engine fallback paths are blocked after a mod-controlled cursor path has been seen.
- Calls to the cursor setter with mod-controlled paths load the texture through the engine texture system, cache it by normalized path, and swap it into the hardware mouse object without a transient null texture pointer.
- Other cursor paths pass through unchanged.

The hook does not animate cursors and does not replace mod logic. A cursor mod remains responsible for setting the current cursor texture; the hook only prevents the engine's fallback cursor from being inserted between mod-controlled frames.

## Safety Boundaries

- The hook is not installed unless `-kcd2dbCursorHook` or `-kcd2dbCursorLock` is present.
- The target setter is resolved by signature scan and validated against the `IHardwareMouse` vtable before patching the function entry.
- The texture cache binding is derived from the resolved setter body, including the engine texture-system global, the version-specific texture-load vtable slot, and the cursor path string assignment helper.
- The `r_MouseCursorTexture` string setter vtable slot is also derived from the resolved setter body instead of the shuffled `ICVar` declaration order.
- If validation fails, the hook logs a warning and leaves the game untouched.
- If the texture cache bindings or a specific cursor texture load fail, that cursor setter call falls back to the original engine implementation.
- The original function-entry bytes, trampoline, cvar hook, and console sink are restored on `DLL_PROCESS_DETACH` when the module is unloaded normally.

## Trace Output

With `-kcd2dbCursorTrace`, the hook logs sampled cursor activity from:

- the cursor setter entry detour, including normalized path, fallback classification, current cvar value, hotspot, and caller module offset;
- cached mod cursor texture application and texture load failures;
- `ICVar::Set(const char*)` for `r_MouseCursorTexture`, including allowed/fallback classification and whether the value was substituted;
- `r_MouseCursorTexture` console variable changes before and after the engine accepts them.

## Default Scope

The default allowed prefixes cover the AnimatedCursor v2.0 texture directories. Use `-kcd2dbCursorPrefix` for mods that store cursor textures elsewhere.
