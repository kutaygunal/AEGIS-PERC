# Run Log: S3-003 — Distributed Execution Phase 2 (Localhost Multi-Process)

**Date:** 2026-08-14
**Task:** S3-003 — Localhost host/worker execution split, implementing Phase 2 of
`docs/design/distributed.md`'s "Recommended evolution path"
**Sprint:** S3 (new, not yet registered in `orchestration/roadmap.yaml`/`tasks.yaml` per this
task's explicit instructions — a separate consolidation pass owns that)
**Commit:** (see branch `feat/s3-003-distributed-phase2-localhost`)

## Goal
`docs/design/distributed.md` (status: Draft, accepted as a design doc) already specifies the
target architecture for distributed execution across four phases. Phase 1 (`LocalJobPipeline`,
in-process, observable staged execution) is done. This task implements the **first increment of
Phase 2**: "localhost multi-process -- split host process from worker process, keep shared
filesystem artifact handoff, add lease/heartbeat and durable job metadata." Phases 3 (blob-backed
artifacts) and 4 (multi-node/remote) are explicitly out of scope and were **not started**.

No new design doc was written -- this implements directly against the existing accepted draft,
per the task instructions.

## What was built

### `DistributedJob` (durable job record)
- **`src/orchestration/include/aegis/orchestration/distributed_job.hpp`**
- **`src/orchestration/src/distributed_job.cpp`**

A filesystem-serializable record carrying every field `docs/design/distributed.md`'s "Job model"
section lists as "recommended stable fields": `job_id`, project/package reference, state, stage,
completed/total stage counts, cancellation-requested flag, worker assignment, output artifact
references, error summary, and timestamps (created/started/updated/finished), plus the small set
of retry-policy fields (`retry_count`, `last_failure_reason`, `heartbeat_at`) needed to implement
the doc's stale-worker/retry guidance.

**Project package handoff** follows the doc's Option A (shared filesystem) exactly as
recommended for a localhost MVP: `package_manifest_json` stores the raw
`ProjectPackage::to_manifest_json()` output (not a live object reference), and `base_path` is a
filesystem root host and worker(s) both resolve artifacts against -- identical to how
`LocalJobPipeline` already resolves artifacts today. This reuses `ProjectPackage`'s existing
round-trip JSON serialization rather than inventing a new package-reference format.

**Timestamps are epoch-milliseconds (`TimestampMs = std::int64_t`)**, not ISO-8601 strings. This
is a deliberate simplification vs. the doc's generic "timestamps: created, started, updated,
finished" language: it keeps heartbeat/lease staleness comparisons trivial integer arithmetic and
avoids the `gmtime_s`/`gmtime_r` portability pitfall already hit once in this codebase (see
`run_2026-08-14_s2-001-declarative-condition-rules.md`, "Post-PR CI Fixups").

Every write goes through `write_job_file_atomic()`: write to a uniquely-suffixed temp file, then
`std::filesystem::rename()` to the canonical `<job_id>.json` path (falling back to copy+remove on
a cross-filesystem rename failure). This directly implements the doc's repeated "temp files +
atomic promote semantics" guidance for every job-record write, not just final report export.

### `DistributedHost` (control plane)
- **`src/orchestration/include/aegis/orchestration/distributed_host.hpp`**
- **`src/orchestration/src/distributed_host.cpp`**

- `submit()` reuses `aegis::storage::ImportPreflightValidator` (not reinvented) to validate the
  package before queueing, exactly per the doc's "Host responsibilities: run import preflight
  using the same ProjectPackage + validator model." A package that fails validation
  (`ValidationStatus::Invalid`) is rejected and **no job record is written** -- matching "host
  should fail submission before queueing if required artifacts are absent." On success, a job
  record (state `Queued`) is durably written to `jobs_dir`.
- `request_cancel()` marks a still-`Queued` job `Cancelled` directly (no worker owns it yet), or a
  `Running` job `Cancelling` (worker observes and finishes the transition) -- matching the doc's
  Failure-mode-4 cancellation flow.
- `reconcile_stale_leases()` scans `jobs_dir` for `Running`/`Cancelling` jobs whose
  `heartbeat_at` exceeds `Config::lease_timeout`, treats them as a presumed worker crash
  (Failure-mode-2), and either requeues for a **full-job retry** (up to `Config::max_retries`) or
  marks the job `Failed` once retries are exhausted -- per the doc's explicit MVP recommendation
  ("prefer full-job rerun first for simplicity" / "do not implement partial-stage resume yet"). A
  stale job that also had a cancellation in flight resolves to `Cancelled`, not `Failed` or
  requeued, consistent with "terminal state becomes cancelled, not failed."
- The host is intentionally **stateless in-process** -- every call reads/writes the job file(s)
  directly rather than caching state in a map, so job metadata survives a host restart without a
  database. This satisfies the Phase 2 goal ("durable job metadata") with the simplest mechanism
  that still works; a real durable store is explicitly a Phase 3/4 concern per the doc.

### `DistributedWorker` (data plane)
- **`src/orchestration/include/aegis/orchestration/distributed_worker.hpp`**
- **`src/orchestration/src/distributed_worker.cpp`**

- **Process model choice (explicit simplification, documented in the header):** the worker runs
  as a background `std::thread` inside the caller's process, communicating with the host *only*
  through the filesystem job-queue contract -- never touching a `DistributedHost` instance or any
  other in-process host state. This keeps the choice reversible: a future increment can replace
  the thread with a real spawned OS process (`CreateProcess`/`posix_spawn`) with **zero** changes
  to the on-disk protocol, because the worker already behaves as if the host were a separate
  process. A true separate-process worker executable was not built this increment because it
  would add platform-specific process-spawning code and a new CLI entry point for a benefit
  (exercising a second OS process) that the filesystem-only contract already lets tests validate
  without it, per the task's own guidance to "pick whichever is a reasonable, testable first
  step."
- **Claiming** (`try_claim_next()`) is atomic via an exclusive-create sidecar lock file
  (`<job_id>.json.lock`). Implemented directly against the OS API
  (`CreateFileW(..., CREATE_NEW, ...)` on Windows, `open(..., O_CREAT|O_EXCL)` on POSIX) rather
  than `std::fopen(path, "wx")` -- MSVC's secure-CRT checks flag `fopen` itself (C4996) under
  this project's warnings-as-errors build, so going straight to the platform API avoids either
  disabling that warning project-wide or introducing a CRT-only code path. Exactly one concurrent
  claimer can succeed for a given job, which is what gives the claim step its atomicity, matching
  the doc's "temp files + atomic promote semantics" guidance generalized to a claim rather than
  just a final-artifact promote.
- **Execution reuses `LocalJobPipeline` internally**, not a reimplementation of the
  parse/normalize/graph_build/rule_execution/report_export stages -- per the doc's "Workers
  should execute the same core libraries used locally. They must not fork independent parsing or
  rule semantics." The worker reconstructs a `ProjectPackage` from the job's
  `package_manifest_json`, builds a `JobRequest`, and submits it to a `LocalJobPipeline` instance,
  bridging its progress callbacks to the on-disk `DistributedJob` record.
- **Cancellation observation**: the doc says "worker observes it at stage boundaries." The bridge
  re-reads the on-disk record on every non-terminal progress callback (each stage-entry point in
  `LocalJobPipeline`) and, if the host's `cancel_requested` flag is set, calls the local
  pipeline's own `request_cancel()`, which `LocalJobPipeline` already checks at its own stage
  boundaries. The terminal state naturally becomes `Cancelled`, matching `LocalJobPipeline`'s
  existing semantics.

## A real bug found and fixed during testing (cancellation-flag clobbering)

The first implementation of the progress-callback bridge built each disk write from
`claimed.record` (the worker's own last self-written copy) merged with the `LocalJobPipeline`
snapshot's `cancel_requested` field. Because `LocalJobPipeline`'s snapshot only reflects
cancellation *after* the worker has already told it to cancel, and the worker only learns about a
host-issued cancellation from a **fresh disk read**, this write path would silently **overwrite
the host's `cancel_requested=true` flag with `false`** on the very next heartbeat write -- before
the worker had a chance to notice it. The end-to-end cancellation test caught this immediately (a
cancelled job ran to full `Completed` instead of stopping). Fixed by reading the on-disk record
fresh at the top of every callback and OR-ing its `cancel_requested` value into the outgoing
write, so a host cancellation can never be clobbered by a worker's own heartbeat write. See the
comment above the callback in `distributed_worker.cpp` for the full reasoning.

## Explicit simplifications vs. the design doc (documented per task instructions)

1. **Timestamps as epoch-ms, not ISO-8601 strings** -- see "DistributedJob" section above.
2. **Worker as in-process `std::thread`, not a separate OS process** -- see "DistributedWorker"
   section above. The filesystem-only communication contract makes this reversible without a
   protocol change.
3. **Job-id uniqueness is process-local** (epoch-ms + monotonic counter), not globally unique
   (e.g. UUID or host-identity-prefixed). Sufficient for Phase 2 (single host process,
   localhost-only); multi-host-safe ID generation is a Phase 4 concern.
4. **Retry backoff is a hard cap on retry count, not exponential backoff.** The doc's "Retry
   policy guidance" says "cap retries with exponential backoff" -- this increment caps by count
   (`Config::max_retries`) only; a requeued job goes straight back to `Queued` with no scheduled
   delay. Backoff scheduling is left to a future increment.
5. **No background reconciliation thread.** `DistributedHost::reconcile_stale_leases()` is a
   method the embedding process (host wrapper, CLI, or test) calls periodically; this increment
   does not spin up its own timer thread to call it automatically.
6. **Two known, documented race windows, both narrow and non-corrupting:**
   - If a worker process crashes between acquiring the claim lock file and promoting the claimed
     job record, the orphaned `.lock` file permanently blocks that job from ever being claimed
     again. `reconcile_stale_leases()` only covers jobs that reached `Running` before their
     worker died, not a crash during the claim step itself. Left for a future increment
     (documented in `try_claim_next()`).
   - `DistributedHost::request_cancel()` and a worker's own heartbeat write are not
     lock-arbitrated against each other; a host cancel write and a worker heartbeat write that
     land at the exact same instant could, in principle, race. The cancellation-flag-clobbering
     fix above (OR-ing the freshly-read disk flag on every worker write) makes this
     self-healing on the very next worker callback rather than a silent, permanent loss -- but
     it is not a lock-free-safe compare-and-swap. Acceptable for this increment; a future
     increment could route cancellation through the same claim/lease mechanism used for
     execution.
7. **No partial-stage resume** -- explicitly a non-goal per the doc's own "MVP recommendation:
   do not implement partial-stage resume yet." Retries always rerun the full job from the
   immutable `package_manifest_json`/`base_path` inputs recorded at submit time.

## Explicit non-goals / deferred work
- **Phase 3 (localhost blob-backed artifacts)**: not started. Artifact handoff remains Option A
  (shared filesystem paths), as instructed.
- **Phase 4 (multi-node/remote)**: not started. No RPC/queue/blob-store dependency was added; no
  network transport exists.
- No gRPC, ZeroMQ, Redis, or any other new external dependency was introduced, per the project's
  README/AGENTS.md constraints and this task's explicit instruction.
- No changes to `src/ui/`, `src/rules/`, or `src/parsing/` beyond what `LocalJobPipeline` (already
  built on top of those modules) itself requires -- this task added no new call sites into those
  modules at all; the worker only calls the existing `LocalJobPipeline`/`ProjectPackage` public
  APIs.
- `orchestration/tasks.yaml`, `orchestration/roadmap.yaml`, `orchestration/HANDOFF.md`,
  `orchestration/DECISIONS.md`, and `README.md` were **not** edited, per this task's explicit
  instructions (a separate consolidation pass owns those).

## Files touched

New:
- `src/orchestration/include/aegis/orchestration/distributed_job.hpp`
- `src/orchestration/src/distributed_job.cpp`
- `src/orchestration/include/aegis/orchestration/distributed_host.hpp`
- `src/orchestration/src/distributed_host.cpp`
- `src/orchestration/include/aegis/orchestration/distributed_worker.hpp`
- `src/orchestration/src/distributed_worker.cpp`
- `tests/unit/orchestration/test_distributed_job.cpp`
- `orchestration/runs/run_2026-08-14_s3-003-distributed-phase2-localhost.md` (this file)

Modified:
- `src/orchestration/CMakeLists.txt` -- registered the four new source/header files.
- `tests/CMakeLists.txt` -- registered `test_distributed_job.cpp`.
- `docs/design/distributed.md` -- `## Status` line updated from `Draft` to `Draft — Phase 2 in
  progress`, with a pointer to this run log. No other content in that file was changed; nothing
  in the implementation contradicted the doc's guidance.

## Tests

`tests/unit/orchestration/test_distributed_job.cpp` (Catch2 v3, 5 new test cases, tags
`[orchestration][s3-003][Distributed]`):
1. **"DistributedHost submit queues a durable job record that a DistributedWorker can claim and
   run to completion"** -- submit writes a durable file before any worker exists; a worker
   claiming the job is externally observable (`state == Running`, `worker_id` set) while an
   artificial per-stage delay keeps it in flight; final state is `Completed` with both report
   paths present on disk.
2. **"DistributedWorker::run_once completes a queued job, producing the expected terminal state
   and output references"** -- synchronous claim+execute, verifies terminal state, output paths,
   and report content (violation count/rule id), and that a second worker finds no more queued
   work.
3. **"Cancellation requested mid-job reaches Cancelled, not Failed, and avoids partial output
   artifacts"** -- host cancels a job mid-flight (generous timing margin: 250ms per-stage
   artificial delay vs. a 50ms trigger); asserts terminal state is `Cancelled`
   (`REQUIRE(state != Failed)` explicitly asserted too), and that no report files were written to
   the output directory.
4. **"A stale/missing worker heartbeat is detected by host reconciliation and the job becomes
   retry-eligible, failing once retries are exhausted"** -- manually backdates a claimed job's
   heartbeat, verifies `reconcile_stale_leases()` requeues it (`retry_count == 1`,
   `worker_id` cleared) then fails it once `max_retries` is exceeded on a second simulated crash;
   also verifies a healthy (non-stale) `Running` job is left untouched by reconciliation.
5. **"A retried job (after a simulated worker crash) produces output byte-identical to a fresh
   run of the same inputs"** -- runs a reference job to completion, separately submits an
   identical package, simulates a worker crash (stale heartbeat, no output ever written),
   verifies reconciliation requeues it, then lets a worker actually complete it and asserts the
   retried run's `report.json`/`report.html` bytes are identical to the reference run's --
   validating the doc's "immutable inputs per retry" determinism guidance.

## Build & test results

Environment: Windows, MSVC 19.44 (Visual Studio 17 2022), `windows-release` preset
(`AEGIS_WARNINGS_AS_ERRORS=ON`, i.e. `/W4 /WX`), Qt 6.8.2 (msvc2022_64).

```
cmake --preset windows-release
=> Configure succeeded (dependencies already cached from a prior FetchContent run in this
   environment; no changes needed to CMakePresets.json or CMakeLists.txt dependency handling).

cmake --build build/windows-release --config Release
=> Full build succeeded, zero warnings/errors, including aegis_orchestration, aegis_unit_tests,
   aegis-perc-cli, and aegis-perc (the Qt GUI target) under /W4 /WX.

ctest --test-dir build/windows-release -C Release -L s3-003
=> 5/5 new distributed tests passing. Re-ran 5 consecutive times after fixing the
   cancellation-clobbering bug and one test-timing flake (see below) with 0 failures across all
   5 runs.

ctest --test-dir build/windows-release -C Release   (full suite)
=> 502/503 passed, 1 failure:
   - Test #158 "WorkspacePersistence persists named filter presets and workspace views across
     restart" (SEGFAULT). This is the pre-existing, previously-documented, timing-sensitive
     crash ("Bug B") recorded in
     run_2026-08-14_s2-001-declarative-condition-rules.md and orchestration/HANDOFF.md as
     unresolved, reproducing standalone ~70-90% of the time, unrelated to any code this task
     touches (this task added zero UI-code changes and zero call sites into src/ui/). Not
     re-investigated here since it is out of this task's scope.
```

## A test-only flake found and fixed while verifying (not a product bug)
While re-running the new suite repeatedly to check for flakiness, the stale-heartbeat test
(#4 above) failed intermittently with `lease_timeout = 10ms`: the sub-check that a *healthy*
`Running` job is left untouched by reconciliation set its heartbeat to "now" and immediately
called `reconcile_stale_leases()` -- with only a 10ms budget, ordinary disk I/O latency for the
write + subsequent directory scan could occasionally exceed the threshold on its own, making the
healthy job look stale by the time it was checked. Fixed by widening that test's
`lease_timeout` to 2000ms (the "definitely stale" heartbeats in the same test are backdated by
100 seconds, so this change doesn't weaken that half of the test at all). Re-verified 5/5 clean
runs after the fix.

## Acceptance mapping (S3-003 task instructions)
| Requirement | Status |
|---|---|
| `DistributedJob` record with doc's recommended stable fields | done -- `distributed_job.hpp` |
| Host: accept job reusing `ProjectPackage`/`ImportPreflightValidator`, write job+queue file, atomic write-then-rename, status snapshot queries | done -- `distributed_host.hpp/.cpp` |
| Worker: claim job, run existing staged pipeline via `LocalJobPipeline` (not reimplemented), write results atomically, report success/failure/cancellation | done -- `distributed_worker.hpp/.cpp` |
| Lease/heartbeat mechanism, stale-worker timeout recovery | done -- `heartbeat_at` field + `reconcile_stale_leases()` |
| Worker crash -> lease timeout -> retry | done -- test 4 |
| Cancellation -> `cancelled` not `failed` | done -- test 3 (and the clobbering bug this caught) |
| No partial-stage resume; full-job rerun on retry | done, by design (no partial-resume code path exists) |
| Headless, no Qt dependency in `orchestration` | done -- verified no Qt includes anywhere in the four new files; `aegis_orchestration`'s CMakeLists links no Qt target |
| No new external dependency (no gRPC/ZeroMQ/Redis) | done -- filesystem + OS process-level exclusive-create APIs only |
| Every functional change has a test | done -- 5 new Catch2 test cases |
| `docs/design/distributed.md` status updated, no other content changed unless proven wrong | done -- nothing was proven wrong; only the `## Status` line changed |
