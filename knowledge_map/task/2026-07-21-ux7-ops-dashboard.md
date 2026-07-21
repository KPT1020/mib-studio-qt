Title: Integrated Experiment operations dashboard (UX-7 slice, issue #311 / epic #304)

Context:
- Operators value the live image but operate from the numbers. The parity shell
  split live Preview and Monitoring into sub-tabs, so the key metrics/alerts
  were a tab away. This brings the key metrics, quality gates, and alerts next
  to the live image in the Experiment/Preview view — no sub-tab switching for
  routine operation. Based on dev/react-tauri (UX-1/3/4/8/9 merged).
- This is the core slice of UX-7 (metrics + alerts + quality beside the image +
  always-on collection). The full layout spec (dedicated run header, quick-adjust
  panel, expandable charts/tables/drawers, keyboard focus order) is a follow-up.

Implementation Notes:
- `desktop/src/operations.ts` — new pure module.
  - `deriveKeyMetrics(input)` → total/valid/invalid events, validity rate,
    throughput, and evicted-rows (bounded-buffer `appended - held`), with no
    divide-by-zero.
  - `deriveAlerts(input)` → an alert list, **critical first**: experiment
    failed, unresolved bridge error, and dropped events are critical; stale
    metrics, low validity (only while active + over-threshold), failing quality
    gates, evictions, and flushing are warnings.
  - No React/Tauri imports (unit-testable).
- `desktop/src/App.tsx`:
  - **Metric collection now runs throughout the Experiment stage and any active
    run** (`monitoringActive = on Experiment tab || run active`), while snapshot
    *polling* runs while the tab is shown — so core metrics keep accumulating
    even when the detailed Monitoring charts aren't rendered (UX-7 data
    behavior). Freshness is tracked via `metricsRef` (last total + change time);
    metrics show Live / Stale / Idle.
  - An operations panel under the live preview shows the five key metric cards,
    the quality-gate strip (reusing UX-4 `quality.ts`), and the alert list.
- `desktop/src/App.css` — `.ops-panel` / `.ops-metric` / `.ops-quality` /
  `.ops-alert`.

Verification:
- `desktop/src/operations.test.ts` — 8 vitest cases: metric math + no
  divide-by-zero; clean run has no alerts; critical-before-warning ordering;
  dropped-events critical; low-validity gated on active + events + threshold;
  stale gated on active; quality-fail + flushing surfaced.
- `npm run build` (tsc strict + vite) green; `npm test` green (72 total).
  Headless Chromium render shows the metrics + quality + alerts panel in the
  Experiment/Preview view with no page errors.
- `python3 scripts/check_docs.py` green.

Follow-ups (remainder of UX-7):
- Dedicated run header (elapsed/estimate, pause), quick-adjust panel with
  effective-value/source/undo + save-to-profile (UX-2 #306), metric
  sparklines/trends, expandable detail drawers, alert acknowledgement, and
  keyboard focus order. Readiness-gated Start ties to UX-6 (#310).
