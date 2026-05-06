# Architecture Decisions

## DEC-001: Core modules must not depend on Qt widgets
Status: accepted
Reason:
Core logic must be unit-testable and usable from CLI, scripting, and future worker processes.

## DEC-002: ML inference must be optional
Status: accepted
Reason:
The product should support deterministic rule checking without requiring AI/ML runtime.

## DEC-003: Successful autonomous tasks go to review, not done
Status: accepted
Reason:
Human review is required before a task is considered complete.

## DEC-004: Parser and rule engine are separate modules
Status: accepted
Reason:
Parsing converts external data into internal representation. Rule engine analyzes that representation.

## DEC-005: Service registry lifetime must be explicit
Status: accepted
Reason:
Hidden global singletons make testing and distributed execution difficult. Prefer dependency injection and explicit service registries.

## DEC-006: Project memory lives in the repository
Status: accepted
Reason:
Coding-agent sessions are forgetful. The repository is the single source of truth for architecture, decisions, task state, and run history.
