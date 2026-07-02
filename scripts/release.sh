#!/usr/bin/env bash
# Semi-automated local release script.
#
# Flow: preflight checks -> tag+push -> wait for CI build -> download dist
# artifact -> verify required files -> npm publish (with confirmation).
#
# This script does NOT run any step silently that mutates remote state
# without first explaining what it's about to do.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

WORKFLOW_FILE="release-build.yaml"
CI_POLL_TIMEOUT=120   # seconds to wait for the run to show up in `gh run list`
CI_POLL_INTERVAL=5    # seconds between polls

log() {
  echo "==> $*"
}

die() {
  echo "ERROR: $*" >&2
  exit 1
}

# ---------------------------------------------------------------------------
# 1. Preflight checks
# ---------------------------------------------------------------------------
log "Running preflight checks..."

command -v gh >/dev/null 2>&1 || die "GitHub CLI (gh) is not installed. Install it from https://cli.github.com/"
gh auth status >/dev/null 2>&1 || die "gh is not authenticated. Run 'gh auth login' first."

command -v npm >/dev/null 2>&1 || die "npm is not installed."
npm whoami >/dev/null 2>&1 || die "npm is not authenticated. Run 'npm login' first."

if [[ -n "$(git status --porcelain)" ]]; then
  die "git working tree is not clean. Commit or stash changes before releasing."
fi

log "Preflight checks passed."

# ---------------------------------------------------------------------------
# 2. Determine version and tag
# ---------------------------------------------------------------------------
VERSION="$(node -p "require('./package.json').version")"
TAG="v${VERSION}"
log "Releasing version ${VERSION} (tag ${TAG})"

# ---------------------------------------------------------------------------
# 3. Idempotent tag creation and push
# ---------------------------------------------------------------------------
REMOTE_TAG="$(git ls-remote --tags origin "refs/tags/${TAG}" || true)"

if [[ -n "$REMOTE_TAG" ]]; then
  log "Tag ${TAG} already exists on origin. Skipping tag creation/push."
else
  if git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null; then
    log "Tag ${TAG} exists locally but not on origin. Pushing existing tag."
  else
    log "Creating tag ${TAG}."
    git tag "$TAG"
  fi
  log "Pushing tag ${TAG} to origin."
  git push origin "$TAG"
fi

# ---------------------------------------------------------------------------
# 4. Detect the CI run for this tag and wait for completion
# ---------------------------------------------------------------------------
log "Looking for CI run triggered by tag ${TAG} (workflow ${WORKFLOW_FILE})..."

RUN_ID=""
elapsed=0
while [[ "$elapsed" -lt "$CI_POLL_TIMEOUT" ]]; do
  RUN_ID="$(gh run list \
    --workflow "$WORKFLOW_FILE" \
    --json databaseId,headBranch,status,conclusion,createdAt \
    --jq "[.[] | select(.headBranch == \"${TAG}\")] | sort_by(.createdAt) | last | .databaseId // empty")"

  if [[ -n "$RUN_ID" ]]; then
    break
  fi

  log "No run found yet for ${TAG}, retrying in ${CI_POLL_INTERVAL}s... (${elapsed}s/${CI_POLL_TIMEOUT}s)"
  sleep "$CI_POLL_INTERVAL"
  elapsed=$((elapsed + CI_POLL_INTERVAL))
done

[[ -n "$RUN_ID" ]] || die "Timed out waiting for a CI run for tag ${TAG} to appear. Check 'gh run list --workflow ${WORKFLOW_FILE}' manually."

log "Found CI run ${RUN_ID}. Waiting for it to complete..."

if ! gh run watch "$RUN_ID" --exit-status; then
  gh run view "$RUN_ID" || true
  die "CI run ${RUN_ID} failed. See details above (or run 'gh run view ${RUN_ID} --log-failed')."
fi

log "CI run ${RUN_ID} completed successfully."

# ---------------------------------------------------------------------------
# 5. Download the dist artifact
# ---------------------------------------------------------------------------
ARTIFACT_NAME="libav-vrew-dist-${TAG}"
log "Downloading artifact ${ARTIFACT_NAME}..."

rm -rf dist
gh run download "$RUN_ID" -n "$ARTIFACT_NAME" -D dist

# The workflow uploads with `path: dist/`, so the artifact root should
# already correspond to the contents of dist/. If gh nests it as
# dist/dist/..., flatten it.
if [[ -d dist/dist ]]; then
  log "Detected nested dist/dist directory, flattening."
  shopt -s dotglob
  mv dist/dist/* dist/
  shopt -u dotglob
  rmdir dist/dist
fi

# ---------------------------------------------------------------------------
# 6. Verify required artifact files
# ---------------------------------------------------------------------------
log "Verifying required dist files against package.json 'files' whitelist..."

REQUIRED_FILES=(
  "dist/libav-vrew.mjs"
  "dist/libav-vrew.js"
  "dist/libav.types.d.ts"
)

for f in "${REQUIRED_FILES[@]}"; do
  [[ -f "$f" ]] || die "Missing required file: ${f}"
done

WASM_MATCHES=(dist/libav-*-vrew.wasm.*)
if [[ ! -e "${WASM_MATCHES[0]}" ]]; then
  die "No files matching dist/libav-*-vrew.wasm.* were found."
fi

log "All required dist files are present."

# ---------------------------------------------------------------------------
# 7. Publish to npm (with explicit confirmation)
# ---------------------------------------------------------------------------
log "Running 'npm publish --dry-run' for review:"
npm publish --dry-run

read -r -p "Publish ${VERSION} to npm? [y/N] " CONFIRM
if [[ "$CONFIRM" =~ ^[Yy]$ ]]; then
  log "Publishing ${VERSION} to npm..."
  npm publish
  log "Published ${VERSION} to npm."
else
  log "publish 취소됨"
  exit 0
fi
