// Integrated Experiment operations: key metrics + alerts (UX-7, issue #311 /
// epic #304).
//
// Operators value the live image but operate the instrument from the numbers.
// The Qt shell (and the parity port) split live Preview and Monitoring into
// sub-tabs, so those numbers were a tab away. This derives the key metrics and
// the alert set from already-bridged monitoring/experiment signals so they can
// sit next to the live image — no sub-tab switching for routine operation.
//
// Pure module: no React/Tauri imports so it is unit-testable in plain Node.

export interface KeyMetricsInput {
  /** Total valid/invalid events appended to the bounded monitor this run. */
  validAppended: number;
  invalidAppended: number;
  /** Rows currently held (appended - held = evicted by the bounded buffer). */
  validHeld: number;
  invalidHeld: number;
  /** Processing throughput (algorithm frames/s). */
  algoFps: number;
}

export interface KeyMetrics {
  totalEvents: number;
  validEvents: number;
  invalidEvents: number;
  /** Valid / total, as a percentage; 0 when no events yet. */
  validityRatePct: number;
  throughputFps: number;
  /** Rows dropped by the bounded monitoring buffer (not lost science data). */
  evicted: number;
}

export function deriveKeyMetrics(i: KeyMetricsInput): KeyMetrics {
  const totalEvents = i.validAppended + i.invalidAppended;
  const validityRatePct = totalEvents > 0 ? (i.validAppended / totalEvents) * 100 : 0;
  const evicted = Math.max(0, i.validAppended - i.validHeld) + Math.max(0, i.invalidAppended - i.invalidHeld);
  return {
    totalEvents,
    validEvents: i.validAppended,
    invalidEvents: i.invalidAppended,
    validityRatePct,
    throughputFps: i.algoFps,
    evicted,
  };
}

export type AlertSeverity = "critical" | "warning";

export interface Alert {
  id: string;
  severity: AlertSeverity;
  message: string;
}

export interface AlertsInput {
  experimentActive: boolean;
  experimentFailed: boolean;
  experimentFlushing: boolean;
  /** A critical save/bridge/device error is unresolved. */
  bridgeError: boolean;
  /** Metrics are not advancing while the run is active. */
  metricsStale: boolean;
  totalEvents: number;
  validityRatePct: number;
  lowValidityThresholdPct: number;
  droppedValid: number;
  droppedInvalid: number;
  evicted: number;
  qualityFails: number;
}

/** Derive the current alert set, most-severe first. */
export function deriveAlerts(i: AlertsInput): Alert[] {
  const critical: Alert[] = [];
  const warning: Alert[] = [];

  if (i.experimentFailed) {
    critical.push({ id: "experiment-failed", severity: "critical", message: "Experiment failed." });
  }
  if (i.bridgeError) {
    critical.push({
      id: "bridge-error",
      severity: "critical",
      message: "Unresolved backend/bridge error — data may be at risk.",
    });
  }
  const droppedTotal = i.droppedValid + i.droppedInvalid;
  if (droppedTotal > 0) {
    critical.push({
      id: "dropped-events",
      severity: "critical",
      message: `${droppedTotal} event(s) dropped under load.`,
    });
  }

  if (i.experimentActive && i.metricsStale) {
    warning.push({ id: "metrics-stale", severity: "warning", message: "Live metrics are stale." });
  }
  if (i.experimentActive && i.totalEvents > 0 && i.validityRatePct < i.lowValidityThresholdPct) {
    warning.push({
      id: "low-validity",
      severity: "warning",
      message: `Low validity rate (${i.validityRatePct.toFixed(0)}%).`,
    });
  }
  if (i.qualityFails > 0) {
    warning.push({
      id: "quality-fails",
      severity: "warning",
      message: `${i.qualityFails} quality gate(s) failing.`,
    });
  }
  if (i.evicted > 0) {
    warning.push({
      id: "evicted",
      severity: "warning",
      message: `${i.evicted} monitoring row(s) evicted (bounded buffer).`,
    });
  }
  if (i.experimentFlushing) {
    warning.push({ id: "flushing", severity: "warning", message: "Flushing buffered data…" });
  }

  return [...critical, ...warning];
}
