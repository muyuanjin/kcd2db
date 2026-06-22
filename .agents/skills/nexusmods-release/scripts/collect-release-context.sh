#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  collect-release-context.sh --previous-tag <tag> --target-tag <tag> [--asset-name <name>] [--asset-pattern <glob>]
  collect-release-context.sh --previous-version <version> --target-version <version> [--asset-name <name>] [--asset-pattern <glob>]

Examples:
  collect-release-context.sh --previous-tag v1.0.0 --target-tag v1.1.0
  collect-release-context.sh --previous-tag 1.0.0 --target-tag 1.1.0 --asset-pattern "*.zip"
  collect-release-context.sh --previous-version 1.0.0 --target-version 1.1.0
USAGE
}

previous_tag=""
target_tag=""
previous_version=""
target_version=""
asset_name=""
asset_pattern=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --previous-tag)
            previous_tag="${2:-}"
            shift 2
            ;;
        --previous-version)
            previous_version="${2:-}"
            shift 2
            ;;
        --target-tag)
            target_tag="${2:-}"
            shift 2
            ;;
        --target-version)
            target_version="${2:-}"
            shift 2
            ;;
        --asset-name)
            asset_name="${2:-}"
            shift 2
            ;;
        --asset-pattern)
            asset_pattern="${2:-}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "$previous_tag" && -z "$previous_version" ]] || [[ -z "$target_tag" && -z "$target_version" ]]; then
    usage >&2
    exit 2
fi

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1" >&2
        exit 1
    fi
}

require_cmd git
require_cmd gh

resolve_version_to_tag() {
    local value="$1"
    if git rev-parse --verify --quiet "refs/tags/${value}" >/dev/null; then
        printf '%s\n' "$value"
        return 0
    fi

    if [[ "$value" != v* ]] && git rev-parse --verify --quiet "refs/tags/v${value}" >/dev/null; then
        printf 'v%s\n' "$value"
        return 0
    fi

    printf '%s\n' "$value"
}

if [[ -z "$previous_tag" ]]; then
    previous_tag="$(resolve_version_to_tag "$previous_version")"
fi

if [[ -z "$target_tag" ]]; then
    target_tag="$(resolve_version_to_tag "$target_version")"
fi

version="${target_tag#v}"

git rev-parse --verify --quiet "refs/tags/${previous_tag}" >/dev/null || {
    echo "Previous tag does not exist locally: ${previous_tag}" >&2
    exit 1
}

git rev-parse --verify --quiet "refs/tags/${target_tag}" >/dev/null || {
    echo "Target tag does not exist locally: ${target_tag}" >&2
    exit 1
}

mapfile -t release_assets < <(gh release view "$target_tag" --json assets --jq '.assets[].name')

selected_asset=""
asset_note=""

if [[ -n "$asset_name" ]]; then
    for candidate in "${release_assets[@]}"; do
        if [[ "$candidate" == "$asset_name" ]]; then
            selected_asset="$candidate"
            break
        fi
    done
    if [[ -z "$selected_asset" ]]; then
        asset_note="requested asset not found: ${asset_name}"
    fi
elif [[ -n "$asset_pattern" ]]; then
    matches=()
    for candidate in "${release_assets[@]}"; do
        if [[ "$candidate" == $asset_pattern ]]; then
            matches+=("$candidate")
        fi
    done
    if [[ ${#matches[@]} -eq 1 ]]; then
        selected_asset="${matches[0]}"
    else
        asset_note="asset pattern matched ${#matches[@]} assets: ${asset_pattern}"
    fi
else
    matches=()
    for candidate in "${release_assets[@]}"; do
        case "$candidate" in
            *.zip|*.7z|*.rar)
                matches+=("$candidate")
                ;;
        esac
    done
    if [[ ${#matches[@]} -eq 1 ]]; then
        selected_asset="${matches[0]}"
    else
        asset_note="archive asset auto-detection found ${#matches[@]} candidates"
    fi
fi

echo "# Nexus Mods Release Context"
echo
echo "previous_tag: ${previous_tag}"
echo "target_tag: ${target_tag}"
echo "version: ${version}"
if [[ -n "$selected_asset" ]]; then
    echo "selected_asset: ${selected_asset}"
else
    echo "selected_asset: unresolved"
    echo "asset_note: ${asset_note}"
fi
echo

echo "## GitHub Repository"
gh repo view --json nameWithOwner,defaultBranchRef,url --jq '"- repo: \(.nameWithOwner)\n- default_branch: \(.defaultBranchRef.name)\n- url: \(.url)"'
echo

echo "## GitHub Configuration"
if gh secret list | grep -q '^NEXUSMODS_API_KEY[[:space:]]'; then
    echo "- NEXUSMODS_API_KEY: configured"
else
    echo "- NEXUSMODS_API_KEY: missing"
fi

if gh variable list | grep -q '^NEXUSMODS_FILE_ID[[:space:]]'; then
    gh variable list | awk '$1 == "NEXUSMODS_FILE_ID" { print "- NEXUSMODS_FILE_ID: configured" }'
else
    echo "- NEXUSMODS_FILE_ID: missing"
fi
echo

echo "## Target GitHub Release"
echo "- tag: ${target_tag}"
echo "- assets:"
for candidate in "${release_assets[@]}"; do
    echo "  - ${candidate}"
done
echo

echo "## Commits"
git log --no-merges --pretty=format:'- %h %s' "${previous_tag}..${target_tag}" || true
echo
echo

echo "## Changed Files"
git diff --name-only "${previous_tag}..${target_tag}" | sed 's/^/- /'
echo

echo "## Existing GitHub Release Body"
gh release view "$target_tag" --json body --jq '.body'
