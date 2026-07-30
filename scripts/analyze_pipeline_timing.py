#!/usr/bin/env python3
"""Analyze pipeline latency CSVs dumped by PipelineTimingRecorder.

Usage:
    python3 scripts/analyze_pipeline_timing.py <dump_dir>

<dump_dir> is the directory containing pipeline_frames.csv,
pipeline_triggers.csv and pipeline_skips.csv (by default
<dataDir>/pipeline_timing, dumped on capture stop / shutdown when
MIB_PIPELINE_TIMING=1 — see docs/howto/pipeline-latency-diagnosis.md).

Prints per-stage latency percentiles (all in milliseconds):
    grab -> algo start     time a frame waited between acquisition and the
                           realtime algorithm picking it up (queueing delay)
    algo                   algorithm duration
    algo end -> dispatch   callback dispatch delay for trigger frames
    request -> fire        trigger-thread wake + output-line latency
    grab -> fire           END-TO-END acquisition-to-trigger-pulse latency
plus inter-frame gap statistics and the frame-accounting summary
(records + skips vs. what the capture pushed), which shows where frames
were dropped and why. Stdlib only; no third-party dependencies.

When the dump directory also contains pipeline_trend.csv (the 1 Hz time
series written by PipelineTrendSampler — enable with MIB_PIPELINE_TREND=1),
a trend section is printed as well: per-minute windows of each latency/depth
metric, the steady-state ratio (last window vs first window — the repo
convention is to judge growth on ratios, never absolute milliseconds), and a
decision-tree verdict mapping which parameter rose to the most likely cause
of latency growth over a long session.
"""

import csv
import sys
from pathlib import Path


def percentile(sorted_values, p):
    if not sorted_values:
        return 0.0
    idx = (p / 100.0) * (len(sorted_values) - 1)
    lo = int(idx)
    hi = min(lo + 1, len(sorted_values) - 1)
    frac = idx - lo
    return sorted_values[lo] * (1.0 - frac) + sorted_values[hi] * frac


def stats_line(name, values_us):
    """Format one metric row; values are microseconds, displayed as ms."""
    if not values_us:
        return f"  {name:<24} (no samples)"
    vals = sorted(v / 1000.0 for v in values_us)  # -> ms
    return (
        f"  {name:<24} n={len(vals):<7} "
        f"mean={sum(vals) / len(vals):8.3f}  p50={percentile(vals, 50):8.3f}  "
        f"p95={percentile(vals, 95):8.3f}  p99={percentile(vals, 99):8.3f}  "
        f"max={vals[-1]:8.3f}"
    )


def read_csv(path):
    if not path.exists():
        return []
    with path.open(newline="") as f:
        return [{k: int(v) for k, v in row.items()} for row in csv.DictReader(f)]


# --- long-session trend analysis (pipeline_trend.csv) -----------------------

# Growth is flagged on the steady-state ratio between the last and first
# per-minute windows, plus a noise guard: the last window must also exceed
# the first window's spread. Ratio-based per repo convention.
GROWTH_RATIO = 1.3


def read_trend_csv(path):
    if not path.exists():
        return []
    with path.open(newline="") as f:
        rows = []
        for row in csv.DictReader(f):
            parsed = {}
            for k, v in row.items():
                if k == "wall_clock":
                    parsed[k] = v
                else:
                    try:
                        parsed[k] = float(v)
                    except ValueError:
                        parsed[k] = 0.0
            rows.append(parsed)
        return rows


def window_series(rows, key, cumulative=False):
    """Per-minute windows of a trend column: list of (minute, values).

    For gauges, values are the positive samples in that minute (zero samples
    mean 'no data this tick' — e.g. algo stages in async-batch mode — and are
    excluded). For cumulative counters, values are per-minute increments.
    A column absent from the CSV (older recordings) yields no windows.
    """
    if rows and key not in rows[0]:
        return []
    windows = {}
    prev = None
    for row in rows:
        minute = int(row["t_s"] // 60)
        value = row[key]
        if cumulative:
            delta = value - prev if prev is not None else 0.0
            prev = value
            windows.setdefault(minute, []).append(max(0.0, delta))
        elif value > 0:
            windows.setdefault(minute, []).append(value)
    return sorted(windows.items())


def median(values):
    vals = sorted(values)
    return vals[len(vals) // 2] if vals else 0.0


def stddev(values):
    if len(values) < 2:
        return 0.0
    mean = sum(values) / len(values)
    return (sum((v - mean) ** 2 for v in values) / (len(values) - 1)) ** 0.5


class TrendMetric:
    """Steady-state growth judgement for one trend column."""

    def __init__(self, rows, key, cumulative=False):
        self.key = key
        windows = window_series(rows, key, cumulative)
        # Need two windows with data; ignore leading/trailing empty ones.
        self.ok = len(windows) >= 2
        if not self.ok:
            self.first = self.last = self.ratio = self.peak = 0.0
            self.grew = False
            return
        first_minute, first_vals = windows[0]
        last_minute, last_vals = windows[-1]
        self.first = median(first_vals)
        self.last = median(last_vals)
        self.peak = max(max(vals) for _, vals in windows)
        self.ratio = self.last / self.first if self.first > 0 else float("inf") if self.last > 0 else 1.0
        noise_floor = self.first + 2.0 * stddev(first_vals)
        self.grew = self.ratio > GROWTH_RATIO and self.last > noise_floor

    def describe(self):
        if not self.ok:
            return f"  {self.key:<28} (insufficient data)"
        ratio = f"{self.ratio:6.2f}x" if self.ratio != float("inf") else "  inf "
        flag = "  << GROWTH" if self.grew else ""
        return (
            f"  {self.key:<28} first={self.first:12.1f}  last={self.last:12.1f}  "
            f"ratio={ratio}  peak={self.peak:12.1f}{flag}"
        )


def analyze_trend(dump_dir):
    rows = read_trend_csv(dump_dir / "pipeline_trend.csv")
    if not rows:
        return
    duration = rows[-1]["t_s"] - rows[0]["t_s"]
    print(f"\n== Latency trend analysis ({len(rows)} samples, {duration:.0f}s) ==")
    if duration < 120:
        print("  (session under 2 minutes — trend windows need at least 2 full minutes)")
        return

    gauges = [
        "e2e_frame_p95_us", "e2e_target_p95_us", "frame_age_p95_us",
        "fetch_extract_p95_us", "algo_p95_us",
        "request_to_fire_p95_us", "backlog_frames", "batch_queue_depth",
        "host_grab_gap_mean_us", "device_tick_gap_mean", "objects_per_frame_mean",
        "empty_frame_avg_us", "overlay_avg_us",
        "cpu_realtime_pct", "cpu_capture_pct", "cpu_hdf_writer_pct",
        "hdf_write_avg_us", "heap_inuse_mb", "heap_free_mb", "mem_mb",
    ]
    counters = ["ring_behind", "dropped_to_latest", "batch_queue_rejected",
                "cs_nonvol_realtime", "cs_nonvol_trigger",
                "mat_allocs", "mat_alloc_mb", "io_write_mb"]
    m = {key: TrendMetric(rows, key) for key in gauges}
    for key in counters:
        m[key] = TrendMetric(rows, key, cumulative=True)

    print("\n-- Per-minute steady-state ratios (first window vs last window) --")
    for key in gauges:
        print(m[key].describe())
    print("-- Cumulative counters (per-minute increments) --")
    for key in counters:
        print(m[key].describe())

    # --- decision tree: which hypothesis does the measured growth confirm? ---
    batch_mode = rows[-1]["realtime_mode"] == 1
    drop_frames = rows[-1]["drop_frames"] == 1
    experiment = any(r["experiment_active"] == 1 for r in rows)
    verdicts = []
    e2e_grew = m["e2e_frame_p95_us"].grew or m["e2e_target_p95_us"].grew
    if e2e_grew:
        if batch_mode and m["batch_queue_depth"].grew:
            verdicts.append(
                "H1 batch-queue bufferbloat: async-batch queue depth is ramping "
                f"(peak {m['batch_queue_depth'].peak:.0f}); standing latency = depth / fps. "
                "Drop-newest overflow preserves the backlog, so depth (not drops) is the signal.")
        if not batch_mode and m["backlog_frames"].grew:
            saw = m["ring_behind"].last > 0
            verdicts.append(
                "H2 inline consumer backlog: latestAvailableIndex - rtLastProcessed is ramping "
                f"(peak {m['backlog_frames'].peak:.0f})"
                + (" with ring_behind evictions (sawtooth at the FrameStore window)" if saw else "")
                + f" — drop_frames={'on' if drop_frames else 'off'}, "
                f"experiment_active={'yes' if experiment else 'no'}; "
                "backlog can only accumulate when drops are off or an experiment is active.")
        if m["frame_age_p95_us"].ok and not m["frame_age_p95_us"].grew and m["algo_p95_us"].grew:
            if m["objects_per_frame_mean"].grew:
                verdicts.append(
                    "H6 contour growth: algorithm time ramps in proportion to detected objects "
                    f"per frame ({m['objects_per_frame_mean'].first:.1f} -> "
                    f"{m['objects_per_frame_mean'].last:.1f}) — scene/background degradation, "
                    "not a pipeline defect.")
            elif m["mem_mb"].grew:
                verdicts.append(
                    "H4 heap growth: algorithm time ramps with RSS "
                    f"({m['mem_mb'].first:.0f} -> {m['mem_mb'].last:.0f} MB) at flat object "
                    "counts — repeat the run at a lower fps (time- vs load-proportional) and "
                    "under -DMIB_SANITIZER=address (LSan) to confirm a leak.")
        if m["request_to_fire_p95_us"].grew:
            verdicts.append(
                "Trigger-thread degradation: request->fire p95 is ramping — scheduling/priority "
                "issue on the trigger thread, not the processing pipeline (new hypothesis; "
                "escalate).")
    if m["host_grab_gap_mean_us"].grew and m["device_tick_gap_mean"].ok \
            and not m["device_tick_gap_mean"].grew:
        verdicts.append(
            "H3 acquisition-side buffering: host grab gap grows while the device tick gap stays "
            "flat — frames queue inside the camera/SDK before grabFrame returns.")
    if m["mem_mb"].grew and not any(v.startswith("H4") for v in verdicts):
        verdicts.append(
            f"RSS ramp ({m['mem_mb'].first:.0f} -> {m['mem_mb'].last:.0f} MB) without a matching "
            "latency signal — possible slow leak (H4); confirm with an LSan run before acting.")

    # --- profiling-layer rules (CPU saturation, fragmentation, churn) ---
    if m["cpu_realtime_pct"].ok and m["cpu_realtime_pct"].last > 90:
        verdicts.append(
            f"Realtime thread near saturation ({m['cpu_realtime_pct'].last:.0f}% CPU in the last "
            "window) — no headroom; any extra load (denser scene, larger ROI) tips it into "
            "backlog. This is the leading indicator of H2 even before backlog appears.")
    if m["mem_mb"].grew and m["heap_inuse_mb"].ok and not m["heap_inuse_mb"].grew:
        verdicts.append(
            "Heap fragmentation signature: RSS ramps while allocator in-use bytes stay flat "
            f"(rss {m['mem_mb'].first:.0f} -> {m['mem_mb'].last:.0f} MB, in-use "
            f"{m['heap_inuse_mb'].last:.0f} MB) — arenas grow but the app isn't holding more "
            "memory. Points at allocation churn patterns, not a leak.")
    if m["mat_allocs"].grew:
        verdicts.append(
            f"cv::Mat allocation rate is growing ({m['mat_allocs'].first:.0f} -> "
            f"{m['mat_allocs'].last:.0f} allocs/s) at steady load — per-frame churn is "
            "increasing over time; correlate with algo duration and heap columns.")
    if m["request_to_fire_p95_us"].grew and m["cs_nonvol_trigger"].grew:
        verdicts.append(
            "Trigger latency growth co-occurs with rising nonvoluntary context switches on the "
            "trigger thread — OS scheduling pressure (check RT priority / CPU load), not "
            "pipeline structure.")

    # --- live-view impact A/B (H5), measured, not inferred ---
    # overlay_count is cumulative: a positive per-tick delta marks a second in
    # which the GUI overlay kernel actually ran. Compare pipeline latency
    # between overlay-active and overlay-idle seconds of the SAME session (the
    # site protocol's show/hide schedule produces both).
    if rows and "overlay_count" in rows[0]:
        on_vals, off_vals = [], []
        prev = None
        for row in rows:
            cnt = row["overlay_count"]
            active = prev is not None and cnt > prev
            prev = cnt
            v = row["e2e_frame_p95_us"]
            if v > 0:
                (on_vals if active else off_vals).append(v)
        if len(on_vals) >= 60 and len(off_vals) >= 60:
            on_med, off_med = median(on_vals), median(off_vals)
            ratio = on_med / off_med if off_med > 0 else float("inf")
            if ratio > GROWTH_RATIO:
                verdicts.append(
                    f"H5 live-view impact: e2e_frame p95 is {ratio:.2f}x higher in seconds where "
                    f"the overlay kernel ran ({on_med:.0f} vs {off_med:.0f} us) — the GUI overlay "
                    "pass measurably slows the pipeline (kernel/context contention).")
            else:
                print(f"\n  live-view A/B: e2e p95 with overlay {on_med:.0f} us vs without "
                      f"{off_med:.0f} us ({ratio:.2f}x) — no measurable live-view impact.")

    print("\n-- Verdict --")
    if verdicts:
        for v in verdicts:
            print(f"  * {v}")
    elif e2e_grew:
        print("  * End-to-end latency grew but no instrumented stage explains it — inspect the "
              "per-stage columns manually and consider the GUI-contention hypothesis (H5): "
              "re-run with the PlaybackPanel hidden vs visible (A/B).")
    else:
        print("  * No latency growth in the instrumented path over this session.")
        print("    If the symptom reproduces only in the full app, suspect GUI-side causes "
              "(H5: 60 Hz overlay kernel contention) or acquisition-side buffering (H3) — "
              "both need an app run with MIB_PIPELINE_TREND=1 to discriminate.")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    dump_dir = Path(sys.argv[1])
    frames = read_csv(dump_dir / "pipeline_frames.csv")
    triggers = read_csv(dump_dir / "pipeline_triggers.csv")
    skips_path = dump_dir / "pipeline_skips.csv"

    print(f"== Pipeline timing analysis: {dump_dir} ==")
    print(f"frame records:   {len(frames)}")
    print(f"trigger records: {len(triggers)}")

    # --- per-stage frame latencies (all values are host-monotonic us) ---
    grab_to_algo = [
        f["algo_start_us"] - f["grab_us"]
        for f in frames
        if f["grab_us"] and f["algo_start_us"]
    ]
    # fetch_start_us is absent in dumps from before the fetch/extract stamp.
    fetch_extract = [
        f["algo_start_us"] - f["fetch_start_us"]
        for f in frames
        if f.get("fetch_start_us") and f["algo_start_us"]
    ]
    algo = [
        f["algo_end_us"] - f["algo_start_us"]
        for f in frames
        if f["algo_start_us"] and f["algo_end_us"]
    ]
    dispatch = [
        f["trigger_dispatch_us"] - f["algo_end_us"]
        for f in frames
        if f["trigger_dispatch_us"] and f["algo_end_us"]
    ]
    print("\n-- Frame stage latencies (ms) --")
    print(stats_line("grab -> algo start", grab_to_algo))
    print(stats_line("fetch+extract", fetch_extract))
    print(stats_line("algo duration", algo))
    print(stats_line("algo end -> tg dispatch", dispatch))

    # --- inter-frame gaps: host clock vs device ticks ---
    # A growing host gap with a steady device gap means frames are queueing in
    # the grabber/SDK before the capture thread sees them.
    host_gaps = []
    device_gaps = []
    for prev, cur in zip(frames, frames[1:]):
        if cur["frame_index"] == prev["frame_index"] + 1:  # adjacent frames only
            if prev["grab_us"] and cur["grab_us"]:
                host_gaps.append(cur["grab_us"] - prev["grab_us"])
            if prev["device_timestamp"] and cur["device_timestamp"]:
                device_gaps.append(cur["device_timestamp"] - prev["device_timestamp"])
    print("\n-- Adjacent-frame gaps --")
    print(stats_line("host grab gap (ms)", host_gaps))
    if device_gaps:
        vals = sorted(device_gaps)
        print(
            f"  {'device tick gap (raw)':<24} n={len(vals):<7} "
            f"mean={sum(vals) / len(vals):8.1f}  p50={percentile(vals, 50):8.1f}  "
            f"max={vals[-1]:8.1f}   (device units, not host us)"
        )

    # --- trigger latencies ---
    req_to_wake = [t["wake_us"] - t["request_us"] for t in triggers if t["request_us"]]
    wake_to_fire = [t["fire_us"] - t["wake_us"] for t in triggers if t["wake_us"]]
    req_to_fire = [t["fire_us"] - t["request_us"] for t in triggers if t["request_us"]]
    grab_to_fire = [
        t["fire_us"] - t["grab_us"] for t in triggers if t["grab_us"] and t["fire_us"]
    ]
    coalesced = sum(t["coalesced"] for t in triggers)
    print("\n-- Trigger latencies (ms) --")
    print(stats_line("request -> wake", req_to_wake))
    print(stats_line("wake -> fire", wake_to_fire))
    print(stats_line("request -> fire", req_to_fire))
    print(stats_line("grab -> fire (END2END)", grab_to_fire))
    if coalesced:
        print(f"  NOTE: {coalesced} trigger request(s) coalesced into earlier pulses")

    # --- frame accounting ---
    print("\n-- Frame accounting --")
    accounted = len(frames)
    if skips_path.exists():
        with skips_path.open(newline="") as f:
            for row in csv.DictReader(f):
                print(f"  {row['reason']:<24} {row['count']}")
                if row["reason"] not in ("frame_records", "trigger_records"):
                    accounted += int(row["count"])
        print(f"  accounted total (records in ring + skips): {accounted}")
        print("  Compare against capture framesProcessed for the same session; a")
        print("  mismatch beyond the ring capacity means unexplained frame loss.")
    else:
        print(f"  (missing {skips_path})")

    # --- identification funnel + loss ---
    # Quantifies "loss of target identification": how the objects the detector
    # produced narrowed down to sort pulses, and where identified frames were
    # lost before reaching the algorithm at all (the skip reasons).
    valid_objs = sum(f["valid_count"] for f in frames)
    invalid_objs = sum(f["invalid_count"] for f in frames)
    tg_frames = [f for f in frames if f["is_target_group"]]
    total_objs = valid_objs + invalid_objs
    print("\n-- Identification funnel --")
    print(f"  objects detected:       {total_objs}")
    if total_objs:
        print(f"  valid (passed gates):   {valid_objs} ({100.0 * valid_objs / total_objs:.1f}%)")
        print(f"  invalid (gated out):    {invalid_objs} "
              f"({100.0 * invalid_objs / total_objs:.1f}%)")
    print(f"  frames requesting sort: {len(tg_frames)} / {len(frames)}")

    # Frames lost before the algorithm (from the skip file) are identification
    # opportunities that never happened. ring_behind / dropped_to_latest are the
    # ones that indicate the realtime consumer could not keep up.
    if skips_path.exists():
        loss = {}
        with skips_path.open(newline="") as f:
            for row in csv.DictReader(f):
                if row["reason"] in ("ring_behind", "dropped_to_latest",
                                     "kernel_error", "batch_queue_rejected"):
                    loss[row["reason"]] = int(row["count"])
        lost = sum(loss.values())
        if lost:
            detail = ", ".join(f"{k}={v}" for k, v in loss.items() if v)
            print(f"  frames lost pre-algo:   {lost}  ({detail})")

    analyze_trend(dump_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
