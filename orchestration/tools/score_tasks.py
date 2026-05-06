#!/usr/bin/env python3
"""
score_tasks.py

Scores orchestration/tasks.yaml quality and identifies weakest tasks.
Requires: PyYAML (pip install pyyaml)

Usage from repo root:
    python orchestration/tools/score_tasks.py
"""
import sys
from pathlib import Path
from collections import Counter

try:
    import yaml
except ImportError:
    print("Error: PyYAML is required. Install with: pip install pyyaml")
    sys.exit(1)

TASKS_PATH = Path("orchestration/tasks.yaml")

VALID_STATUS = {"todo", "ready", "in_progress", "blocked", "review", "done"}
VALID_PRIORITY = {"critical", "high", "medium", "low"}
VALID_RISK = {"high", "medium", "low"}

# Weights must sum to 100 or less; leftover is slack.
SCORE_WEIGHTS = {
    "has_title": 5,
    "title_length_ok": 5,
    "has_phase": 5,
    "has_area": 5,
    "valid_status": 5,
    "valid_priority": 5,
    "valid_risk": 5,
    "acceptance_nonempty": 10,
    "acceptance_detailed": 10,
    "implementation_rules_nonempty": 10,
    "implementation_rules_detailed": 10,
    "test_commands_nonempty": 10,
    "dependencies_valid": 5,
    "pr_sized": 10,
}


def load_tasks():
    if not TASKS_PATH.exists():
        print(f"Error: {TASKS_PATH} not found. Run this script from the repo root.")
        sys.exit(1)
    raw = TASKS_PATH.read_text(encoding="utf-8")
    try:
        data = yaml.safe_load(raw)
    except yaml.YAMLError as exc:
        print(f"YAML parse error:\n{exc}")
        sys.exit(1)
    return data.get("tasks", [])


def score_task(task, all_ids):
    score = 0
    details = []

    title = task.get("title", "")
    if title and isinstance(title, str):
        score += SCORE_WEIGHTS["has_title"]
        if 10 <= len(title) <= 100:
            score += SCORE_WEIGHTS["title_length_ok"]
        else:
            details.append("title too short or too long")
    else:
        details.append("missing title")

    if task.get("phase"):
        score += SCORE_WEIGHTS["has_phase"]
    else:
        details.append("missing phase")

    if task.get("area"):
        score += SCORE_WEIGHTS["has_area"]
    else:
        details.append("missing area")

    status = task.get("status")
    if status in VALID_STATUS:
        score += SCORE_WEIGHTS["valid_status"]
    else:
        details.append("invalid status")

    priority = task.get("priority")
    if priority in VALID_PRIORITY:
        score += SCORE_WEIGHTS["valid_priority"]
    else:
        details.append("invalid priority")

    risk = task.get("risk")
    if risk in VALID_RISK:
        score += SCORE_WEIGHTS["valid_risk"]
    else:
        details.append("invalid risk")

    deps = task.get("depends_on", [])
    bad_deps = [d for d in deps if d == task.get("id") or d not in all_ids]
    if not bad_deps:
        score += SCORE_WEIGHTS["dependencies_valid"]
    else:
        details.append(f"bad dependencies: {bad_deps}")

    acceptance = task.get("acceptance", [])
    if isinstance(acceptance, list) and all(isinstance(x, str) and x.strip() for x in acceptance):
        score += SCORE_WEIGHTS["acceptance_nonempty"]
        if len(acceptance) >= 3 and acceptance:
            avg_len = sum(len(x) for x in acceptance) / len(acceptance)
            if avg_len > 30:
                score += SCORE_WEIGHTS["acceptance_detailed"]
            else:
                details.append("acceptance criteria too vague")
        else:
            details.append("acceptance criteria too few")
    else:
        details.append("empty or invalid acceptance criteria")

    rules = task.get("implementation_rules", [])
    if isinstance(rules, list) and all(isinstance(x, str) and x.strip() for x in rules):
        score += SCORE_WEIGHTS["implementation_rules_nonempty"]
        if len(rules) >= 2 and rules:
            avg_len = sum(len(x) for x in rules) / len(rules)
            if avg_len > 20:
                score += SCORE_WEIGHTS["implementation_rules_detailed"]
            else:
                details.append("implementation rules too vague")
        else:
            details.append("implementation rules too few")
    else:
        details.append("empty or invalid implementation_rules")

    tests = task.get("test_commands", [])
    if isinstance(tests, list) and all(isinstance(x, str) and x.strip() for x in tests):
        score += SCORE_WEIGHTS["test_commands_nonempty"]
    else:
        details.append("empty or invalid test_commands")

    # PR-sized heuristic
    total_items = len(acceptance) + len(rules) + len(tests)
    if total_items <= 15 and len(acceptance) <= 7:
        score += SCORE_WEIGHTS["pr_sized"]
    else:
        details.append("likely oversized")

    return score, details


def main():
    tasks = load_tasks()
    if not tasks:
        print("No tasks found.")
        sys.exit(1)

    all_ids = set()
    for t in tasks:
        tid = t.get("id")
        if tid:
            all_ids.add(tid)

    scores = []
    title_counts = Counter()
    for task in tasks:
        tid = task.get("id", "???")
        title = task.get("title", "???")
        title_counts[title] += 1
        s, details = score_task(task, all_ids)
        scores.append({
            "id": tid,
            "title": title,
            "status": task.get("status", "???"),
            "score": s,
            "details": details,
        })

    avg_score = sum(s["score"] for s in scores) / len(scores)
    scores.sort(key=lambda x: x["score"])

    duplicated = [t for t, c in title_counts.items() if c > 1]

    missing_counts = Counter()
    for s in scores:
        for d in s["details"]:
            missing_counts[d] += 1

    print("=" * 60)
    print("TASK QUALITY SCORE REPORT")
    print("=" * 60)
    print(f"Total tasks: {len(scores)}")
    print(f"Average score: {avg_score:.1f} / 100")
    print()

    print("-" * 60)
    print("WEAKEST 10 TASKS")
    print("-" * 60)
    for s in scores[:10]:
        print(f"  [{s['score']}] {s['id']} ({s['status']}) - {s['title']}")
        for d in s["details"]:
            print(f"      - {d}")
    print()

    if duplicated:
        print("-" * 60)
        print("DUPLICATED TITLES")
        print("-" * 60)
        for t in duplicated:
            print(f"  - {t}")
        print()

    if missing_counts:
        print("-" * 60)
        print("TOP MISSING DETAILS ACROSS ALL TASKS")
        print("-" * 60)
        for item, count in missing_counts.most_common(5):
            print(f"  - {item}: {count} tasks")
        print()

    print("-" * 60)
    print("LIKELY OVERSIZED TASKS")
    print("-" * 60)
    oversized = [s for s in scores if "likely oversized" in s["details"]]
    for s in oversized[:10]:
        print(f"  - {s['id']}: {s['title']}")
    print()

    print("-" * 60)
    print("RECOMMENDED NEXT REFINEMENT BATCH (up to 10, non-done)")
    print("-" * 60)
    non_done = [s for s in scores if s["status"] != "done"][:10]
    batch_ids = [s["id"] for s in non_done]
    for s in non_done:
        print(f"  - {s['id']} (score {s['score']}) {s['title']}")
    print()
    print("To refine this batch, use the task_refiner prompt with these IDs:")
    print("  " + ", ".join(batch_ids))
    print("=" * 60)


if __name__ == "__main__":
    main()
