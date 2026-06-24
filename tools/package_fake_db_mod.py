from __future__ import annotations

import argparse
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parents[1]
MOD_ID = "kcd_fake_db"
MOD_FOLDER = "kcd_fake_db"
PAK_NAME = "kcd_fake_db.pak"
FAKE_DB_SOURCE_PATH = ROOT / "dist" / "kcd2db_fake_db.lua"
ASSET_PREFIX = "kcd2db_fake_db"


def normalized_version(version: str) -> str:
    return version.strip() or "dev"


def make_manifest(version: str) -> bytes:
    text = f"""<?xml version="1.0" encoding="utf-8"?>
<kcd_mod>
  <info>
    <name>kcd2db Fake DB</name>
    <description>Session-only fake DB backend for diagnosing kcd2db-dependent Lua mods without the ASI plugin.</description>
    <author>muyuanjin</author>
    <version>{version}</version>
    <created_on>2026-06-24</created_on>
    <modid>{MOD_ID}</modid>
  </info>
</kcd_mod>
"""
    return text.encode("utf-8")


def make_lua(version: str) -> bytes:
    fake_db_source = FAKE_DB_SOURCE_PATH.read_text(encoding="utf-8")
    text = f"""System.LogAlways("[kcd2db_fake_db] loading fake DB fallback {version}")

local ok, err = pcall(function()
{fake_db_source}
end)

if ok then
    if type(KCD2DB) == "table" and KCD2DB.fake == true then
        System.LogAlways("[kcd2db_fake_db] installed session-only fake kcd2db backend")
    else
        System.LogAlways("[kcd2db_fake_db] native kcd2db backend is available; fake backend not used")
    end
else
    System.LogAlways("[kcd2db_fake_db][ERROR] failed to install fake DB fallback: " .. tostring(err))
end
"""
    return text.encode("utf-8")


def add_stored(zf: ZipFile, name: str, data: bytes) -> None:
    info = ZipInfo(name, date_time=(2026, 6, 24, 12, 0, 0))
    info.compress_type = ZIP_STORED
    info.create_system = 0
    info.external_attr = 0x20
    zf.writestr(info, data)


def build(output: Path, version: str) -> None:
    version = normalized_version(version)
    output.parent.mkdir(parents=True, exist_ok=True)

    lua = make_lua(version)
    pak_buffer = output.parent / PAK_NAME
    with ZipFile(pak_buffer, "w", compression=ZIP_STORED, allowZip64=False) as pak:
        add_stored(pak, f"Scripts/Mods/{MOD_ID}.lua", lua)

    with ZipFile(output, "w", compression=ZIP_DEFLATED, allowZip64=False) as archive:
        archive.writestr(f"Mods/{MOD_FOLDER}/mod.manifest", make_manifest(version))
        archive.write(pak_buffer, f"Mods/{MOD_FOLDER}/data/{PAK_NAME}")

    pak_buffer.unlink()


def main() -> None:
    parser = argparse.ArgumentParser(description="Package the kcd2db fake DB fallback mod.")
    parser.add_argument("--version", default="dev", help="Version string written to mod.manifest.")
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output zip path. Defaults to dist/kcd2db_fake_db-<version>.zip.",
    )
    args = parser.parse_args()

    version = normalized_version(args.version)
    output = args.output or ROOT / "dist" / f"{ASSET_PREFIX}-{version}.zip"
    build(output, version)
    print(output)


if __name__ == "__main__":
    main()
