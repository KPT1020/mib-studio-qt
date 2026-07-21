import { describe, expect, it } from "vitest";
import {
  deriveAlerts,
  deriveKeyMetrics,
  type AlertsInput,
  type KeyMetricsInput,
} from "./operations";

const METRICS: KeyMetricsInput = {
  validAppended: 90,
  invalidAppended: 10,
  validHeld: 80,
  invalidHeld: 10,
  algoFps: 6210,
};

describe("deriveKeyMetrics", () => {
  it("computes totals, validity rate and eviction", () => {
    const m = deriveKeyMetrics(METRICS);
    expect(m.totalEvents).toBe(100);
    expect(m.validEvents).toBe(90);
    expect(m.validityRatePct).toBeCloseTo(90);
    expect(m.throughputFps).toBe(6210);
    expect(m.evicted).toBe(10); // 90 appended - 80 held
  });

  it("reports 0% validity with no events (no divide-by-zero)", () => {
    const m = deriveKeyMetrics({ ...METRICS, validAppended: 0, invalidAppended: 0, validHeld: 0, invalidHeld: 0 });
    expect(m.totalEvents).toBe(0);
    expect(m.validityRatePct).toBe(0);
    expect(m.evicted).toBe(0);
  });
});

const CLEAN: AlertsInput = {
  experimentActive: true,
  experimentFailed: false,
  experimentFlushing: false,
  bridgeError: false,
  metricsStale: false,
  totalEvents: 100,
  validityRatePct: 90,
  lowValidityThresholdPct: 50,
  droppedValid: 0,
  droppedInvalid: 0,
  evicted: 0,
  qualityFails: 0,
};

describe("deriveAlerts — clean run", () => {
  it("raises no alerts when everything is healthy", () => {
    expect(deriveAlerts(CLEAN)).toEqual([]);
  });
});

describe("deriveAlerts — severity & ordering", () => {
  it("puts critical alerts before warnings", () => {
    const alerts = deriveAlerts({
      ...CLEAN,
      experimentFailed: true,
      evicted: 5,
      metricsStale: true,
    });
    expect(alerts[0].severity).toBe("critical");
    expect(alerts.some((a) => a.severity === "warning")).toBe(true);
    // criticals must all precede warnings
    const firstWarning = alerts.findIndex((a) => a.severity === "warning");
    const lastCritical = alerts.map((a) => a.severity).lastIndexOf("critical");
    expect(lastCritical).toBeLessThan(firstWarning);
  });

  it("dropped events are critical (real data loss)", () => {
    const a = deriveAlerts({ ...CLEAN, droppedValid: 3 });
    expect(a.find((x) => x.id === "dropped-events")?.severity).toBe("critical");
  });

  it("low validity only warns while active and with events over threshold", () => {
    expect(deriveAlerts({ ...CLEAN, validityRatePct: 20 }).some((a) => a.id === "low-validity")).toBe(true);
    // no events → no low-validity alert
    expect(
      deriveAlerts({ ...CLEAN, validityRatePct: 20, totalEvents: 0 }).some((a) => a.id === "low-validity"),
    ).toBe(false);
    // idle → no low-validity alert
    expect(
      deriveAlerts({ ...CLEAN, validityRatePct: 20, experimentActive: false }).some((a) => a.id === "low-validity"),
    ).toBe(false);
  });

  it("stale metrics warn only while the experiment is active", () => {
    expect(deriveAlerts({ ...CLEAN, metricsStale: true }).some((a) => a.id === "metrics-stale")).toBe(true);
    expect(
      deriveAlerts({ ...CLEAN, metricsStale: true, experimentActive: false }).some((a) => a.id === "metrics-stale"),
    ).toBe(false);
  });

  it("surfaces quality-gate failures and flushing", () => {
    const a = deriveAlerts({ ...CLEAN, qualityFails: 2, experimentFlushing: true });
    expect(a.some((x) => x.id === "quality-fails")).toBe(true);
    expect(a.some((x) => x.id === "flushing")).toBe(true);
  });
});
