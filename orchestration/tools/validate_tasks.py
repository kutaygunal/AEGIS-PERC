#!/usr/bin/env python3
"""
validate_tasks.py

Validates orchestration/tasks.yaml structure and semantics.
Requires: PyYAML (pip install pyyaml)

Usage from repo root:
    python orchestration/tools/validate_tasks.py
"""
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("Error: PyYAML is required. Install with: pip install pyyaml")
    sys.exit(1)

TASKS_PATH = Path("orchestration/tasks.yaml")

REQUIRED_FIELDS = {
    "id",
    "phase",
    "title",
    "priority",
    "area",
    "risk",
    "depends_on",
    "acceptance",
    "implementation_rules",
    "test_commands",
    "status",
}

VALID_STATUS = {"todo", "ready", "in_progress", "blocked", "review", "done"}
VALID_PRIORITY = {"critical", "high", "medium", "low"}
VALID_RISK = {"high", "medium", "low"}


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

    if not isinstance(data, dict):
        print("Error: YAML root must be a mapping.")
        sys.exit(1)

    tasks = data.get("tasks")
    if not isinstance(tasks, list):
        print("Error: 'tasks' key must contain a list.")
        sys.exit(1)

    return tasks


def validate():
    tasks = load_tasks()
    errors = []

    ids = set()
    id_to_task = {}

    for idx, task in enumerate(tasks):
        prefix = f"Task #{idx + 1}"
        if not isinstance(task, dict):
            errors.append(f"{prefix} is not a mapping.")
            continue

        missing = REQUIRED_FIELDS - set(task.keys())
        if missing:
            errors.append(f"{prefix} missing fields: {sorted(missing)}")

        tid = task.get("id")
        if not tid:
            errors.append(f"{prefix} missing 'id'.")
            continue
        if tid in ids:
            errors.append(f"Duplicate id: {tid}")
        else:
            ids.add(tid)
        id_to_task[tid] = task

        status = task.get("status")
        if status not in VALID_STATUS:
            errors.append(f"{tid}: invalid status '{status}'. Valid: {sorted(VALID_STATUS)}")

        priority = task.get("priority")
        if priority not in VALID_PRIORITY:
            errors.append(f"{tid}: invalid priority '{priority}'. Valid: {sorted(VALID_PRIORITY)}")

        risk = task.get("risk")
        if risk not in VALID_RISK:
            errors.append(f"{tid}: invalid risk '{risk}'. Valid: {sorted(VALID_RISK)}")

        depends_on = task.get("depends_on", [])
        if not isinstance(depends_on, list):
            errors.append(f"{tid}: 'depends_on' must be a list.")
            depends_on = []

        for dep in depends_on:
            if dep == tid:
                errors.append(f"{tid}: self-dependency.")
            elif dep not in ids:
                # Will be re-checked after the first pass, but we note missing IDs here.
                pass

        # Non-empty list-of-strings checks
        for field in ("acceptance", "implementation_rules", "test_commands"):
            val = task.get(field, [])
            if not isinstance(val, list) or not val:
                errors.append(f"{tid}: '{field}' is empty or missing.")
            elif not all(isinstance(v, str) and v.strip() for v in val):
                errors.append(f"{tid}: '{field}' must be a list of non-empty strings.")

    # Dependency existence
    for tid, task in id_to_task.items():
        depends_on = task.get("depends_on", [])
        if not isinstance(depends_on, list):
            continue
        for dep in depends_on:
            if dep not in id_to_task:
                errors.append(f"{tid}: dependency '{dep}' does not exist.")

    # Circular dependency detection via DFS
    WHITE, GRAY, BLACK = 0, 1, 2
    color = {tid: WHITE for tid in ids}
    reported_cycles = set()

    def dfs(node, stack):
        color[node] = GRAY
        for dep in id_to_task[node].get("depends_on", []):
            if dep not in id_to_task:
                continue
            if color[dep] == GRAY:
                cycle_nodes = stack[stack.index(dep):] + [dep]
                cycle_key = tuple(sorted(cycle_nodes))
                if cycle_key not in reported_cycles:
                    reported_cycles.add(cycle_key)
                    cycle_str = " -> ".join(cycle_nodes)
                    errors.append(f"Circular dependency detected: {cycle_str}")
            elif color[dep] == WHITE:
                dfs(dep, stack + [dep])
        color[node] = BLACK

    for tid in ids:
        if color[tid] == WHITE:
            dfs(tid, [tid])

    if errors:
        print(f"Validation FAILED: {len(errors)} error(s)")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)
    else:
        print(f"Validation PASSED for {len(tasks)} task(s).")
        sys.exit(0)


if __name__ == "__main__":
    validate()
