# KCD2Cursor

`kcd2cursor.asi` is a standalone native cursor hook for Kingdom Come: Deliverance II mods that replace the mouse cursor texture.

It is separate from `kcd2db.asi`: cursor hook code is compiled only into `kcd2cursor.asi`, and Lua persistence code is compiled only into `kcd2db.asi`.

## What It Does

Some cursor mods update `r_MouseCursorTexture` correctly, but the engine can still briefly reapply its built-in cursor while the mouse is moving. The map UI can also switch or move cursor resources in a way that leaves stale texture resources behind and can crash later.

`kcd2cursor.asi` addresses those cases by:

- remembering the most recent mod-controlled cursor texture path,
- blocking known engine fallback cursor writes after a mod cursor has been observed,
- substituting fallback writes before they mutate `r_MouseCursorTexture`,
- applying mod cursor frames through a cached texture path that does not clear the active cursor texture first,
- keeping successful `Lock(path)` calls as the preferred post-unlock fallback cursor.

Installing `kcd2cursor.asi` enables the hook. No `kcd2db` command-line flag is required.

## Default Cursor Scope

Any cursor texture path under `Data/Textures/Cursor*` is treated as mod-controlled automatically and does not need Lua registration.

Matching is normalized, so `data/textures/cursor*` also works. `Data/Textures/Cursor*` is the recommended spelling because it follows the game's data path casing convention.

Examples that do not need `KCD2Cursor.Declare(path)`:

```lua
System.SetCVar("r_MouseCursorTexture", "Data/Textures/CursorMyMod/frame_01.dds")
System.SetCVar("r_MouseCursorTexture", "Data/Textures/Cursor/MyMod/frame_01.dds")
```

Use Lua registration only when your cursor textures live outside `Data/Textures/Cursor*`.

## Lua API

`kcd2cursor.asi` exposes a global `KCD2Cursor` table:

```lua
KCD2Cursor.Declare("Data/Textures/MyCursor/frame_01.dds")
KCD2Cursor.Lock("Data/Textures/MyCursor/frame_01.dds")
KCD2Cursor.Unlock()
```

`Declare(path)` marks a path as mod-controlled. It can be called during Lua startup while the native hook is still installing; valid declarations are queued and used after installation finishes. It returns `false` if validation has already failed.

`Lock(path)` marks the path as mod-controlled, applies it immediately through the cached texture path, and makes it the preferred fallback replacement after that apply succeeds. Cursor animation mods can call `Lock(path)` only when their animation frame changes instead of repeatedly writing `r_MouseCursorTexture` every frame.

`Unlock()` clears the locked preference. After unlock, fallback writes use the most recent remembered mod-controlled cursor path.

## Safety Notes

- The `SetCursor` hook is installed only by `kcd2cursor.asi`.
- The concrete `SetCursor` entry point is resolved by signature scan.
- The `r_MouseCursorTexture` string setter vtable slot is derived from the resolved setter body instead of the shuffled `ICVar` declaration order.
- If validation fails, the hook logs the failure and leaves engine cursor behavior unchanged.

## Debugging

With `-console`, the hook writes important cursor activity to `kcd2cursor.log`, including first accepted mod cursor path, first fallback substitution, first blocked engine fallback, and first cached texture application.

Use `-kcd2cursorConsoleLog=debug|info|warn|error|off` to change console log verbosity for this ASI.
