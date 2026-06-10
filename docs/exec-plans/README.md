# Execution Plans

Plans are first-class, versioned artifacts. Agents and humans operate from
plans checked into this directory, not from external context.

## Layout

| Path | Contents |
|------|----------|
| `active/` | In-flight plans. One file per plan. |
| `completed/` | Finished plans, moved here verbatim when done. |
| [`tech-debt-tracker.md`](tech-debt-tracker.md) | Known debt with exit criteria. |

Legacy plans and paired design specs live in
[`../superpowers/plans/`](../superpowers/plans/) and
[`../superpowers/specs/`](../superpowers/specs/); new plans go here.

## When to write a plan

- The change spans backend and frontend, or
- the change needs more than one PR, or
- the work has decisions worth recording (then also consider an ADR in
  [`../decisions/`](../decisions/README.md)).

Small single-PR changes do not need a plan; the PR description plus a dated
task note in `knowledge_map/task/` is enough.

## Plan format

Name files `YYYY-MM-DD-<topic>.md`. Every active plan must contain a
`Status:` line. Recommended skeleton:

```markdown
# <Title>

Status: active | blocked | completed

## Goal
One paragraph. What is true when this is done.

## Acceptance criteria
- [ ] ...

## Decision log
- YYYY-MM-DD: <decision and why>

## Progress
- [ ] step
```

## Lifecycle

1. Create the plan in `active/` in the first PR of the work.
2. Update the decision log and progress as PRs land — the plan is the memory
   between agent runs.
3. When done, set `Status: completed` and `git mv` the file to `completed/`.
4. Anything intentionally left unfinished goes to the tech debt tracker.

`scripts/check_docs.py` enforces that active plans carry a `Status:` line.
