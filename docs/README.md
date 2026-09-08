# Documentation

This directory separates current project guidance from historical planning
records.

## Start Here

- [Getting Started](getting-started.md): repository orientation, tools, and
  the first build
- [Architecture Blueprint](architecture/blueprint.md): long-term direction,
  invariants, and phase roadmap
- [Development Log](development/devlog.md): completed checkpoints, test
  results, and debugging notes
- [Security Policy](../SECURITY.md): current trust model and missing defenses

## Design Records

- [Phase 4 runtime](design/2026-09-07-phase4-runtime.md): transactions, scheduler,
  replay, service replacement, and the driver boundary.

The files in [design/](design/) describe decisions for major subsystems. They
record the intended behavior and security boundaries, but the source and
tests remain authoritative when implementation details have changed.

Formal TLA+ specifications and model configurations live in [models/](models/).

## Archive

[archive/plans/](archive/plans/) contains completed implementation checklists.
They are retained for history and are not current instructions. Start new work
from the blueprint, current design records, source code, and tests.
