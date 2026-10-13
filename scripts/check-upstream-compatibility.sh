#!/usr/bin/env bash
# Usage: bash scripts/check-upstream-compatibility.sh [diagnostics-directory]
# Run from a full-history checkout. Only a disposable worktree is rebased.
set -Eeuo pipefail
export GIT_TERMINAL_PROMPT=0
repo=$(git rev-parse --show-toplevel)
out=$(realpath -m "${1:-$repo/upstream-compatibility-logs}")
mkdir -p "$out"
exec > >(tee "$out/check.log") 2>&1
scratch=$(mktemp -d)
work="$scratch/worktree"
branch="compatibility-test-$$"
status=setup-failed
cleanup() {
    rc=$?
    trap - EXIT
    printf '%s\n' "$status" > "$out/result.txt"
    if [[ -n ${GITHUB_STEP_SUMMARY:-} ]]; then
        printf '### Upstream compatibility\n\nResult: **%s**\n\nSee the diagnostics artifact for pinned revisions, logs and candidate commits.\n' "$status" >> "$GITHUB_STEP_SUMMARY"
    fi
    git -C "$repo" worktree remove --force "$work" 2>/dev/null || true
    git -C "$repo" branch -D "$branch" 2>/dev/null || true
    rm -rf "$scratch"
    exit "$rc"
}
trap cleanup EXIT
[[ $(git -C "$repo" rev-parse --is-shallow-repository) == false ]] || { echo 'Full Git history is required'; exit 1; }
git -C "$repo" fetch --no-tags origin refs/heads/rebased
target=$(git -C "$repo" rev-parse FETCH_HEAD)
git -C "$repo" fetch --no-tags https://github.com/mamedev/mame.git refs/heads/master
upstream=$(git -C "$repo" rev-parse FETCH_HEAD)
base=$(git -C "$repo" merge-base "$target" "$upstream")
printf 'rebased=%s\nupstream/master=%s\nmerge-base=%s\n' "$target" "$upstream" "$base" > "$out/revisions.txt"
git -C "$repo" log --format=fuller --stat "$base..$upstream" > "$out/upstream-commits.txt"
git -C "$repo" log --oneline "$base..$target" > "$out/groovymame-commits.txt"
git -C "$repo" worktree add -b "$branch" "$work" "$target"
# A sparse caller must still build the complete source tree.
git -C "$work" sparse-checkout disable
cd "$work"
if ! git -c user.name='Compatibility check' -c user.email='compatibility@localhost' -c rerere.enabled=false rebase --onto "$upstream" "$base" 2>&1 | tee "$out/rebase.log"; then
    status=rebase-failed
    git status --porcelain=v1 > "$out/conflict-status.txt"
    git diff --name-only --diff-filter=U > "$out/conflicted-files.txt"
    git diff > "$out/conflicts.diff"
    git rebase --show-current-patch > "$out/failed-groovymame-commit.patch" || true
    git ls-files -u > "$out/conflict-stages.txt"
    while IFS= read -r path; do
        printf '\n### %s\n' "$path"
        git log --format=fuller --stat "$base..$upstream" -- "$path"
    done < "$out/conflicted-files.txt" > "$out/upstream-conflict-candidates.txt"
    git rebase --abort || true
    # Produce bounded, read-only context for Work; do not retry or skip commits.
    bash "$repo/scripts/collect-upstream-context.sh" \
        "$repo" "$base" "$target" "$upstream" "$out" || \
        echo 'WARNING: supplemental Work context could not be completed' >&2
    exit 1
fi
git rev-parse HEAD > "$out/rebased-test-head.txt"
status=build-failed
jobs=${BUILD_JOBS:-$(nproc)}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'BUILD_JOBS must be a positive integer'; exit 1; }
# Same Linux configuration and distribution check as .github/workflows/build.yml.
if ! make -j"$jobs" TARGET=mame TOOLS=1 SEPARATE_BIN=1 PTR64=1 OPTIMIZE=3 SYMBOLS=0 SYMLEVEL=1 REGENIE=1 2>&1 | tee "$out/build.log"; then
    # Paths in compiler errors narrow the upstream candidates; this is not a bisect.
    sed -nE 's@^([^ :]+\.(c|cc|cpp|cxx|h|hpp)):[0-9]+:.*@\1@p' "$out/build.log" | sort -u > "$out/build-error-files.txt"
    while IFS= read -r path; do
        printf '\n### %s\n' "$path"
        git log --format=fuller --stat "$base..$upstream" -- "$path"
    done < "$out/build-error-files.txt" > "$out/upstream-build-candidates.txt"
    exit 1
fi
make -f dist.mak PTR64=1 2>&1 | tee "$out/distribution.log"
status=passed
