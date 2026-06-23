# Nexus Mods API Reference

Use these references before doing broad web searches:

- GitHub Marketplace action: https://github.com/marketplace/actions/upload-to-nexus-mods
- Nexus Mods API docs overview: https://api-docs.nexusmods.com/#section/Overview
- Nexus Mods API example repository: https://github.com/Nexus-Mods/API-Example
- Nexus Mods upload action source: https://github.com/Nexus-Mods/upload-action

## Upload Action Behavior

The official `Nexus-Mods/upload-action` uses `https://api.nexusmods.com/v3` by default. It uploads a file to a multipart upload session, finalises that upload, then creates a new mod file version with:

```text
POST /mod-files/{id}/versions
```

The create-version request accepts fields including:

- `upload_id`
- `name`
- `description`
- `version`
- `file_category`
- `archive_existing_file`
- `primary_mod_manager_download`
- `allow_mod_manager_download`
- `show_requirements_pop_up`

This is why a GitHub workflow can submit a longer `description` when creating a new Nexus file version.

## Known Public API Constraint

As of 2026-06-24, the OpenAPI schema bundled in `Nexus-Mods/upload-action` exposes:

- `POST /mod-files/{id}/versions` to create a new version with a `description`.
- `GET /mod-file-versions/{id}` to read a version.
- `PUT /mod-files/{id}` to update the mod file container name.

It does not expose a public `PUT` or `PATCH` endpoint for editing an existing mod file version's `description` after publication. If a task asks to change an already-published version description without uploading a new file/version, treat that as unsupported by the documented public v3 API unless current docs or Nexus support say otherwise.

## Practical Guidance

- For new uploads, pass long descriptions through the workflow/action input.
- For already-published versions, use the Nexus web UI if the needed edit fits its limits, create a new version if appropriate, or ask Nexus support for a backend correction.
- Do not rely on undocumented browser/admin endpoints for normal release automation.
- The Nexus web UI may limit manual description edits to 255 characters, but workflow/API upload descriptions can exceed that when creating a new version.
- Keep descriptions concise, but do not force them under 255 characters. Treat descriptions over 255 characters as effectively write-once and review them carefully before publishing.
