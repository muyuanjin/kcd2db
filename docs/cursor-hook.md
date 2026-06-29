# Cursor Hook

## Purpose

Cursor Hook is an opt-in native hook for mods that replace the mouse cursor texture. Some cursor mods update `r_MouseCursorTexture` correctly, but the engine can still briefly reapply its built-in cursor while the mouse is moving. This feature keeps mod-controlled cursor textures active by blocking known engine fallback cursor paths after a mod cursor path has been observed, and by applying mod cursor frames through a cached texture path that does not clear the active cursor texture first.

The feature is disabled by default.

## Launch Flag

Enable the cursor hook:

```text
-kcd2dbCursorHook
```

## Runtime Behavior

The hook validates the game cursor setter by signature scanning `WHGame.dll`, then cross-checking the function address against the `IHardwareMouse` vtable found from `gEnv`. After validation, it installs a function-entry detour on the resolved cursor setter rather than only replacing the vtable slot. This is required because the engine has internal direct calls to the same setter that bypass the vtable.

When `-kcd2dbCursorHook` is active:

- Paths under `Data/Textures/Cursor`, `Data/Textures/CursorMouse`, `Data/Textures/CursorHand`, and `Data/Textures/CursorDagger` are treated as mod-controlled cursor textures.
- Paths declared through `KCD2DB.Cursor.Declare(path)` or `KCD2DB.Cursor.Lock(path)` are also treated as mod-controlled cursor textures.
- The most recent mod-controlled cursor path is remembered.
- A path locked through `KCD2DB.Cursor.Lock(path)` is preferred over the most recently observed path until `KCD2DB.Cursor.Unlock()` is called.
- The concrete `ICVar::Set(const char*)` slot for `r_MouseCursorTexture` is hooked so known fallback writes are substituted with the last remembered mod path before they mutate the cvar.
- Calls to the cursor setter with known engine fallback paths are blocked after a mod-controlled cursor path has been seen.
- Calls to the cursor setter with mod-controlled paths load the texture through the engine texture system, cache it by normalized path, and swap it into the hardware mouse object without a transient null texture pointer.
- Other cursor paths pass through unchanged.

The hook does not animate cursors and does not replace mod logic. A cursor mod remains responsible for setting the current cursor texture; the hook only prevents the engine's fallback cursor from being inserted between mod-controlled frames.

## Lua API

Cursor APIs are exposed through `KCD2DB.Cursor`:

```lua
KCD2DB.Cursor.Declare("Data/Textures/MyCursor/frame_01.dds")
KCD2DB.Cursor.Lock("Data/Textures/MyCursor/frame_01.dds")
KCD2DB.Cursor.Unlock()
```

`Declare(path)` marks a path as mod-controlled without changing the current locked path. Use it for cursor textures outside the default directories.

`Lock(path)` marks the path as mod-controlled, applies it immediately through the cached texture path, and makes it the preferred fallback replacement after that apply succeeds. Cursor animation mods can call `Lock(path)` only when their animation frame changes instead of repeatedly writing `r_MouseCursorTexture` every frame.

`Unlock()` clears the explicit lock. After unlock, fallback replacement returns to the most recently observed mod-controlled cursor path.

`Declare(path)` can be called during Lua startup while the native hook is still installing; when `-kcd2dbCursorHook` is present, valid declarations are queued and used after installation finishes. It returns `false` if the hook was not requested or validation has already failed. `Lock(path)` and `Unlock()` require an installed hook and return `false` if the hook is unavailable. `Lock(path)` also returns `false` if the cursor texture cannot be applied.

## Safety Boundaries

- The hook is not installed unless `-kcd2dbCursorHook` is present.
- The target setter is resolved by signature scan and validated against the `IHardwareMouse` vtable before patching the function entry.
- The texture cache binding is derived from the resolved setter body, including the engine texture-system global, the version-specific texture-load vtable slot, and the cursor path string assignment helper.
- The `r_MouseCursorTexture` string setter vtable slot is also derived from the resolved setter body instead of the shuffled `ICVar` declaration order.
- If validation fails, the hook logs a warning and leaves the game untouched.
- If the texture cache bindings or a specific cursor texture load fail, that cursor setter call falls back to the original engine implementation.
- The original function-entry bytes, trampoline, cvar hook, and console sink are restored on `DLL_PROCESS_DETACH` when the module is unloaded normally.

## Diagnostics

With `-console`, the hook writes important cursor activity to `kcd2db.log`, including first accepted mod cursor path, first fallback substitution, first blocked engine fallback, and first cached texture application.

## Default Scope

Any cursor texture path under `Data/Textures/Cursor*` is treated as mod-controlled automatically and does not need `KCD2DB.Cursor.Declare(path)`. Matching is done after path normalization, so `data/textures/cursor*` works too; `Data/Textures/Cursor*` is the recommended spelling because it follows the game data path casing. Use `Declare(path)` or `Lock(path)` only for cursor textures stored outside that default scope.
