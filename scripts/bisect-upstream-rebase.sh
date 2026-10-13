#!/usr/bin/env bash
# Find the first observed failing upstream first-parent revision for the complete
# GroovyMAME rebase. This is a bounded diagnostic, not a proof of monotonicity.
# Usage: bash scripts/bisect-upstream-rebase.sh BASE TARGET UPSTREAM WORKTREE OUT
set -Eeuo pipefail
export GIT_TERMINAL_PROMPT=0
base=$1 target=$2 upstream=$3 work=$4 out=$5
mkdir -p "$out"
report="$out/upstream-bisect.txt"
: > "$report"
log() { printf '%s\n' "$*" | tee -a "$report"; }
log "Base: $base"
log "GroovyMAME: $target"
log "Upstream: $upstream"
# First-parent history gives an ordered release-integration path. If the
# merge-base is not on that path, don't claim a meaningful binary search.
mapfile -t chain < <(git rev-list --first-parent --reverse "$upstream")
start=-1
for i in "${!chain[@]}"; do
    if [[ ${chain[i]} == "$base" ]]; then start=$i; break; fi
done
if (( start < 0 )); then
    log 'SKIPPED: merge-base is not on the upstream first-parent chain'
    exit 0
fi
commits=("$base" "${chain[@]:start+1}")
last=$((${#commits[@]} - 1))
if (( last < 1 )); then log 'SKIPPED: no upstream first-parent commits'; exit 0; fi
log "First-parent commits in interval: $last"
# Ensure every attempt starts from the exact original GroovyMAME tip.
# --onto replays the whole patchset, including prerequisites of the failing commit.
test_candidate() {
    local sha=$1 result=$2 rc
    git -C "$work" rebase --abort >/dev/null 2>&1 || true
    git -C "$work" reset --hard "$target" >/dev/null
    set +e
    GIT_EDITOR=: GIT_SEQUENCE_EDITOR=: git -C "$work" \
      -c user.name='Compatibility check' \
      -c user.email='compatibility@localhost' \
      -c rerere.enabled=false \
      rebase --onto "$sha" "$base" > "$out/bisect-last-rebase.log" 2>&1
    rc=$?
    set -e
    if (( rc == 0 )); then
        printf 'pass' > "$result"
    else
        printf 'fail' > "$result"
        git -C "$work" rebase --show-current-patch 2>/dev/null | \
          sed -n '1,12p' > "$out/bisect-last-failed-patch.txt" || true
    fi
    git -C "$work" rebase --abort >/dev/null 2>&1 || true
    git -C "$work" reset --hard "$target" >/dev/null
}
# The base must pass; otherwise the diagnostic cannot attribute the failure
# to newer upstream commits. The tip was already observed to fail by caller.
log 'Testing original merge-base (control)...'
test_candidate "$base" "$out/bisect-control.txt"
if [[ $(cat "$out/bisect-control.txt") != pass ]]; then
    log 'INCONCLUSIVE: rebase already fails at merge-base'
    exit 0
fi
low=0 high=$last attempts=0 max_attempts=${BISECT_MAX_ATTEMPTS:-12}
while (( high - low > 1 && attempts < max_attempts )); do
    mid=$(((low + high) / 2))
    sha=${commits[mid]}
    result="$out/bisect-probe-result.txt"
    test_candidate "$sha" "$result"
    outcome=$(cat "$result")
    log "Probe $((attempts+1)): $sha ($mid/$last) => $outcome"
    if [[ $outcome == pass ]]; then low=$mid; else high=$mid; fi
    attempts=$((attempts+1))
done
if (( high - low != 1 )); then
    log "INCONCLUSIVE: stopped after $attempts probes; remaining interval: $((high-low)) commits"
    exit 0
fi
log "Last passing first-parent commit: ${commits[low]}"
log "First failing first-parent commit: ${commits[high]}"
git show -s --format='Commit: %H%nSubject: %s%nAuthor date: %aI' "${commits[high]}" >> "$report"
git diff-tree --no-commit-id --name-only -r "${commits[high]}" > "$out/bisect-first-failing-files.txt"
log 'NOTE: This identifies a pass/fail boundary for the full rebase on the first-parent path, not necessarily the unique cause of every conflict. Non-monotonic histories may have earlier failures.'
