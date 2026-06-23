---
name: nexusmods-release
description: Prepare and publish existing GitHub Release assets to Nexus Mods. Use when the user explicitly asks to prepare, review, summarize, or publish a Nexus Mods release from a repository release. The skill must discover project-specific values from the current repository, user input, workflow configuration, GitHub variables/secrets, and release assets; draft user-facing release notes from the previous Nexus-published version through the target version; and require explicit user approval before triggering any publishing workflow.
---

# Nexus Mods Release

## Non-Hardcoding Rule

Do not hardcode repository-specific, mod-specific, account-specific, or personal values in this skill or its outputs. Discover or request them instead.

Examples of values that must be discovered or requested:

- Repository owner/name.
- Mod name and display name.
- Nexus Mods file ID.
- Previous Nexus-published version.
- GitHub Release tag.
- GitHub Release asset filename or naming pattern.
- Workflow filename used for Nexus publishing.

It is acceptable to refer to generic configuration keys such as `NEXUSMODS_API_KEY` and `NEXUSMODS_FILE_ID`, because they are configuration names rather than a specific user's secret or file ID.

## Safety Gate

Never publish on the first pass. Treat Nexus publication as a two-step process:

1. Gather release context and draft the upload parameters.
2. Ask the user to approve the exact tag, version, asset, archive setting, description, and workflow command.

Only run `gh workflow run ...` after the user gives explicit approval for those exact values. If the user asks for a draft, review, summary, or recommendation, do not trigger the workflow.

## Required Context

Before drafting release notes, determine:

- Target GitHub Release tag.
- Nexus version string, usually the target tag without a leading `v`.
- Previous Nexus-published version or tag.
- GitHub Release asset to upload.
- Nexus publishing workflow filename.
- Whether `NEXUSMODS_API_KEY` and `NEXUSMODS_FILE_ID` are configured.

Find the previous Nexus-published version in this order:

1. Use the version or tag explicitly provided by the user.
2. Use reliable current conversation context if the user already provided the current Nexus version.
3. Inspect public Nexus Mods or API information if available and appropriate.
4. If it still cannot be determined, ask the user for the current Nexus version before writing a changelog.

Find the upload asset in this order:

1. Use an asset explicitly provided by the user.
2. Use the repository's Nexus publishing workflow if it clearly selects an asset.
3. Use the only `.zip`, `.7z`, or `.rar` asset on the target GitHub Release.
4. If multiple plausible assets exist, ask the user which one to upload.

## Context Collection

When Nexus Mods API details, upload-action behavior, or file-description limits matter, read `references/nexusmods-api.md` before browsing the web. Use that reference for stable entry points and known public API constraints; browse only to confirm behavior that may have changed.

Use the helper script when possible:

```bash
.agents/skills/nexusmods-release/scripts/collect-release-context.sh \
  --previous-tag <previous-tag> \
  --target-tag <target-tag>
```

Optional asset arguments:

```bash
.agents/skills/nexusmods-release/scripts/collect-release-context.sh \
  --previous-tag <previous-tag> \
  --target-tag <target-tag> \
  --asset-pattern "*.zip"
```

The script prints repository configuration, release asset checks, commit subjects, and changed files for the comparison range. Read its output before summarizing.

If the helper cannot run, collect the same information manually:

```bash
gh secret list
gh variable list
gh release view <target-tag> --json tagName,assets,body
git log --oneline <previous-tag>..<target-tag>
git diff --name-only <previous-tag>..<target-tag>
```

## User-Facing Summary Rules

Write Nexus descriptions for players and mod users, not for maintainers.

Include:

- support changes that affect whether the mod loads or works.
- Crash, data loss, save/load, install, or startup fixes.
- Logging and diagnostics only when they help users report or understand failures.
- Clear update advice, such as whether the update is optional.

Omit or compress:

- Build workflow fixes.
- Documentation-only changes.
- Internal refactors, dependency moves, style changes, and code organization changes.
- Low-level implementation details unless they explain a user-visible effect.

Translate technical details into user language. For example:

- Prefer: `Improved support for the Game Pass / Microsoft Store version.`
- Prefer: `The mod can now find the required game files more reliably across different install variants.`
- Avoid: `Improved DLL scanning genericity.`

## Draft Output

Before asking for approval, show:

- `workflow`
- `tag`
- `version`
- `asset`
- `archive_existing_version`
- `description`
- The exact `gh workflow run` command that will be used

Use `archive_existing_version=false` by default when the release is optional or the prior Nexus version should remain downloadable. Use `true` only when the old version should be hidden because it is broken, dangerous, or obsolete.

For long descriptions, use a temporary file and `-F description=@file`:

```bash
gh workflow run <workflow-file> \
  -f tag=<tag> \
  -f version=<version> \
  -F description=@/tmp/nexusmods-release-description.txt \
  -f archive_existing_version=false
```

After the user approves, run the command, then report the workflow run URL or explain how to inspect it:

```bash
gh run list --workflow <workflow-file> --limit 3
```
