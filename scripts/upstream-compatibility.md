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

## Bounded upstream regression search

After a failed rebase, the checker attempts a bounded binary search over the
**first-parent** history between the common ancestor and upstream master. Each
probe resets the disposable worktree to the pinned GroovyMAME tip and replays
**all** GroovyMAME commits onto the candidate upstream revision; testing only
the failing commit would omit its GroovyMAME prerequisites. A control probe at
the merge-base must succeed, otherwise the search is inconclusive.

The default limit is 12 probes and 900 seconds. Configure
`BISECT_MAX_ATTEMPTS` or `UPSTREAM_BISECT_TIMEOUT` to adjust these limits;
`UPSTREAM_BISECT_ENABLED=0` skips the search. Results are in
`upstream-bisect.txt`, with the last probe log and first-failing commit file
list. A failed or timed-out diagnosis never changes the original rebase failure
result. The search does not build every candidate, and it does not claim to
identify all independent conflicts. In particular, pass/fail may be
non-monotonic when upstream later resolves an earlier incompatibility.
