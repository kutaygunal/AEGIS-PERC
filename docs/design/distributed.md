# Distributed Execution Architecture Aligned to Import and Job Pipeline

## Status
Draft — Phase 2 in progress (localhost multi-process host/worker split implemented for the
first increment; see orchestration/runs/run_2026-08-14_s3-003-distributed-phase2-localhost.md).
Phases 3 and 4 are not started.

## Purpose
Define a host/worker execution architecture for AEGIS-PERC that extends the Sprint 6 import model and the local job pipeline into a future distributed system.

This document is intentionally implementation-neutral. It identifies stable boundaries, artifact flow, failure handling, and infrastructure options without locking the project into a specific RPC or queue stack yet.

## Scope
Covers:
- host responsibilities
- worker responsibilities
- artifact handoff
- job queue and scheduling
- progress and result streaming
- localhost multi-process deployment
- future multi-node deployment
- retries, cancellation, partial availability, and resumable reporting

Does not yet commit to:
- a concrete transport library
- a database schema
- Kubernetes/cloud deployment specifics
- security hardening details beyond boundary guidance

## Existing foundations
Sprint 6 already introduced the key local abstractions this design should preserve:
- `aegis_storage::ProjectPackage` for versioned imported project/session state
- `aegis_storage::ImportPreflightValidator` for reusable validation
- `aegis_orchestration::LocalJobPipeline` for observable staged execution
- staged job lifecycle: `parse -> normalize -> graph_build -> rule_execution -> report_export`

These are the compatibility anchors for distributed execution.

## Architectural goals
- Keep **import and validation semantics identical** between desktop, CLI, localhost workers, and remote workers.
- Preserve **deterministic rule execution** regardless of worker placement.
- Make job state transitions observable to both UI and CLI.
- Support **single-host multi-process first**, then extend to multi-node execution.
- Decouple **artifact storage**, **queueing**, and **execution** so each can evolve independently.

## Module alignment
This design follows `docs/architecture/ADR-001-module-structure.md`:
- `aegis_storage`: project package, manifest, provenance, validation state
- `aegis_parsing`: file parsing
- `aegis_graph`: graph construction and enrichment
- `aegis_rules`: deterministic rule execution
- `aegis_reporting`: JSON/HTML export
- `aegis_orchestration`: job lifecycle, host/worker coordination
- `aegis_ui` / `aegis_scripting`: clients of orchestration, not execution engines themselves

The distributed layer should be introduced under orchestration/infrastructure boundaries, not by moving parsing/rules into UI or CLI code.

## Core design

### 1. Host
The host is the control plane.

Responsibilities:
- accept project submissions from UI or CLI
- run import preflight using the same `ProjectPackage` + validator model
- assign a stable `job_id`
- materialize or reference input artifacts
- enqueue execution stages
- dispatch work to available workers
- aggregate progress and final results
- manage cancellation, retries, and terminal state
- publish stable status snapshots to UI/CLI

The host should not require Qt Widgets. A desktop app may embed a localhost host process, but the orchestration API itself must remain headless.

### 2. Worker
The worker is the data-plane executor.

Responsibilities:
- claim queued work
- fetch job package metadata and referenced artifacts
- perform parse / normalize / graph build / rule execution / report export
- stream stage progress back to host
- write result artifacts
- report success, failure, or cancellation acknowledgement

Workers should execute the same core libraries used locally. They must not fork independent parsing or rule semantics.

### 3. Artifact store
Execution should not assume all files are embedded in queue messages.

Artifact store responsibilities:
- persist submitted source files or references
- expose immutable job input snapshots
- store generated outputs: JSON report, HTML summary, logs, optional intermediate bundles
- support local filesystem first, object storage later

### 4. Queue / scheduler
The queue is responsible for durable dispatch.

Responsibilities:
- pending/running/retry/cancel state tracking
- worker lease or claim model
- retry backoff
- stale-worker timeout recovery
- fairness / resource-aware routing in future

## Canonical execution flow
1. Client submits imported project or manifest.
2. Host validates package using `ProjectPackage` + preflight validator.
3. Host stores artifacts and creates a job record.
4. Host enqueues staged execution request.
5. Worker claims the job.
6. Worker resolves artifact paths/handles from the artifact store.
7. Worker runs stages:
   - parse
   - normalize
   - graph_build
   - rule_execution
   - report_export
8. Worker streams progress snapshots to host.
9. Worker writes result artifacts.
10. Host marks job complete and exposes result metadata.
11. UI/CLI fetches summaries and downloads/export paths.

## Job model
A distributed job should preserve the same observable concepts already present in `LocalJobPipeline`.

Recommended stable fields:
- `job_id`
- project/package reference
- current state: queued/running/cancelling/cancelled/completed/failed
- current stage
- completed stage count / total stage count
- cancellation requested flag
- worker assignment
- output artifact references
- error summary
- timestamps: created, started, updated, finished

## Project package handoff
`ProjectPackage` is the control document for distributed execution.

The host should treat it as:
- the normalized declaration of source artifact roles
- the provenance carrier
- the worker input contract
- the reporting metadata source

Recommended handoff model:
- store the raw package manifest/versioned JSON
- rewrite artifact paths into host-managed artifact references if needed
- preserve original relative-path provenance in metadata
- avoid mutating the semantic meaning of artifact roles during dispatch

### Why this matters
Without a stable package handoff, worker behavior can drift from UI/CLI import behavior. The distributed design should therefore treat the imported package as the source of truth, not rebuild role inference on each worker.

## Artifact handoff patterns

### Option A: Shared filesystem (localhost MVP)
Host and workers share a filesystem root.

Pros:
- simplest localhost multi-process model
- easiest debugging
- no object store dependency

Cons:
- weaker isolation
- path portability issues across nodes
- limited scalability

Best for:
- first multi-process implementation on a single machine

### Option B: Content-addressed local object store
Host writes artifacts into a managed local blob store and workers fetch by ID.

Pros:
- cleaner host/worker contract
- resumable and cache-friendly
- easier future transition to remote object storage

Cons:
- more implementation work

### Option C: Remote object storage
Artifacts stored in S3-compatible/blob/object storage.

Pros:
- multi-node ready
- clean separation of control plane vs data plane

Cons:
- requires auth, upload/download policy, cache strategy

## Progress streaming
Progress must remain stage-oriented and script-friendly.

Recommended behavior:
- workers emit explicit stage transitions
- workers may emit optional sub-progress within a stage
- host converts worker events into stable job snapshots
- UI and CLI poll or subscribe to the same host-facing progress model

Minimum progress payload:
- `job_id`
- state
- stage
- message
- completed stage count
- total stage count
- cancel requested flag

Future optional payload:
- percentage within current stage
- estimated remaining time
- worker hostname/id
- active artifact/file being processed
- warning counters

## Result export model
Workers should produce durable result artifacts, not only transient stdout.

Minimum outputs:
- JSON report
- HTML summary
- execution log / diagnostics stream

Recommended future outputs:
- packaged job bundle
- partial-stage checkpoints
- imported/normalized package snapshot used for the run
- optional ML feature outputs

The host should expose artifact references rather than requiring clients to inspect worker-local paths.

## Localhost multi-process scenario
This is the first recommended distributed deployment mode.

### Topology
- desktop app or CLI starts/submits to a local host service
- host runs as a separate process
- one or more local worker processes claim jobs
- shared filesystem or local blob store provides artifacts

### Benefits
- validates host/worker boundaries early
- allows cancellation and worker crash recovery independent of UI
- preserves current single-machine assumptions while removing direct in-process execution coupling

### Key requirement
`LocalJobPipeline` should evolve into a host-side abstraction, with a local worker adapter underneath, rather than being discarded.

## Future multi-node scenario
### Topology
- one host service manages submission and scheduling
- many workers run on different machines
- shared artifact store/object storage holds inputs/outputs
- queue coordinates claims and retries
- progress events stream back over RPC/message bus

### Added concerns
- machine capability tagging
- worker version compatibility
- artifact locality/caching
- authn/authz
- encrypted artifact transport
- host failover and durable job state

## Failure modes and handling

### 1. Partial artifact availability
Examples:
- manifest exists but one referenced CSV is missing
- upload succeeded for DEF but failed for rule pack
- worker can read package metadata but not one artifact blob

Handling:
- host should fail submission before queueing if required artifacts are absent
- optional artifacts may downgrade to warning only if contract allows it
- worker should report a typed artifact-resolution failure if a supposedly committed artifact becomes unavailable
- host should mark the job failed or retry depending on root cause and durability guarantees

### 2. Worker crash during execution
Handling:
- queue lease/heartbeat expires
- host returns job to pending/retry state if retry policy allows
- partial output files must not be published as final outputs
- artifact export should use temp files + atomic promote semantics

### 3. Host crash or restart
Handling:
- job metadata must be durably stored outside process memory for real distributed mode
- running jobs should be reconciled on restart via worker heartbeats/leases
- completed outputs should remain discoverable from the artifact store

### 4. Cancellation
Handling:
- host marks job as cancelling
- worker observes cancellation between safe stage boundaries and, later, at finer checkpoints inside long stages
- worker stops before publishing final outputs when cancellation wins the race
- temp outputs are cleaned up or left clearly non-final
- terminal state becomes `cancelled`, not `failed`

### 5. Retry after transient failure
Handling:
- retry only idempotent stages or rerun full job from stable inputs
- prefer full-job rerun first for simplicity
- record retry count and prior failure cause
- keep deterministic rule outputs by using immutable inputs per retry

### 6. Resumable reporting
Future design option:
- after successful rule execution, report export may be retried without repeating parse/rule stages if a stable execution snapshot exists
- this requires persistent intermediate state definitions and version compatibility rules

MVP recommendation:
- do not implement partial-stage resume yet
- do design result export APIs so report generation can later be rerun from a persisted rule-result bundle

## Retry policy guidance
Recommended initial policy:
- no automatic retry for deterministic schema/import errors
- retry allowed for transport, lease, or temporary artifact-store errors
- cap retries with exponential backoff
- preserve all prior failure diagnostics in job history

## Versioning and compatibility
The host must validate compatibility across:
- manifest/package version
- rule pack schema version
- worker software version
- optional intermediate snapshot version

Recommended policy:
- host only dispatches to workers advertising compatible execution capabilities
- incompatible workers refuse claims or are filtered by the scheduler

## Security and trust boundaries
Even in a local-first system, define boundaries early:
- imported customer artifacts may be sensitive
- workers should receive least-privilege access to required artifacts only
- logs and reports must avoid leaking secrets unnecessarily
- future remote mode should support encrypted transport and at-rest protection

## External dependency options
The following libraries/services are plausible candidates. None are mandated yet.

### RPC / progress streaming
- gRPC
  - strong typed APIs
  - bidirectional streaming for progress/events
  - good fit for host/worker RPC
- ZeroMQ
  - flexible messaging patterns
  - lighter-weight but more custom protocol work
- plain HTTP + SSE/WebSocket
  - simpler operational model for host/client progress APIs

### Queue / coordination
- Redis
  - simple durable-enough queueing/leases for early deployments
  - useful for retries and state handoff
- PostgreSQL-backed queue
  - strong durability, fewer moving parts for some teams
- custom filesystem queue
  - acceptable only for earliest localhost experiments

### Artifact storage
- local filesystem
- S3-compatible object storage (MinIO, AWS S3, Ceph RGW)
- Azure Blob / GCS equivalents in enterprise deployments

## Recommended evolution path
### Phase 1: local in-process (already present)
- `LocalJobPipeline` with observable stages and export

### Phase 2: localhost multi-process
- split host process from worker process
- keep shared filesystem artifact handoff
- add lease/heartbeat and durable job metadata

### Phase 3: localhost blob-backed
- replace raw path sharing with artifact IDs
- keep single host, multiple local workers

### Phase 4: multi-node distributed
- remote artifact store
- queue-backed scheduling
- worker capability registry
- progress streaming over RPC/message bus

## Open design questions
- Should host persist normalized intermediate state, or only raw package + final outputs?
- Is report export a worker responsibility, or can it become a dedicated reporting service later?
- Should workers claim full jobs or individual stages?
- How much sub-stage progress detail is worth standardizing now?
- What is the minimum durable job history needed for enterprise auditability?

## Acceptance mapping to P6-010
- Host, worker, artifact handoff, job queue, and progress streaming are defined.
- Localhost multi-process and future multi-node scenarios are covered.
- Failure modes include partial artifact availability, retries, cancellation, and resumable reporting.
- The design explicitly references the Sprint 6 project package/import model and local job abstractions.
