<#
.SYNOPSIS
    Reproducible signoff-loop walkthrough for AEGIS-PERC (Windows/PowerShell).

.DESCRIPTION
    Runs the full headless CLI loop end-to-end against one of the repo's
    existing, already-tested sample fixtures:
      import -> run (violation active) -> baseline -> run (waiver applied) -> diff (gate)

    Every command below is exactly one of the subcommands documented in
    README.md's "Headless CLI quick reference" (import / run / baseline /
    diff), using only flags accepted by app/cli_main.cpp. No new CLI flags,
    no new sample data — see the "Which sample design?" banner below for why
    this script targets data/import_packages/openframe_simple_design rather
    than data/sample_designs/*.json.

.PARAMETER OutDir
    Directory to write step-by-step JSON output into. Defaults to
    <repo root>/demo_output.

.PARAMETER CliPath
    Explicit path to the aegis-perc-cli(.exe) binary. If omitted, the script
    searches common CMake preset build output locations, then PATH.

.EXAMPLE
    scripts/demo_walkthrough.ps1
    scripts/demo_walkthrough.ps1 -OutDir C:\temp\aegis_demo -CliPath build\windows-release\app\Release\aegis-perc-cli.exe
#>

param(
    [string]$OutDir,
    [string]$CliPath
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$DesignDir = Join-Path $RepoRoot "data\import_packages\openframe_simple_design"

if (-not $OutDir) { $OutDir = Join-Path $RepoRoot "demo_output" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$script:StepNum = 0
function Write-Banner([string]$Title) {
    $script:StepNum++
    Write-Host ""
    Write-Host "================================================================"
    Write-Host "STEP $($script:StepNum): $Title"
    Write-Host "================================================================"
}
function Write-Note([string]$Text) { Write-Host "  -> $Text" }

function Explain-ExitCode([int]$Code) {
    switch ($Code) {
        0  { "0  = success, no violations / no new regressions" }
        2  { "2  = import failure" }
        3  { "3  = success, violations found (expected here)" }
        4  { "4  = execution error" }
        5  { "5  = success, new regressions found vs baseline (CI gate should fail the build)" }
        64 { "64 = usage error" }
        default { "$Code = unrecognized exit code" }
    }
}

function Summarize-RunJson([string]$Path) {
    if (-not (Test-Path $Path)) {
        Write-Note "(no output file at $Path)"
        return
    }
    try {
        $data = Get-Content $Path -Raw | ConvertFrom-Json
    } catch {
        Write-Note "(could not parse ${Path}: $_)"
        return
    }
    if ($data.summary) {
        $s = $data.summary
        Write-Host "  violation_count:        $($s.violation_count)"
        Write-Host "  active_violation_count: $($s.active_violation_count)"
        Write-Host "  waived_violation_count: $($s.waived_violation_count)"
        Write-Host "  severity_counts:        $($s.severity_counts | ConvertTo-Json -Compress)"
        if ($null -ne $s.new_count) {
            Write-Host "  baseline_total: $($s.baseline_total)  current_total: $($s.current_total)"
            Write-Host "  new: $($s.new_count)  removed: $($s.removed_count)  unchanged: $($s.unchanged_count)  changed_severity: $($s.changed_severity_count)"
        }
    }
    if ($data.violations) {
        foreach ($v in $data.violations) {
            $waived = $false
            if ($v.metadata -and $v.metadata.waived) { $waived = $true }
            $tag = if ($waived) { "WAIVED" } else { "active" }
            Write-Host "    - [$tag] $($v.rule_id): $($v.message)"
        }
    }
}

function Find-Cli {
    if ($CliPath -and (Test-Path $CliPath)) { return (Resolve-Path $CliPath).Path }
    if ($env:AEGIS_CLI -and (Test-Path $env:AEGIS_CLI)) { return (Resolve-Path $env:AEGIS_CLI).Path }
    $candidates = @(
        "build\windows-release\app\Release\aegis-perc-cli.exe",
        "build\windows-release\app\aegis-perc-cli.exe",
        "build\windows-debug\app\Debug\aegis-perc-cli.exe",
        "build\windows-debug\app\aegis-perc-cli.exe"
    )
    foreach ($c in $candidates) {
        $full = Join-Path $RepoRoot $c
        if (Test-Path $full) { return $full }
    }
    $onPath = Get-Command aegis-perc-cli.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    return $null
}

$Cli = Find-Cli
if (-not $Cli) {
    Write-Host "ERROR: could not find an aegis-perc-cli(.exe) binary."
    Write-Host ""
    Write-Host "Build it first, e.g.:"
    Write-Host "  cmake --preset windows-release"
    Write-Host "  cmake --build build/windows-release --config Release"
    Write-Host "(see docs/BUILD.md and README.md 'Quick Build' for other presets)"
    Write-Host ""
    Write-Host "Then re-run this script, optionally passing the binary path explicitly:"
    Write-Host "  scripts/demo_walkthrough.ps1 -CliPath <path to aegis-perc-cli.exe>"
    exit 1
}

Write-Host "AEGIS-PERC signoff-loop walkthrough"
Write-Host "Using CLI:      $Cli"
Write-Host "Sample design:  $DesignDir"
Write-Host "Output written: $OutDir"

Write-Banner "Which sample design, and why"
@"
This walkthrough runs against data/import_packages/openframe_simple_design,
an existing, already-tested repo fixture (real LEF/DEF/Verilog + an
aegis-perc rule pack + a waiver example, wired together by a versioned
aegis-project.yaml manifest -- see data/import_packages/*/README.md).

Note: data/sample_designs/ (inverter.json, nand2.json, ring_oscillator.json)
is a different, simpler JSON format consumed today only by the desktop app's
bundled sample browser and by unit tests (aegis::parsing::LayoutIR) -- the
CLI's import/run/baseline/diff path takes LEF/DEF/Verilog/rules (explicit
flags or a manifest), not that JSON format directly. This script therefore
uses the closest existing, real, CLI-compatible fixture instead of inventing
new sample data. See orchestration/runs/run_2026-08-14_s3-004-product-quick-wins.md
for the full note.
"@ | Write-Host

$DesignArgs = @(
    "--project", "openframe_simple_design_demo",
    "--lef", (Join-Path $DesignDir "layout\technology.lef"),
    "--def", (Join-Path $DesignDir "layout\top.def"),
    "--netlist", (Join-Path $DesignDir "netlist\top.v"),
    "--rules", (Join-Path $DesignDir "rules\aegis_rules.yaml")
)
$WaiverArgs = @("--waiver", (Join-Path $DesignDir "reports\waivers.csv"))

# --- Step 1: import ---------------------------------------------------------
Write-Banner "Import - parse LEF/DEF/Verilog/rules into a project package"
Write-Note "Command: aegis-perc-cli import ... --output 01_import.json"
& $Cli import @DesignArgs --output (Join-Path $OutDir "01_import.json")
Write-Note "Exit code: $(Explain-ExitCode $LASTEXITCODE)"

# --- Step 2: run checks (no waiver) -----------------------------------------
Write-Banner "Run checks (no waiver applied) - see the raw, active violation(s)"
Write-Note "Command: aegis-perc-cli run ... --output 02_run_unwaived.json"
& $Cli run @DesignArgs --output (Join-Path $OutDir "02_run_unwaived.json")
Write-Note "Exit code: $(Explain-ExitCode $LASTEXITCODE)"
Summarize-RunJson (Join-Path $OutDir "02_run_unwaived.json")

# --- Step 3: baseline --------------------------------------------------------
Write-Banner "Baseline - commit today's findings as the regression-gate reference"
Write-Note "Command: aegis-perc-cli baseline ... --output 03_baseline.json"
& $Cli baseline @DesignArgs --output (Join-Path $OutDir "03_baseline.json")
Write-Note "Exit code: $(Explain-ExitCode $LASTEXITCODE)"
Write-Note "Baseline written to $OutDir\03_baseline.json -- this is what you'd commit to the repo."

# --- Step 4: run with waiver applied ----------------------------------------
Write-Banner "Run checks WITH waiver - suppress the known finding, keep the audit trail"
Write-Note "Design ships reports\waivers.csv (rule_id-based match) - passed via --waiver"
Write-Note "Command: aegis-perc-cli run ... --waiver waivers.csv --output 04_run_waived.json"
& $Cli run @DesignArgs @WaiverArgs --output (Join-Path $OutDir "04_run_waived.json")
Write-Note "Exit code: $(Explain-ExitCode $LASTEXITCODE)"
Summarize-RunJson (Join-Path $OutDir "04_run_waived.json")
Write-Note "Waived findings are suppressed from 'active' counts but remain in the JSON for audit."

# --- Step 5: diff (the CI gate) ---------------------------------------------
Write-Banner "Diff against the baseline - the CI gate check"
Write-Note "Command: aegis-perc-cli diff --baseline 03_baseline.json ... --output 05_diff.json"
& $Cli diff --baseline (Join-Path $OutDir "03_baseline.json") @DesignArgs @WaiverArgs --output (Join-Path $OutDir "05_diff.json")
$diffCode = $LASTEXITCODE
Write-Note "Exit code: $(Explain-ExitCode $diffCode)"
Summarize-RunJson (Join-Path $OutDir "05_diff.json")
if ($diffCode -eq 5) {
    Write-Note "New regressions detected - a CI job wired to this exit code would fail the build here."
} else {
    Write-Note "No new regressions vs the committed baseline - a CI job wired to this exit code would pass."
}

# --- Wrap-up -----------------------------------------------------------------
Write-Banner "What just happened (the wedge)"
@"
This is the adoption loop AEGIS-PERC is built around:

    run -> waive -> diff -> gate

Add it as a lightweight CI gate on NEW work -- "does this change introduce a
new Error-severity finding vs the last committed baseline?" -- well before
you'd trust it as a full signoff replacement for an incumbent tool. That is
a materially smaller ask than "rip out your existing signoff flow," and it
is exactly the loop this script just ran end-to-end.

All output JSON is in the output directory below for inspection; nothing
here was written back into the repo's tracked sample data.
"@ | Write-Host

Write-Host ""
Write-Host "Output files:"
Get-ChildItem (Join-Path $OutDir "*.json") -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "  $($_.FullName)" }
Write-Host ""
Write-Host "Done."
exit 0
