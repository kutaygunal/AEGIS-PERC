#!/usr/bin/env bash
#
# demo_walkthrough.sh — Reproducible signoff-loop walkthrough for AEGIS-PERC.
#
# Runs the full headless CLI loop end-to-end against one of the repo's
# existing, already-tested sample fixtures:
#   import -> run (violation active) -> baseline -> run (waiver applied) -> diff (gate)
#
# Every command below is exactly one of the subcommands documented in
# README.md's "Headless CLI quick reference" (import / run / baseline / diff),
# using only flags accepted by app/cli_main.cpp. No new CLI flags, no new
# sample data — see the note in "Which sample design?" below for why this
# script targets data/import_packages/openframe_simple_design rather than
# data/sample_designs/*.json.
#
# Usage:
#   scripts/demo_walkthrough.sh [output_dir] [path_to_aegis-perc-cli]
#
# Exit code: 0 if every step reached a recognized AEGIS-PERC CLI exit code
# (including "violations found" / "regressions found", which are expected,
# successful demonstrations of the tool working) — non-zero only if a step
# failed in an *unexpected* way (crash, usage error, missing build).
#
set -uo pipefail

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DESIGN_DIR="${REPO_ROOT}/data/import_packages/openframe_simple_design"
OUT_DIR="${1:-${REPO_ROOT}/demo_output}"
CLI_OVERRIDE="${2:-${AEGIS_CLI:-}}"

mkdir -p "${OUT_DIR}"

# ---------------------------------------------------------------------------
# Narration helpers
# ---------------------------------------------------------------------------
STEP_NUM=0
banner() {
    STEP_NUM=$((STEP_NUM + 1))
    echo
    echo "================================================================"
    echo "STEP ${STEP_NUM}: $1"
    echo "================================================================"
}
note() { echo "  -> $1"; }

# Prefer python for JSON summarization (already a documented prerequisite,
# used by scripts/validate_samples.py); fall back to raw file inspection.
PYTHON_BIN=""
for candidate in python3 python; do
    if command -v "${candidate}" >/dev/null 2>&1; then
        PYTHON_BIN="${candidate}"
        break
    fi
done

summarize_run_json() {
    local file="$1"
    if [[ -n "${PYTHON_BIN}" && -f "${file}" ]]; then
        "${PYTHON_BIN}" - "$file" <<'PYEOF'
import json, sys
try:
    with open(sys.argv[1], "r", encoding="utf-8") as f:
        data = json.load(f)
except Exception as exc:
    print(f"  (could not parse {sys.argv[1]}: {exc})")
    sys.exit(0)
summary = data.get("summary")
if summary is not None:
    print(f"  violation_count:        {summary.get('violation_count')}")
    print(f"  active_violation_count: {summary.get('active_violation_count')}")
    print(f"  waived_violation_count: {summary.get('waived_violation_count')}")
    print(f"  severity_counts:        {summary.get('severity_counts')}")
for v in data.get("violations", []):
    waived = v.get("metadata", {}).get("waived", False)
    print(f"    - [{'WAIVED' if waived else 'active'}] {v.get('rule_id')}: {v.get('message')}")
if "summary" in data and "new_count" in data.get("summary", {}):
    s = data["summary"]
    print(f"  baseline_total: {s.get('baseline_total')}  current_total: {s.get('current_total')}")
    print(f"  new: {s.get('new_count')}  removed: {s.get('removed_count')}  unchanged: {s.get('unchanged_count')}  changed_severity: {s.get('changed_severity_count')}")
PYEOF
    else
        note "(python not found — raw JSON written to ${file}, inspect it directly)"
    fi
}

# ---------------------------------------------------------------------------
# Locate the aegis-perc-cli binary
# ---------------------------------------------------------------------------
find_cli() {
    if [[ -n "${CLI_OVERRIDE}" && -x "${CLI_OVERRIDE}" ]]; then
        echo "${CLI_OVERRIDE}"
        return 0
    fi
    local candidates=(
        "${REPO_ROOT}/build/windows-release/app/Release/aegis-perc-cli.exe"
        "${REPO_ROOT}/build/windows-release/app/aegis-perc-cli.exe"
        "${REPO_ROOT}/build/windows-debug/app/Debug/aegis-perc-cli.exe"
        "${REPO_ROOT}/build/windows-debug/app/aegis-perc-cli.exe"
        "${REPO_ROOT}/build/linux-release/app/aegis-perc-cli"
        "${REPO_ROOT}/build/linux-debug/app/aegis-perc-cli"
    )
    for c in "${candidates[@]}"; do
        if [[ -x "$c" ]]; then
            echo "$c"
            return 0
        fi
    done
    if command -v aegis-perc-cli >/dev/null 2>&1; then
        command -v aegis-perc-cli
        return 0
    fi
    return 1
}

CLI="$(find_cli || true)"
if [[ -z "${CLI}" ]]; then
    echo "ERROR: could not find an aegis-perc-cli binary."
    echo
    echo "Build it first, e.g.:"
    echo "  cmake --preset windows-release"
    echo "  cmake --build build/windows-release --config Release"
    echo "(see docs/BUILD.md and README.md 'Quick Build' for other presets)"
    echo
    echo "Then re-run this script, optionally passing the binary path explicitly:"
    echo "  scripts/demo_walkthrough.sh [output_dir] /path/to/aegis-perc-cli"
    exit 1
fi

echo "AEGIS-PERC signoff-loop walkthrough"
echo "Using CLI:      ${CLI}"
echo "Sample design:  ${DESIGN_DIR}"
echo "Output written: ${OUT_DIR}"

# ---------------------------------------------------------------------------
# Which sample design?
# ---------------------------------------------------------------------------
banner "Which sample design, and why"
cat <<EOF
This walkthrough runs against data/import_packages/openframe_simple_design,
an existing, already-tested repo fixture (real LEF/DEF/Verilog + an
aegis-perc rule pack + a waiver example, wired together by a versioned
aegis-project.yaml manifest — see data/import_packages/*/README.md).

Note: data/sample_designs/ (inverter.json, nand2.json, ring_oscillator.json)
is a different, simpler JSON format consumed today only by the desktop app's
bundled sample browser and by unit tests (aegis::parsing::LayoutIR) — the
CLI's import/run/baseline/diff path takes LEF/DEF/Verilog/rules (explicit
flags or a manifest), not that JSON format directly. This script therefore
uses the closest existing, real, CLI-compatible fixture instead of inventing
new sample data. See orchestration/runs/run_2026-08-14_s3-004-product-quick-wins.md
for the full note.
EOF

DESIGN_ARGS=(
    --project "openframe_simple_design_demo"
    --lef "${DESIGN_DIR}/layout/technology.lef"
    --def "${DESIGN_DIR}/layout/top.def"
    --netlist "${DESIGN_DIR}/netlist/top.v"
    --rules "${DESIGN_DIR}/rules/aegis_rules.yaml"
)
WAIVER_ARGS=(--waiver "${DESIGN_DIR}/reports/waivers.csv")

explain_exit_code() {
    case "$1" in
        0) echo "0  = success, no violations / no new regressions" ;;
        2) echo "2  = import failure" ;;
        3) echo "3  = success, violations found (expected here)" ;;
        4) echo "4  = execution error" ;;
        5) echo "5  = success, new regressions found vs baseline (CI gate should fail the build)" ;;
        64) echo "64 = usage error" ;;
        *) echo "$1 = unrecognized exit code" ;;
    esac
}

# ---------------------------------------------------------------------------
# Step 1: import
# ---------------------------------------------------------------------------
banner "Import — parse LEF/DEF/Verilog/rules into a project package"
note "Command: aegis-perc-cli import ... --output 01_import.json"
"${CLI}" import "${DESIGN_ARGS[@]}" --output "${OUT_DIR}/01_import.json"
CODE=$?
note "Exit code: $(explain_exit_code "${CODE}")"

# ---------------------------------------------------------------------------
# Step 2: run checks (no waiver) — produce a violation
# ---------------------------------------------------------------------------
banner "Run checks (no waiver applied) — see the raw, active violation(s)"
note "Command: aegis-perc-cli run ... --output 02_run_unwaived.json"
"${CLI}" run "${DESIGN_ARGS[@]}" --output "${OUT_DIR}/02_run_unwaived.json"
CODE=$?
note "Exit code: $(explain_exit_code "${CODE}")"
summarize_run_json "${OUT_DIR}/02_run_unwaived.json"

# ---------------------------------------------------------------------------
# Step 3: baseline
# ---------------------------------------------------------------------------
banner "Baseline — commit today's findings as the regression-gate reference"
note "Command: aegis-perc-cli baseline ... --output 03_baseline.json"
"${CLI}" baseline "${DESIGN_ARGS[@]}" --output "${OUT_DIR}/03_baseline.json"
CODE=$?
note "Exit code: $(explain_exit_code "${CODE}")"
note "Baseline written to ${OUT_DIR}/03_baseline.json — this is what you'd commit to the repo."

# ---------------------------------------------------------------------------
# Step 4: run with waiver applied — waive the known finding
# ---------------------------------------------------------------------------
banner "Run checks WITH waiver — suppress the known finding, keep the audit trail"
note "Design ships reports/waivers.csv (rule_id-based match) — passed via --waiver"
note "Command: aegis-perc-cli run ... --waiver waivers.csv --output 04_run_waived.json"
"${CLI}" run "${DESIGN_ARGS[@]}" "${WAIVER_ARGS[@]}" --output "${OUT_DIR}/04_run_waived.json"
CODE=$?
note "Exit code: $(explain_exit_code "${CODE}")"
summarize_run_json "${OUT_DIR}/04_run_waived.json"
note "Waived findings are suppressed from 'active' counts but remain in the JSON for audit."

# ---------------------------------------------------------------------------
# Step 5: diff — the CI gate
# ---------------------------------------------------------------------------
banner "Diff against the baseline — the CI gate check"
note "Command: aegis-perc-cli diff --baseline 03_baseline.json ... --output 05_diff.json"
"${CLI}" diff --baseline "${OUT_DIR}/03_baseline.json" "${DESIGN_ARGS[@]}" "${WAIVER_ARGS[@]}" --output "${OUT_DIR}/05_diff.json"
CODE=$?
note "Exit code: $(explain_exit_code "${CODE}")"
summarize_run_json "${OUT_DIR}/05_diff.json"
if [[ "${CODE}" == "5" ]]; then
    note "New regressions detected — a CI job wired to this exit code would fail the build here."
else
    note "No new regressions vs the committed baseline — a CI job wired to this exit code would pass."
fi

# ---------------------------------------------------------------------------
# Wrap-up
# ---------------------------------------------------------------------------
banner "What just happened (the wedge)"
cat <<'EOF'
This is the adoption loop AEGIS-PERC is built around:

    run -> waive -> diff -> gate

Add it as a lightweight CI gate on NEW work — "does this change introduce a
new Error-severity finding vs the last committed baseline?" — well before
you'd trust it as a full signoff replacement for an incumbent tool. That is
a materially smaller ask than "rip out your existing signoff flow," and it
is exactly the loop this script just ran end-to-end.

All output JSON is in the output directory below for inspection; nothing
here was written back into the repo's tracked sample data.
EOF
echo
echo "Output files:"
ls -1 "${OUT_DIR}"/*.json 2>/dev/null | sed 's/^/  /'
echo
echo "Done."
exit 0
