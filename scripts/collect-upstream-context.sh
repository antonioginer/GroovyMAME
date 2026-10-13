#!/usr/bin/env bash
# Usage: collect-upstream-context.sh REPO BASE TARGET UPSTREAM OUT
# Read-only analysis of an already failed rebase, after conflict files were saved.
set -Eeuo pipefail
repo=$1 base=$2 target=$3 upstream=$4 out=$5
mkdir -p "$out"
limit=${CONTEXT_MAX_UPSTREAM_COMMITS:-300}
[[ $limit =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid CONTEXT_MAX_UPSTREAM_COMMITS' >&2; exit 2; }
failed=$(git -C "$repo" rev-parse --verify -q REBASE_HEAD 2>/dev/null || true)
# Caller has aborted rebase, so REBASE_HEAD is normally gone. Read patch metadata instead.
if [[ -z $failed && -s "$out/failed-groovymame-commit.patch" ]]; then
    failed=$(sed -nE 's/^commit ([0-9a-f]{40})$/\1/p' "$out/failed-groovymame-commit.patch" | head -n 1)
fi
# The first failed commit is also available in the rebase log's 'could not apply' line.
if [[ -z $failed ]]; then
    short=$(sed -nE 's/^error: could not apply ([0-9a-f]+)\.\.\..*/\1/p' "$out/rebase.log" | head -n 1)
    [[ -z $short ]] || failed=$(git -C "$repo" rev-parse --verify -q "${short}^{commit}" || true)
fi
mapfile -t pending < <(git -C "$repo" rev-list --reverse --topo-order "$base..$target")
start=0
if [[ -n $failed ]]; then
    for i in "${!pending[@]}"; do
        if [[ ${pending[i]} == "$failed" ]]; then start=$i; break; fi
    done
fi
{
    printf 'order\tcommit\tsubject\tchanged_paths\n'
    for ((i=start;i<${#pending[@]};i++)); do
        sha=${pending[i]}
        subject=$(git -C "$repo" show -s --format=%s "$sha" | tr '\t\r\n' '   ')
        paths=$(git -C "$repo" diff-tree --root --no-commit-id --name-only -r "$sha" | paste -sd ';' -)
        printf '%s\t%s\t%s\t%s\n' "$((i+1))" "$sha" "$subject" "$paths"
    done
} > "$out/groovymame-pending-commits.tsv"
printf 'conflicted_path\torder\tcommit\tsubject\n' > "$out/conflict-related-groovymame.tsv"
printf 'conflicted_path\tupstream_commit\tdate\tsubject\n' > "$out/upstream-conflict-history.tsv"
while IFS= read -r path; do
    [[ -n $path ]] || continue
    # List all pending commits that touch this path, in replay order.
    for ((i=start;i<${#pending[@]};i++)); do
        sha=${pending[i]}
        if git -C "$repo" diff-tree --root --no-commit-id --name-only -r "$sha" -- "$path" | grep -q .; then
            subject=$(git -C "$repo" show -s --format=%s "$sha" | tr '\t\r\n' '   ')
            printf '%s\t%s\t%s\t%s\n' "$path" "$((i+1))" "$sha" "$subject" >> "$out/conflict-related-groovymame.tsv"
        fi
    done
    total=$(git -C "$repo" rev-list --count "$base..$upstream" -- "$path")
    git -C "$repo" log --format='%H%x09%as%x09%s' -n "$limit" "$base..$upstream" -- "$path" | \
      while IFS= read -r line; do printf '%s\t%s\n' "$path" "$line"; done >> "$out/upstream-conflict-history.tsv"
    if ((total > limit)); then
        printf '%s: upstream history truncated to latest %s of %s commits\n' "$path" "$limit" "$total" >> "$out/context-truncation.txt"
    fi
done < "$out/conflicted-files.txt"
{
    echo '# GroovyMAME / MAME upstream conflict — Work handoff'
    echo
    printf -- '- GroovyMAME rebased: `%s`\n- Upstream master: `%s`\n- Merge-base: `%s`\n' "$target" "$upstream" "$base"
    printf -- '- First failing GroovyMAME commit: `%s`\n' "${failed:-unknown; see rebase.log}"
    echo
    echo '## Conflicted paths'
    sed 's/^/- `/' "$out/conflicted-files.txt" | sed 's/$/`/'
    echo
    echo '## Instructions for Work'
    echo 'Reproduce the rebase using the pinned SHAs in a disposable worktree.'
    echo 'Inspect the failed commit, the remaining incremental commits, and upstream'
    echo 'changes to the same paths. Resolve related conflicts coherently while'
    echo 'preserving all GroovyMAME functionality and upstream architecture.'
    echo 'Continue through the entire series; do not skip commits to force success.'
    echo 'Compile using the existing Linux workflow settings. Provide a reviewable'
    echo 'patch or PR; do not push to or rewrite `rebased`.'
    echo
    echo '## Files to inspect'
    echo '- `rebase.log`, `conflicts.diff`, `conflict-stages.txt`'
    echo '- `failed-groovymame-commit.patch`'
    echo '- `groovymame-pending-commits.tsv`'
    echo '- `conflict-related-groovymame.tsv`'
    echo '- `upstream-conflict-history.tsv` (candidates only)'
    [[ ! -f "$out/context-truncation.txt" ]] || echo '- `context-truncation.txt`'
} > "$out/work-handoff.md"
