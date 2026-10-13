# Upstream compatibility check

The checker fetches `origin/rebased` and `mamedev/mame` master, pins both SHAs,
and rebases a temporary worktree from their merge base onto upstream. It then
runs the Linux build and `dist.mak` with the options in `build.yml`. It never
pushes, changes `rebased`, or creates tags/releases. Temporary branches and
worktrees are removed on ordinary completion or failure.

## Manual test and weekly activation

1. Incorporate the workflow into the repository default branch (`rebased`).
   GitHub requires a `workflow_dispatch` workflow to exist on the default branch.
2. Open Actions → Upstream compatibility → Run workflow. Weekly builds remain
   disabled until the repository Actions variable
   `UPSTREAM_COMPATIBILITY_ENABLED` is set to the exact string `true`.
3. Inspect the result and download the diagnostics artifact. Full MAME builds
   can consume considerable runner time; the job has a six-hour timeout.
4. Enable that variable after a satisfactory manual test. Weekly checks run
   Mondays at 06:00 UTC (08:00 Spanish summer time, 07:00 winter time).

Before merging, you can run the script locally on this feature branch, with a
full-history clone, the Linux dependencies from the workflow and sufficient
free disk space:

```sh
BUILD_JOBS=2 bash scripts/check-upstream-compatibility.sh /tmp/gm-compatibility-logs
```

The checker always tests the latest remote `rebased`, even when invoked from
another branch. It does not test uncommitted local changes. `BUILD_JOBS` can
limit local memory usage. Avoid simultaneous invocations sharing a diagnostics
directory. Use a clone where `origin` points to antonioginer/GroovyMAME.

## Diagnostics

Artifacts are retained for 30 days, including failed checks. They contain the
pinned SHAs, merge base, upstream commit history, GroovyMAME commit list, and
rebase/build/distribution logs. A rebase conflict also records unmerged index
stages, conflict diffs and the GroovyMAME patch being replayed. Compiler errors
with conventional source-path/line-number formatting produce a file list.
Path-specific upstream history provides candidate commits for investigation;
it does not establish causality or replace a bisect. The whole upstream
history is retained when compiler paths cannot be extracted.

Setup/network/toolchain failures also fail the check; consult the Actions logs
if they occur before the checker starts. A forced runner termination may stop
cleanup or artifact upload; no remote refs are written in any case. The initial
implementation was checked with Bash syntax validation, YAML parsing and
isolated Git fixtures for successful rebases, conflicts, build failures and
distribution failures. A full real GroovyMAME build remains to be run in Actions.

## Work-oriented conflict diagnostics

A failed rebase also invokes `scripts/collect-upstream-context.sh` after saving
its original conflict state. The collector performs **no rebases** and no
bisection. It writes:

- `work-handoff.md`: pinned revisions, first failed GroovyMAME commit, affected
  paths, and a reproducible Work task with safety constraints.
- `groovymame-pending-commits.tsv`: the failed commit and later commits in their
  original replay order, with paths changed by each commit.
- `conflict-related-groovymame.tsv`: pending commits touching a currently
  conflicted path, grouped by path, to reveal incremental dependencies.
- `upstream-conflict-history.tsv`: upstream commits since the common ancestor
  touching those paths (SHA, date, subject, path). This is a candidate list,
  **not** a causal attribution.

The original `failed-groovymame-commit.patch`, `conflicts.diff`, unmerged index
stages and rebase log are preserved as before. Work should reproduce the rebase
on a disposable branch/worktree, resolve related conflicts consistently across
the complete GroovyMAME series, build, and submit a patch/PR for human review.
Never skip a conflicting commit or change `rebased` to make the test pass.

For very large histories, the detailed upstream candidate list is capped at
`CONTEXT_MAX_UPSTREAM_COMMITS` (default 300) per conflicted path, with an explicit
truncation notice. The collector is best-effort and never masks the original
rebase failure.
