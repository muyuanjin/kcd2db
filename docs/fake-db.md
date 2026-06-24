# Optional Fake DB

`dist/kcd2db_fake_db.lua` lets a Lua mod use the `DB.Create` API even when the user has not installed `kcd2db.asi`.

Copy the file into your own PAK and load it before creating a database object:

```lua
Script.ReloadScript("scripts/<modid>/kcd2db_fake_db.lua")
local db = DB.Create("MyMod")
```

Recommended PAK layout:

```text
data/<mod>.pak
  scripts/mods/<modid>.lua
  scripts/<modid>/kcd2db_fake_db.lua
```

Use a mod-specific directory such as `scripts/<modid>/` so that different mods do not overwrite a shared fake DB script path inside merged PAKs.

## Runtime Behavior

- When `kcd2db.asi` is installed, the fake DB file leaves the real `LuaDB` backend in place.
- When `kcd2db.asi` is missing, it installs a session-only fake backend. Reads and writes work in memory for the current Lua VM session and may survive reloading this fake DB file because the same fake `LuaDB` table is reused. Data is not saved to disk, does not survive a game restart, and is not isolated by save switch.
- Existing kcd2db versions with the full raw `LuaDB` API are treated as supported, including `v0.1.7` through `v0.1.12`. The fake DB file checks for `LuaDB.Set/Get/Del/Exi/All/SetG/GetG/DelG/ExiG/AllG/Dump`.
- The `DB` wrapper JSON-encodes values through the game's `Scripts/Utils/JSON/json.lua`. Tables and `nil` values require that JSON script. Raw `LuaDB` calls only accept booleans, numbers, and strings.
- `DB.Set("x", nil)` stores JSON `null` when JSON is available. `DB.Get("x")` returns `nil`, and `DB.Exi("x")` remains `true`. Use `DB.Del("x")` or `db.L.x = nil` to delete a key.

## Backend Status

The fake DB file exposes `_G.KCD2DB`:

```lua
KCD2DB.native      -- true when a real LuaDB backend is active
KCD2DB.fake        -- true when the session-only fake backend is active
KCD2DB.persistent  -- true when data is expected to persist
KCD2DB.backend     -- "sqlite", "native-supported", or "memory"
KCD2DB.version     -- fake DB script version
KCD2DB.conflicts   -- replaced or overwritten global values, when any
```

Mods should generally call `DB.Create` and use the returned object. Check `KCD2DB.persistent` only when the user experience needs to change without persistent storage.
