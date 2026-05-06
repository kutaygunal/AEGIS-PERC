# Project Memory

## Project
AEGIS-PERC is a Qt6/C++20 commercial-grade AI-assisted semiconductor verification platform.

## Architecture Rules
- UI observes state; core owns state.
- Parser, graph, rule engine, ML, reporting, storage, and scripting must stay decoupled.
- Core modules must be testable without launching the UI.
- ML inference is optional and replaceable.
- Visualization must not own business logic.
- Python scripting must call core services, not UI classes.

## Current Architecture
- CMake-based modular structure.
- Qt6 desktop application.
- Core engine intended to be independent from widgets.
- Future modules: parser, graph, rule engine, ML, reporting, storage, scripting, plugins.

## Important Constraints
- Keep every change PR-sized.
- Do not implement future sprint features early.
- Mark successful autonomous work as `review`, not `done`.
- Only the user can mark tasks `done`.

## Known Risks
- Context-window loss can cause duplicated work.
- Later tasks in tasks.yaml may need refinement.
- Architecture drift must be prevented by reviewer/architect prompts.
