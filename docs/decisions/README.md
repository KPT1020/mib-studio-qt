# Architecture Decision Records

Short, dated records of decisions that shape the codebase. Write one whenever
a choice would otherwise need re-explaining in review: a new threading
pattern, a dependency, a storage schema change, a boundary exception.

## Format

Name files `NNNN-<slug>.md` (e.g. `0001-backend-frontend-bridge.md`):

```markdown
# NNNN. <Title>

Date: YYYY-MM-DD
Status: accepted | superseded by NNNN

## Context
What forced the decision.

## Decision
What we chose.

## Consequences
What becomes easier/harder; what future agents must respect.
```

## Index

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-react-tauri-migration.md) | Migrate the desktop application from Qt to React + Tauri | accepted |

Decisions made before this index existed live implicitly in
[`../architecture/`](../architecture/) and the vault
(`knowledge_map/architecture/`). When you rediscover one, backfill it here.
