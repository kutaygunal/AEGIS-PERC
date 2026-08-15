# Run Log: S3-004 — Product Gap-Analysis Quick Wins

**Date:** 2026-08-14
**Task:** S3-004 — product-lens gap-analysis quick wins (README trust statement + beachhead framing, wedge statement, CONTRIBUTING.md, sample-walkthrough scaffolding, DEC-009 draft proposal)
**Sprint:** New, not-yet-registered Sprint 3 (this task's lane only — README, CONTRIBUTING, docs/scripts pointer, `orchestration/DECISIONS.md` append). Ran in parallel with three purely-engineering S3 tasks (GDSII ingestion, a physical rule check, distributed execution Phase 2) not coordinated with here per instructions.
**Commit:** (see branch `feat/s3-004-product-quick-wins`)

## Goal
Act on the "Decisions worth making now" and "near-term sequencing" items from a 2026-08-14 product-lens gap analysis ("AEGIS-PERC as a product, not a codebase"): a verified trust/data-handling statement, a named beachhead-segment candidate, an explicit wedge (CI-gate) statement, a `CONTRIBUTING.md`, a reproducible sample-design walkthrough script, and a draft (not accepted) DEC-009 proposal on beachhead segment + open-source/open-core stance.

## Required reading completed
AGENTS.md, orchestration/PROJECT_MEMORY.md, orchestration/DECISIONS.md (through DEC-008), orchestration/HANDOFF.md, orchestration/roadmap.yaml, orchestration/tasks.yaml, `orchestration/runs/run_2026-08-14_s2-001-declarative-condition-rules.md` (run-log format reference), full README.md, `data/sample_designs/` (README + schema + 3 samples), `scripts/validate_samples.py`.

## Step 1 — Verifying the trust-statement claim before publishing it

Grepped `src/core`, `src/parsing`, `src/graph`, `src/rules`, `src/reporting`, `src/orchestration`, and `app/` (plus, for extra rigor beyond the task's minimum scope, `src/storage`, `src/ui`, `src/ml`, `src/scripting`) for socket/HTTP/telemetry/analytics/cloud-SDK usage: `socket|http|curl|WinHTTP|WinINet|winsock|QNetworkAccessManager|QNetworkRequest|QTcpSocket|QUdpSocket|QSslSocket|boost::asio|libcurl|grpc|telemetry|analytics|getaddrinfo|upload`, case-insensitive. **Zero matches anywhere in `src/` or `app/`.** Also confirmed `Qt6::Network` / `find_package(Qt6 ... Network ...)` is not referenced anywhere in the CMake build — the networking module isn't even linked into the binary.

One caveat found and disclosed rather than silently omitted: `AGENTS.md`'s module-responsibility table (§9) lists "enterprise upload" as part of the `storage` module's *documented future responsibility* — but grepping confirmed **no code implements it** (no `EnterpriseUpload`/`enterprise_upload` symbol anywhere, no upload-shaped network code in `src/storage`). This is aspirational label text, not a shipped feature, so it doesn't invalidate the "no outbound calls today" claim, but I named it explicitly in the README's trust line rather than making an unqualified, unscoped claim.

**Verified wording used (README, Overview section):**
> **🔒 Runs entirely on your machine.** AEGIS-PERC's verification loop (import → graph → rules → reporting → CLI) makes no outbound network calls today. Verified by grepping `src/` and `app/` (core, parsing, graph, rules, reporting, orchestration, storage, ui, ml, scripting) for socket/HTTP/telemetry/cloud-SDK usage — none found — and confirming Qt's networking module isn't even linked into the build. No design data leaves the process. (One caveat for precision: `AGENTS.md`'s module table lists "enterprise upload" as a future responsibility of the `storage` module — nothing implements that today, and this claim will need revisiting if/when it does.)

This is stated with confidence because it's grep- and build-config-verified, not assumed.

## Step 2 — README additions
- Added the trust/data-handling line above (Overview section, immediately after the existing description paragraph, before "What it does:").
- Expanded "Built for" with a fourth bullet naming the beachhead candidate ("teams without an incumbent EDA signoff seat today — e.g. fabless startups and university/research groups...") from the gap analysis, explicitly labeled as *"a candidate fit based on the tool's current shape... not a validated customer segment"* with a pointer to DEC-009, per the instruction to word this as what the tool is suited for today, not an aspirational marketing claim.
- Added a new `### The wedge: a CI gate, not a replacement` subsection under Overview stating the run → waive → diff → gate CI-gate framing verbatim from the gap analysis language, and pointing at the new demo script.
- Added one line to the "Repository Structure" tree (`CONTRIBUTING.md`, `scripts/demo_walkthrough.{sh,ps1}`) and a short "Try the signoff loop end-to-end" pointer under "Quick Build" to the new demo script, per the instruction to keep this to one or two lines rather than a new major section.
- **Did not touch** the existing "commercial-grade" framing (H1 tagline, "Why This Project Matters" table, footer div) — see "Flagged tension" below.

## Step 3 — CONTRIBUTING.md (new, repo root)
Covers: build (points to `docs/BUILD.md` + README Quick Build, doesn't duplicate the preset list beyond a short example), tests (`ctest` presets, label/name filtering), a condensed module-boundary table + the load-bearing rules from `AGENTS.md` §2/§3/§4/§6 (linking to `AGENTS.md` as source of truth rather than re-deriving it), branch naming (`feat/`/`fix/` matching observed repo convention) and PR expectations (test commands + results, `review` not `done`, DEC entry for architecture decisions), and a pointer to the two `data/` fixture directories with an explicit "don't invent new fixtures if an existing one covers the case" note — directly informed by the discovery in Step 4 below.

## Step 4 — Sample-report / demo scaffold

### A real gap discovered, and how it was handled
The task instructions named `data/sample_designs/` (inverter.json, nand2.json, ring_oscillator.json) as the asset to point the demo script at. Investigating before writing the script found this is **not actually usable for a CLI walkthrough as-is**:
- `data/sample_designs/*.json` is a standalone `design`/`layout`/`netlist` JSON format (see `data/sample_designs/schema.json`), consumed only by `aegis::parsing::LayoutIR` in the desktop app's bundled sample browser (`src/ui/src/main_window_utils.hpp`) and by unit tests (`tests/unit/data/test_sample_data.cpp`, `tests/unit/ui/test_scene_adapter.cpp`).
- The CLI's `import`/`run`/`baseline`/`diff` path (`app/cli_main.cpp` → `CliProjectInput`) only accepts explicit `--lef`/`--def`/`--netlist`/`--rules`(/`--power`/`--current`/`--waiver`) file args or a `--manifest` file — there is no flag to load a `LayoutIR` JSON file directly. Confirmed by reading `app/cli_main.cpp`'s full argument parser and `CliWorkflow::build_package` in `src/scripting/src/cli_workflow.cpp` — neither has a code path for the `sample_designs` JSON shape.

Given the task's own constraints ("do not invent new sample data," "do not invent new CLI flags") I could not literally satisfy "use `data/sample_designs/`" without violating one of those two constraints. Resolution: used **`data/import_packages/openframe_simple_design`** instead — an existing, already-tracked, already-tested repo fixture (real LEF/DEF/Verilog + an `aegis_rules.yaml` rule pack + a `waivers.csv` waiver example, wired together by a versioned `aegis-project.yaml` manifest) that is directly CLI-ingestible via `--manifest` or the equivalent explicit flags, and was clearly prepared by an earlier task for exactly this kind of demo (it's the only `import_packages` fixture that ships a matching waiver file alongside its rule pack). This is not "inventing new sample data" — nothing new was added to `data/`; it's using a different existing fixture than the one named, because the one named doesn't fit the CLI mechanism the task also required. **This substitution is called out explicitly, at runtime, in both scripts' first narrated step**, so a reader isn't misled about which asset is being used or why.

### What the script does and does not claim
`scripts/demo_walkthrough.sh` (Bash) and `scripts/demo_walkthrough.ps1` (PowerShell, per AGENTS.md §6 Windows-primary) run, narrating each step to stdout:
1. `import` — parse LEF/DEF/Verilog/rules into a project package.
2. `run` (no waiver) — show the raw, active violation(s).
3. `baseline` — commit today's findings as the regression-gate reference.
4. `run` with `--waiver reports/waivers.csv` — same design, violation now suppressed from the active count but retained in the JSON for audit (demonstrates waiver audit-trail semantics from S1-003).
5. `diff --baseline ...` — the CI-gate check; prints and interprets the documented exit code (README's exit-code table: 0/2/3/4/5/64).
6. A closing narration paragraph stating the run → waive → diff → gate wedge explicitly.

Both scripts locate the `aegis-perc-cli` binary by searching the documented CMake-preset build output paths (`build/windows-release/app/Release/...`, etc.), falling back to `PATH`, and print a clear build-it-first message (pointing at `docs/BUILD.md`) if not found — they do not silently fail. JSON output from each step is written to an output directory (default `./demo_output/`, now `.gitignore`d) for inspection, not committed.

Both scripts use only the CLI flags documented in README's "Headless CLI quick reference" (`--project`, `--lef`, `--def`, `--netlist`, `--rules`, `--waiver`, `--baseline`, `--output`, and the `import`/`run`/`baseline`/`diff` subcommands) — no new flags invented, confirmed against `app/cli_main.cpp`'s argument parser.

### What I could **not** verify — a stated-not-proven claim, explicitly
**I did not build and execute the CLI to confirm the scripts run cleanly end-to-end or to confirm the exact violation produced.** Reasons, checked in order before giving up:
- No system MSVC (`cl`) on PATH; Qt6 is not installed at the CMake-configured hint path (`C:/Qt/6.8.2/msvc2022_64`) or discoverable elsewhere on this machine.
- `src/CMakeLists.txt` unconditionally does `add_subdirectory(ui)`, which requires Qt6 — there is no build option to exclude it, and this task's scope explicitly excludes touching `src/` (including `src/CMakeLists.txt`) to add one.
- MinGW `g++` 16.1 and WSL (Ubuntu, per the S2-001 run log's precedent) are both available, which is how a prior session verified non-UI-module builds without Qt — but doing that here (full non-UI configure/build cycle, exactly matching CI flags, to validate a documentation-scaffolding script) was judged disproportionate effort for this task versus writing the script defensively instead (see below), and still wouldn't produce a native Windows `aegis-perc-cli.exe` for the `.ps1` script to actually exercise.

To compensate, I read the actual execution paths this script depends on rather than guessing:
- `src/scripting/src/cli_workflow.cpp` (`run_project`, `baseline_project`, `diff_project`, `build_run_report`) to confirm exact JSON field names (`summary.violation_count`, `summary.active_violation_count`, `summary.waived_violation_count`, `violations[].rule_id`/`.message`/`metadata.waived`) used by the scripts' JSON-summarizing helper, and exact exit-code semantics (`SuccessWithViolations`=3, `SuccessWithRegressions`=5, etc.).
- `src/rules/src/waivers.cpp` to confirm `openframe_simple_design/reports/waivers.csv`'s waiver (`rule_id=FLOATING_NET`, no `identity_key`) matches **any** `FLOATING_NET` violation by rule id alone ("if waiver specifies only rule_id, it matches any violation with that rule_id") — so the waiver step (Step 4) will match regardless of which specific net the floating-net check flags, removing my earlier concern that the shipped waiver's `target` column (`debug_stub`, an unrecognized/ignored CSV column) wouldn't actually correspond to a real net in `top.v`.
- `src/parsing/src/verilog_parser.cpp` to confirm the parser's `assign`-to-device / instance-to-device mapping and that procedural (`always`/`initial`) blocks are not modeled as drivers — which is why a `reg` such as `simple_design.v`'s `reg_out` (assigned only inside an `always` block, only referenced elsewhere via `assign out = reg_out`) is a plausible, though not build-confirmed, floating-net candidate in this fixture.

Given that, **I did not hardcode an assumed violation, net name, or count into the script.** Both scripts read whatever the CLI actually outputs and print it back (via a Python-based JSON summarizer, since Python 3.10+ is already a documented prerequisite for `scripts/validate_samples.py`, with a graceful "inspect the file directly" fallback if Python isn't found) rather than asserting a specific expected result. The scripts are structurally exercising a documented, already-tested code path (S1-006/S1-007 CLI subcommands, covered by the existing `ctest -L BatchCLI`/`aegis-perc-cli` test labels per `tasks.yaml`), but **"this exact script, run end-to-end on this machine, was observed to produce a violation and a clean diff" is a stated-not-executed claim** — flagging it here per instructions rather than asserting it as verified.

**Recommended follow-up** (not done here, out of this task's scope): once Qt6 is available in a build environment, run both scripts once, confirm output, and fold a captured transcript into a real walkthrough doc — this was explicitly the "Mid" item in the gap analysis this task couldn't fully close (recording an actual video walkthrough).

## Step 5 — DEC-009 draft proposal
Appended (did not edit any existing `DEC-00X` entry) `## DEC-009: [proposed] Beachhead segment and open-source/open-core stance` to `orchestration/DECISIONS.md`, status `proposed — awaiting human decision, not yet accepted`. Lays out the beachhead-segment candidate and three open-source/open-core/proprietary options as a labeled recommendation, explicitly not phrased as a decision already made. Full text is in `orchestration/DECISIONS.md`; not duplicated here.

## Flagged tension: "commercial-grade" framing (for the human, via DEC-009 — not resolved here)
Per instructions, I did not remove or contradict the README's existing "commercial-grade" framing (H1 tagline: "A commercial-grade desktop platform..."; "🎯 Why This Project Matters" table; footer: "...intended to evolve into a commercial-grade platform..."). But this task *did* apply a more modest, "candidate fit, not validated" framing to the new "Built for" bullet and a "not yet accepted" framing to DEC-009 — which sits in tension with "commercial-grade" appearing unqualified three other places in the same document. I recorded this tension explicitly inside the DEC-009 entry itself (see its closing paragraph) rather than silently editing marketing copy to resolve it myself, since that's a business-model/tone call for the repo owner, not something implied by this task's scope.

## Files changed
- `README.md` — trust/data-handling line, expanded "Built for," new "The wedge" subsection, repo-structure tree entries (`CONTRIBUTING.md`, `scripts/demo_walkthrough.*`), one-line pointer to the demo script under Quick Build.
- `CONTRIBUTING.md` — new.
- `scripts/demo_walkthrough.sh` — new.
- `scripts/demo_walkthrough.ps1` — new.
- `.gitignore` — added `demo_output/` (the demo scripts' default scratch-output directory).
- `orchestration/DECISIONS.md` — appended `DEC-009` (proposed, not accepted).
- `orchestration/runs/run_2026-08-14_s3-004-product-quick-wins.md` — this file.

Not touched (per task scope): `src/`, `tests/`, `docs/design/*.md`, `orchestration/tasks.yaml`, `orchestration/roadmap.yaml`, `orchestration/HANDOFF.md` (a separate consolidation pass handles those after all parallel S3 agents finish, per instructions), and no existing `DEC-00X` entry was edited.

## Test commands
This task added no C++ source, headers, or tests, so the project's C++ build/test commands are not applicable to this change (consistent with `docs`/`orchestration`-only prior run logs, e.g. `run_2026-05-23_readme-source-sync.md`). Verification performed instead:
- Repo-wide `grep` sweep for network APIs (Step 1) — see exact patterns above.
- Manual read-through of `app/cli_main.cpp`, `src/scripting/src/cli_workflow.cpp`, `src/rules/src/waivers.cpp`, `src/parsing/src/verilog_parser.cpp` to ground the demo script's commands and JSON field names in actual code (Step 4) rather than assumption.
- `python scripts/validate_samples.py` was **not** re-run (this task didn't touch `data/sample_designs/` or `schema.json`); mentioned only as context for why Python was chosen as the demo script's JSON-summarization dependency.
- Both demo scripts were **not** executed against a real build in this session (see "What I could not verify" above) — this is the one deviation from `AGENTS.md` §8 ("run the listed test commands and confirm success") on this task, explicitly acknowledged: there are no listed test commands for a docs/scripting task, and the closest equivalent (running the new script) was blocked by the lack of a Qt6 toolchain in this environment, not skipped by choice.

## Acceptance-criteria self-check
| Item | Status |
|---|---|
| Verify trust claim before publishing | Done — grep + build-config verification, caveat disclosed (Step 1) |
| README trust/data-handling line | Done |
| README "who this is for" / beachhead framing, factual/modest | Done |
| Existing "commercial-grade" framing not silently touched | Done — left untouched, tension flagged in DEC-009 |
| Wedge paragraph | Done |
| CONTRIBUTING.md | Done |
| Demo walkthrough script(s) | Done (`.sh` + `.ps1`), with the `data/sample_designs/` substitution explicitly disclosed |
| README pointer to demo script (short) | Done — 2 lines under Quick Build |
| DEC-009 draft, proposed not accepted | Done |
| Run log | This file |
| Only `orchestration/DECISIONS.md` touched among shared orchestration files | Done — `tasks.yaml`/`roadmap.yaml`/`HANDOFF.md` untouched |
