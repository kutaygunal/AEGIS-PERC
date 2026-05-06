#!/usr/bin/env python3
"""Print a fast project status summary from repo memory."""

import os
import sys
import glob
from datetime import datetime
from pathlib import Path

def find_latest_run_log(runs_dir: Path) -> Path | None:
    logs = sorted(runs_dir.glob("*.md"), reverse=True)
    return logs[0] if logs else None

def parse_tasks_yaml(tasks_path: Path) -> dict:
    try:
        import yaml
    except ImportError:
        print("Install PyYAML to parse tasks.yaml: pip install pyyaml")
        sys.exit(1)

    with open(tasks_path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)
    return data or {}

def summarize_tasks(tasks_path: Path) -> None:
    data = parse_tasks_yaml(tasks_path)
    tasks = data.get("tasks", [])

    statuses = {"todo": [], "ready": [], "in_progress": [], "blocked": [], "review": [], "done": []}
    for t in tasks:
        s = t.get("status", "unknown")
        statuses.setdefault(s, []).append(t)

    print("=" * 60)
    print("AEGIS-PERC Project Status")
    print("=" * 60)

    current = statuses.get("in_progress", [])
    if current:
        t = current[0]
        print(f"\nCurrent task: {t['id']} — {t['title']} (in_progress)")
    else:
        print("\nCurrent task: None")

    ready = statuses.get("ready", [])
    print(f"\nReady tasks ({len(ready)}):")
    for t in ready:
        print(f"  - {t['id']}: {t['title']}")

    blocked = statuses.get("blocked", [])
    print(f"\nBlocked tasks ({len(blocked)}):")
    for t in blocked:
        print(f"  - {t['id']}: {t['title']}")

    review = statuses.get("review", [])
    print(f"\nTasks in review ({len(review)}):")
    for t in review:
        print(f"  - {t['id']}: {t['title']}")

    done = statuses.get("done", [])
    print(f"\nCompleted tasks: {len(done)} / {len(tasks)}")

    if ready:
        t = ready[0]
        print(f"\nRecommended next task: {t['id']} — {t['title']}")
    elif review:
        print("\nRecommended next action: Review tasks awaiting approval.")
    else:
        print("\nRecommended next action: Check roadmap for new work.")

def summarize_handoff(handoff_path: Path) -> None:
    if not handoff_path.exists():
        return
    with open(handoff_path, "r", encoding="utf-8") as f:
        lines = f.readlines()
    # Print only the first non-empty sections for brevity
    print("\nHandoff snippet:")
    for line in lines[:20]:
        stripped = line.strip()
        if stripped:
            print(f"  {stripped}")

def summarize_latest_run(runs_dir: Path) -> None:
    latest = find_latest_run_log(runs_dir)
    if not latest:
        print("\nLast run: None")
        return
    with open(latest, "r", encoding="utf-8") as f:
        lines = f.readlines()
    print(f"\nLast run: {latest.name}")
    for line in lines[:15]:
        stripped = line.strip()
        if stripped:
            print(f"  {stripped}")

def main() -> None:
    repo_root = Path(__file__).resolve().parent.parent.parent
    orchestration = repo_root / "orchestration"
    tasks_path = orchestration / "tasks.yaml"
    handoff_path = orchestration / "HANDOFF.md"
    runs_dir = orchestration / "runs"

    summarize_tasks(tasks_path)
    summarize_handoff(handoff_path)
    summarize_latest_run(runs_dir)
    print("\n" + "=" * 60)

if __name__ == "__main__":
    main()
