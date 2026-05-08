# AEGIS-PERC Orchestration System

This directory contains the AI-assisted development workflow for AEGIS-PERC. It treats engineering tasks as packets with dependencies, acceptance criteria, build/test gates, and review prompts.

## Directory Layout

```
orchestration/
  roadmap.yaml           — High-level phases and exit criteria
  tasks.yaml             — Executable task registry with dependencies and status
  PROJECT_MEMORY.md      — Long-term stable project facts and architecture rules
  DECISIONS.md           — Immutable architecture decisions and reasoning
  HANDOFF.md             — Latest session state and next recommended action
  session_bootstrap.md   — Prompt every new session must run first
  prompts/
    planner.md           — Prompt template for implementation planning
    worker.md            — Prompt template for task implementation
    reviewer.md          — Prompt template for code review
    architect.md         — Prompt template for architecture review
    overnight_executor.md— Prompt template for autonomous overnight mode
    task_refiner.md      — Prompt template for task refinement
  runs/
    .gitkeep             — Run logs and artifacts go here (kept in git)
  tools/
    project_status.py    — Quick CLI summary of current task state
    validate_tasks.py    — Task schema validator
    score_tasks.py       — Task scoring helper
  README.md              — This file
```

## Task Lifecycle

```
 todo → ready → in_progress → review → done
  ↑          ↓       ↓           ↓
blocked ←─── (dependencies not done)
```

- **todo** — Backlog; not yet actionable.
- **ready** — All dependencies are `done`; may be started.
- **in_progress** — Active work is happening.
- **review** — Implementation complete; under code/architecture review.
- **done** — Accepted; merged; dependencies may unblock.
- **blocked** — External or dependency blocker.

### Dependency Rule
A task can be marked `ready` **only if all tasks in `depends_on` are `done`**.

## Workflow

### 1. Select the next ready task
Identify the highest-priority task with status `ready`. If none are ready, work on unblocking dependencies.

### 2. Plan
Use the **Planner** prompt to produce an implementation plan:
- Identify files, modules, interfaces, data flow.
- Identify risks, tests, and estimates.
- Do not write code during planning.

### 3. Implement
Use the **Worker** prompt to implement exactly one task:
- Modify only necessary files.
- Follow AGENTS.md rules.
- Add or update tests.
- Run the task's `test_commands` and report results.
- Stop after the task; do not proceed to the next task.

### 4. Review
Use the **Reviewer** prompt to evaluate the diff:
- Check acceptance criteria coverage.
- Check architecture violations (UI/core, parser/rule, rule/ML).
- Check tests, safety, performance, and maintainability.
- Return `APPROVE` or `REQUEST_CHANGES`.

### 5. Architecture Review (for foundational or high-risk tasks)
Use the **Architect** prompt to validate long-term compatibility:
- Plugin support, graph analysis, ML inference, Python scripting, distributed execution.
- Identify overengineering or underengineering.
- Do not write code unless explicitly requested.

### 6. Update status
After approval, update the task status in `tasks.yaml` to `done`. Unblock downstream tasks by marking them `ready` if all their dependencies are now `done`.

## How to Add a New Task

1. Assign an id with phase prefix (`P1-011`, `P2-012`, etc.).
2. Fill all fields: `id`, `phase`, `title`, `priority`, `area`, `risk`, `depends_on`, `acceptance`, `implementation_rules`, `test_commands`.
3. Set status to `todo` unless all dependencies are already `done`.
4. Insert the task in dependency order.

## Current Execution Lane

The repository has advanced well beyond the original Phase 1 bootstrap.

- **Completed in codebase:** P1, P2, P3, P6, and P7 deliverables reflected in `README.md`, `src/`, `docs/`, and `tests/`
- **Active task registry contents:** forward-looking backlog items for P4 and P5
- **Current tasks.yaml snapshot:** `P4-001` and `P5-001` remain `todo`

If additional historical task tracking is needed, recreate archived task entries rather than assuming the current `tasks.yaml` is a full project history.

## Risk and Priority Legend

| Priority | Meaning |
|----------|---------|
| critical | Blocks significant downstream work; do first. |
| high | Important for current sprint success. |
| medium | Valuable but not blocking. |
| low | Nice to have; defer if time-constrained. |

| Risk | Meaning |
|------|---------|
| high | Foundational decision; mistake is expensive to undo. |
| medium | Moderate uncertainty or integration surface. |
| low | Well-understood or isolated change. |

## Notes

- Do not mark tasks `done` until acceptance criteria are verified and tests pass.
- Do not skip review for high-risk tasks.
- If a task needs to be split mid-flight, create new child tasks, update dependencies, and mark the original as blocked or superseded.
