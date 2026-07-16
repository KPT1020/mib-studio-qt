//! Headless contract test for the Rust <-> C++ bridge (epic #246, ADR 0003).
//!
//! Drives a real lifecycle against the linked Qt-free backend:
//!   init -> configure mock camera -> start -> observe CameraStatus + FrameReady
//!   events -> fetch_latest_frame -> stop -> shutdown.
//! Asserts the event contract and frame metadata. Runs with no Qt, no webkit,
//! no display — this is the Phase 2 gate and the boundary regression guard.

use std::path::PathBuf;
use std::time::{Duration, Instant};

use mib_bridge::ffi::{self, BridgeEventKind};
use serial_test::serial;

/// Copy the committed sample frame into a fresh temp dir under OUT-of-tree
/// scratch, enough times for the mock camera (looping) to produce a stream.
fn make_frame_dir() -> PathBuf {
    // A real 512x96 microscopy frame (the .png in the same dir is a 1x1
    // placeholder unsuitable for the pipeline).
    let sample = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../data/mock_frames/frame_00000.tiff");
    assert!(sample.exists(), "sample frame missing: {}", sample.display());

    let dir = std::env::temp_dir().join(format!("mib_bridge_contract_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    for i in 0..4 {
        let dst = dir.join(format!("frame_{i:04}.tiff"));
        std::fs::copy(&sample, &dst).unwrap();
    }
    dir
}

fn drain_until<F: Fn(&ffi::BridgeEvent) -> bool>(
    bridge: &mut cxx::UniquePtr<ffi::BackendBridge>,
    pred: F,
    timeout: Duration,
) -> Option<ffi::BridgeEvent> {
    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        for ev in bridge.pin_mut().poll_events() {
            if pred(&ev) {
                return Some(ev);
            }
        }
        std::thread::sleep(Duration::from_millis(5));
    }
    None
}

#[test]
fn abi_version_is_stable() {
    // The command/event schema is versioned (ADR 0003). v2 added the review
    // commands; v3 the processing commands; v4 operation state, the bounded
    // event queue, and the extended error sources (ADR 0004); v5 the
    // experiment lifecycle (BE-4); v6 monitoring snapshots + trigger (BE-5);
    // v7 camera discovery/selection (BE-2).
    assert_eq!(ffi::bridge_abi_version(), 7);
}

// BE-2: camera discovery/selection contract — mock enumeration/selection is
// headless-testable, invalid indices/paths return structured errors, and the
// selected-device snapshot is authoritative and survives capture start/stop.
#[test]
#[serial]
fn camera_discovery_and_selection_contract() {
    let frame_dir = make_frame_dir();
    let data_dir = std::env::temp_dir().join(format!("mib_bridge_cam_data_{}", std::process::id()));

    let mut bridge = ffi::new_backend_bridge();
    assert!(bridge.pin_mut().initialize(&data_dir.to_string_lossy()));

    // Discovery always contains the synthetic mock entry (camera_type 2);
    // hardware lists are empty without the SDKs on this platform.
    let discovery = bridge.pin_mut().fetch_camera_discovery();
    assert!(discovery.valid);
    assert!(
        discovery.cameras.iter().any(|c| c.camera_type == 2),
        "mock camera entry missing from discovery"
    );

    // The boot selection is authoritative: on platforms without the camera
    // SDKs the backend falls back to a mock factory at initialize, so the
    // mode is either None (hardware default) or Mock (fallback) — never a
    // hardware/MindVision claim without the SDK.
    let selection = bridge.pin_mut().fetch_camera_selection();
    assert!(selection.valid);
    assert!(
        selection.mode == 0 || selection.mode == 1,
        "unexpected boot selection mode {}",
        selection.mode
    );

    // Structured errors for invalid selections / paths.
    assert!(!bridge.pin_mut().select_hardware_camera(-1, 0, "bad").ok);
    assert!(!bridge.pin_mut().select_mindvision_camera(-1, "bad", "").ok);
    let script = bridge.pin_mut().apply_camera_script("/nonexistent/script.js");
    assert!(!script.ok && script.message.contains("No hardware camera selected"));
    assert!(!bridge.pin_mut().reset_hardware_camera().ok);

    // Configure the mock source; the snapshot becomes authoritative.
    assert!(bridge
        .pin_mut()
        .configure_mock_camera(&frame_dir.to_string_lossy(), 7, true)
        .ok);
    let selection = bridge.pin_mut().fetch_camera_selection();
    assert_eq!(selection.mode, 1, "expected Mock selection mode");
    assert!(selection.configured);
    assert_eq!(selection.mock_interval_ms, 7);
    assert!(selection.mock_loop);
    assert!(selection.mock_frame_dir.ends_with(
        frame_dir.file_name().unwrap().to_str().unwrap()
    ));
    assert!(!selection.running);

    // Selection survives capture start/stop and reports running state.
    assert!(bridge.pin_mut().start_capture().ok);
    let running = bridge.pin_mut().fetch_camera_selection();
    assert!(running.running && running.mode == 1 && running.configured);
    assert!(bridge.pin_mut().stop_capture().ok);
    let stopped = bridge.pin_mut().fetch_camera_selection();
    assert!(!stopped.running && stopped.mode == 1 && stopped.configured);

    bridge.pin_mut().shutdown();
    let _ = std::fs::remove_dir_all(&frame_dir);
    let _ = std::fs::remove_dir_all(&data_dir);
}

// BE-5: bounded monitoring snapshot semantics and the sorter trigger contract
// (manual, periodic, stop, and failure behavior with the mock camera's
// trigger-output emulation).
#[test]
#[serial]
fn monitoring_and_trigger_contract() {
    let frame_dir = make_frame_dir();
    let data_dir = std::env::temp_dir().join(format!("mib_bridge_mon_data_{}", std::process::id()));

    let mut bridge = ffi::new_backend_bridge();
    assert!(bridge.pin_mut().initialize(&data_dir.to_string_lossy()));
    assert!(bridge
        .pin_mut()
        .configure_mock_camera(&frame_dir.to_string_lossy(), 5, true)
        .ok);

    // Failure behavior: no camera attached yet -> manual pulse and periodic
    // start fail with a structured message.
    let res = bridge.pin_mut().trigger_manual_pulse();
    assert!(!res.ok && res.message.contains("No camera"), "{}", res.message);
    assert!(!bridge.pin_mut().trigger_periodic_start(10).ok);
    // Invalid parameters are structured errors.
    assert!(!bridge.pin_mut().trigger_set_pulse_duration(0).ok);
    assert!(!bridge.pin_mut().trigger_periodic_start(0).ok);

    // Monitoring enable/disable/clear round-trip.
    assert!(bridge.pin_mut().monitoring_set_active(true).ok);
    let snap = bridge.pin_mut().fetch_monitoring_snapshot(50);
    assert!(snap.valid && snap.monitoring_active);
    assert_eq!(snap.capacity, 1000);
    assert!(snap.rows.len() <= 50, "snapshot not bounded: {}", snap.rows.len());
    assert!(bridge.pin_mut().monitoring_clear().ok);
    let cleared = bridge.pin_mut().fetch_monitoring_snapshot(50);
    assert_eq!(cleared.valid_appended, 0);
    assert_eq!(cleared.invalid_appended, 0);
    assert!(bridge.pin_mut().monitoring_set_active(false).ok);
    assert!(!bridge.pin_mut().fetch_monitoring_snapshot(50).monitoring_active);

    // Attach the camera by starting capture; the trigger service receives it
    // through the camera-ready callback.
    assert!(bridge.pin_mut().start_capture().ok);
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline && !bridge.pin_mut().fetch_trigger_status().camera_attached {
        std::thread::sleep(Duration::from_millis(10));
    }
    let status = bridge.pin_mut().fetch_trigger_status();
    assert!(status.valid && status.camera_attached, "trigger camera never attached");

    // Pulse duration round-trips.
    assert!(bridge.pin_mut().trigger_set_pulse_duration(5).ok);
    assert_eq!(bridge.pin_mut().fetch_trigger_status().pulse_duration_us, 5);

    // Manual pulse increments the trigger count.
    let before = bridge.pin_mut().fetch_trigger_status().trigger_count;
    assert!(bridge.pin_mut().trigger_manual_pulse().ok);
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline
        && bridge.pin_mut().fetch_trigger_status().trigger_count <= before
    {
        std::thread::sleep(Duration::from_millis(5));
    }
    assert!(
        bridge.pin_mut().fetch_trigger_status().trigger_count > before,
        "manual pulse did not fire"
    );

    // Periodic test: counts grow while running, stop halts the generator.
    let before = bridge.pin_mut().fetch_trigger_status().trigger_count;
    assert!(bridge.pin_mut().trigger_periodic_start(5).ok);
    assert!(bridge.pin_mut().fetch_trigger_status().periodic_active);
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline
        && bridge.pin_mut().fetch_trigger_status().trigger_count < before + 3
    {
        std::thread::sleep(Duration::from_millis(10));
    }
    assert!(
        bridge.pin_mut().fetch_trigger_status().trigger_count >= before + 3,
        "periodic test did not generate pulses"
    );
    assert!(bridge.pin_mut().trigger_periodic_stop().ok);
    assert!(!bridge.pin_mut().fetch_trigger_status().periodic_active);

    assert!(bridge.pin_mut().stop_capture().ok);
    bridge.pin_mut().shutdown();
    let _ = std::fs::remove_dir_all(&frame_dir);
    let _ = std::fs::remove_dir_all(&data_dir);
}

// The Rust enum values must match contract/bridge-contract.json — the single
// source of truth also pinned by shim.cpp static_asserts (C++) and the
// generated desktop/src/bridgeContract.ts (TypeScript). Drift fails here.
#[test]
fn rust_enums_match_contract_json() {
    let contract: serde_json::Value =
        serde_json::from_str(include_str!("../contract/bridge-contract.json")).unwrap();

    assert_eq!(
        contract["abi_version"].as_u64().unwrap() as u32,
        ffi::bridge_abi_version(),
        "abi_version drifted between JSON contract and bridge"
    );

    let kinds = contract["event_kinds"].as_object().unwrap();
    let expected: &[(&str, BridgeEventKind)] = &[
        ("FrameReady", BridgeEventKind::FrameReady),
        ("CameraStatus", BridgeEventKind::CameraStatus),
        ("RecordingStatus", BridgeEventKind::RecordingStatus),
        ("ProcessingResult", BridgeEventKind::ProcessingResult),
        ("PlaybackPosition", BridgeEventKind::PlaybackPosition),
        ("BackendError", BridgeEventKind::BackendError),
        ("OperationStatus", BridgeEventKind::OperationStatus),
        ("QueueOverflow", BridgeEventKind::QueueOverflow),
        ("ExperimentStatus", BridgeEventKind::ExperimentStatus),
    ];
    assert_eq!(kinds.len(), expected.len(), "event kind count drifted");
    for (name, kind) in expected {
        assert_eq!(
            kinds[*name].as_u64().unwrap() as u32,
            kind.repr,
            "event kind {name} drifted from the JSON contract"
        );
    }
}

// Duplicate/late commands must fail safely without desynchronizing state
// (BE-1): double start is idempotent, double stop is idempotent, and the
// pipeline still works afterwards.
#[test]
#[serial]
fn duplicate_start_stop_cannot_desynchronize() {
    let frame_dir = make_frame_dir();
    let data_dir = std::env::temp_dir().join(format!("mib_bridge_dup_data_{}", std::process::id()));

    let mut bridge = ffi::new_backend_bridge();
    assert!(bridge.pin_mut().initialize(&data_dir.to_string_lossy()));
    assert!(bridge
        .pin_mut()
        .configure_mock_camera(&frame_dir.to_string_lossy(), 5, true)
        .ok);

    assert!(bridge.pin_mut().start_capture().ok);
    // Duplicate start while running: must not tear down or double-spawn.
    assert!(bridge.pin_mut().start_capture().ok);

    let deadline = Instant::now() + Duration::from_secs(5);
    let mut got = false;
    while Instant::now() < deadline {
        if bridge.pin_mut().fetch_latest_frame().valid {
            got = true;
            break;
        }
        std::thread::sleep(Duration::from_millis(10));
    }
    assert!(got, "no frame after duplicate start");

    assert!(bridge.pin_mut().stop_capture().ok);
    // Duplicate stop: idempotent.
    assert!(bridge.pin_mut().stop_capture().ok);

    // The pipeline still works after the duplicate commands.
    assert!(bridge.pin_mut().start_capture().ok);
    assert!(bridge.pin_mut().stop_capture().ok);

    bridge.pin_mut().shutdown();
    let _ = std::fs::remove_dir_all(&frame_dir);
    let _ = std::fs::remove_dir_all(&data_dir);
}

// Cancelling an unknown or finished operation fails safely (BE-1).
#[test]
#[serial]
fn cancel_unknown_operation_fails_safely() {
    let data_dir =
        std::env::temp_dir().join(format!("mib_bridge_cancel_data_{}", std::process::id()));
    let mut bridge = ffi::new_backend_bridge();
    assert!(bridge.pin_mut().initialize(&data_dir.to_string_lossy()));

    let res = bridge.pin_mut().cancel_operation(999_999);
    assert!(!res.ok, "cancelling an unknown operation must not report ok");
    assert!(
        res.message.to_lowercase().contains("unknown"),
        "unexpected message: {}",
        res.message
    );

    bridge.pin_mut().shutdown();
    let _ = std::fs::remove_dir_all(&data_dir);
}

// A recording load is a tracked operation: the command result carries a
// non-zero operation_id and Started/Completed OperationStatus events with the
// same id bracket it (BE-1).
#[test]
#[serial]
fn recording_load_emits_operation_lifecycle() {
    let frame_dir = make_frame_dir();
    let data_dir = std::env::temp_dir().join(format!("mib_bridge_op_data_{}", std::process::id()));
    let rec_path = std::env::temp_dir().join(format!("mib_bridge_op_{}.h5", std::process::id()));

    let mut bridge = ffi::new_backend_bridge();
    assert!(bridge.pin_mut().initialize(&data_dir.to_string_lossy()));
    assert!(bridge
        .pin_mut()
        .configure_mock_camera(&frame_dir.to_string_lossy(), 5, true)
        .ok);
    assert!(bridge.pin_mut().start_capture().ok);
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline && !bridge.pin_mut().fetch_latest_frame().valid {
        std::thread::sleep(Duration::from_millis(10));
    }
    assert!(bridge.pin_mut().start_frame_recording(&rec_path.to_string_lossy()).ok);
    std::thread::sleep(Duration::from_millis(200));
    assert!(bridge.pin_mut().stop_frame_recording().ok);
    assert!(bridge.pin_mut().stop_capture().ok);
    // Discard events so far; focus on the load.
    let _ = bridge.pin_mut().poll_events();

    let load = bridge.pin_mut().load_recording(&rec_path.to_string_lossy());
    assert!(load.ok, "load_recording failed: {}", load.message);
    assert!(load.operation_id != 0, "load did not report an operation id");

    let events = bridge.pin_mut().poll_events();
    let op_events: Vec<_> = events
        .iter()
        .filter(|e| e.kind == BridgeEventKind::OperationStatus && e.u0 == load.operation_id)
        .collect();
    // u2 = state: 0 Started .. 2 Completed (contract values).
    assert!(
        op_events.iter().any(|e| e.u2 == 0),
        "no Started operation event for the load"
    );
    assert!(
        op_events.iter().any(|e| e.u2 == 2),
        "no Completed operation event for the load"
    );
    // A finished operation can no longer be cancelled.
    assert!(!bridge.pin_mut().cancel_operation(load.operation_id).ok);

    bridge.pin_mut().shutdown();
    let _ = std::fs::remove_dir_all(&frame_dir);
    let _ = std::fs::remove_dir_all(&data_dir);
    let _ = std::fs::remove_file(&rec_path);
}

// Mock E2E for the experiment lifecycle (BE-4): preconditions, start →
// accumulate → stop → the finalized HDF5 reopens, and the experiment's
// operation lifecycle completes.
#[test]
#[serial]
fn experiment_lifecycle_end_to_end() {
    let frame_dir = make_frame_dir();
    let data_dir = std::env::temp_dir().join(format!("mib_bridge_exp_data_{}", std::process::id()));
    let out_path = std::env::temp_dir().join(format!("mib_bridge_exp_{}.h5", std::process::id()));

    let mut bridge = ffi::new_backend_bridge();
    assert!(bridge.pin_mut().initialize(&data_dir.to_string_lossy()));
    assert!(bridge
        .pin_mut()
        .configure_mock_camera(&frame_dir.to_string_lossy(), 5, true)
        .ok);

    // Precondition: camera must be running (Qt-parity failure message).
    let early = bridge.pin_mut().experiment_start(&out_path.to_string_lossy());
    assert!(!early.ok, "experiment started without a running camera");
    assert!(
        early.message.contains("Camera must be running"),
        "unexpected precondition message: {}",
        early.message
    );

    assert!(bridge.pin_mut().apply_processing(true, 1.0).ok);
    assert!(bridge.pin_mut().start_capture().ok);
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline && !bridge.pin_mut().fetch_latest_frame().valid {
        std::thread::sleep(Duration::from_millis(10));
    }

    let start = bridge.pin_mut().experiment_start(&out_path.to_string_lossy());
    assert!(start.ok, "experiment_start failed: {}", start.message);
    assert!(start.operation_id != 0, "experiment has no operation id");

    // Duplicate start must fail without desynchronizing state.
    assert!(!bridge.pin_mut().experiment_start(&out_path.to_string_lossy()).ok);

    // The camera cannot stop underneath an active experiment (Qt parity).
    assert!(!bridge.pin_mut().stop_capture().ok);

    let status = bridge.pin_mut().fetch_experiment_status();
    assert!(status.valid);
    assert_eq!(status.state, 2, "experiment should be Active");
    assert!(status.output_path.ends_with(".h5"));

    std::thread::sleep(Duration::from_millis(400)); // let frames accumulate

    let stop = bridge.pin_mut().experiment_stop();
    assert!(stop.ok, "experiment_stop failed: {}", stop.message);

    // The stop finalizes asynchronously; wait for the terminal Idle status.
    let deadline = Instant::now() + Duration::from_secs(10);
    let mut finalized = false;
    while Instant::now() < deadline {
        let s = bridge.pin_mut().fetch_experiment_status();
        if s.valid && s.state == 0 {
            finalized = true;
            break;
        }
        assert_ne!(s.state, 4, "experiment failed: {}", s.message);
        std::thread::sleep(Duration::from_millis(20));
    }
    assert!(finalized, "experiment did not finalize within 10s");

    // Terminal events: an ExperimentStatus(Idle) and the operation Completed.
    let events = bridge.pin_mut().poll_events();
    assert!(
        events
            .iter()
            .any(|e| e.kind == BridgeEventKind::ExperimentStatus && e.u0 == 0),
        "no terminal ExperimentStatus event"
    );
    assert!(
        events
            .iter()
            .any(|e| e.kind == BridgeEventKind::OperationStatus
                && e.u0 == start.operation_id
                && e.u2 == 2),
        "no Completed operation event for the experiment"
    );

    // Double stop fails safely.
    assert!(!bridge.pin_mut().experiment_stop().ok);

    // The finalized file reopens through the review path.
    assert!(bridge.pin_mut().stop_capture().ok);
    let status = bridge.pin_mut().fetch_experiment_status();
    let load = bridge.pin_mut().load_recording(&status.output_path);
    assert!(load.ok, "finalized experiment file failed to load: {}", load.message);

    bridge.pin_mut().shutdown();
    let _ = std::fs::remove_dir_all(&frame_dir);
    let _ = std::fs::remove_dir_all(&data_dir);
    let _ = std::fs::remove_file(&status.output_path);
}

// The shim event queue is bounded drop-oldest and overflow is observable
// (BE-1): with a tiny capacity, flooding events yields a bounded poll batch
// prefixed by a QueueOverflow marker.
#[test]
#[serial]
fn event_queue_overflow_is_bounded_and_observable() {
    std::env::set_var("MIB_BRIDGE_MAX_QUEUE", "8");
    let data_dir =
        std::env::temp_dir().join(format!("mib_bridge_ovf_data_{}", std::process::id()));

    let mut bridge = ffi::new_backend_bridge();
    assert!(bridge.pin_mut().initialize(&data_dir.to_string_lossy()));
    let _ = bridge.pin_mut().poll_events();

    // Every seek emits a PlaybackPosition event even with no frames loaded.
    for _ in 0..40 {
        let _ = bridge.pin_mut().playback_seek_latest();
    }

    let events = bridge.pin_mut().poll_events();
    assert!(
        events.len() <= 8 + 1,
        "poll batch exceeded the bounded capacity: {}",
        events.len()
    );
    let overflow = &events[0];
    assert_eq!(
        overflow.kind,
        BridgeEventKind::QueueOverflow,
        "first event of an overflowed batch must be the QueueOverflow marker"
    );
    assert!(overflow.u0 > 0, "overflow marker reports zero dropped events");
    assert_eq!(overflow.u1, bridge.queue_overflow_total());
    assert!(bridge.queue_overflow_total() > 0);

    bridge.pin_mut().shutdown();
    std::env::remove_var("MIB_BRIDGE_MAX_QUEUE");
    let _ = std::fs::remove_dir_all(&data_dir);
}

#[test]
#[serial]
fn processing_settings_and_stats() {
    let frame_dir = make_frame_dir();
    let data_dir = std::env::temp_dir().join(format!("mib_bridge_proc_data_{}", std::process::id()));

    let mut bridge = ffi::new_backend_bridge();
    assert!(bridge.pin_mut().initialize(&data_dir.to_string_lossy()));
    assert!(bridge
        .pin_mut()
        .configure_mock_camera(&frame_dir.to_string_lossy(), 5, true)
        .ok);

    // Apply processing settings (enable realtime + a pixel→micron scale).
    let res = bridge.pin_mut().apply_processing(true, 2.5);
    assert!(res.ok, "apply_processing failed: {}", res.message);

    assert!(bridge.pin_mut().start_capture().ok);
    std::thread::sleep(Duration::from_millis(200));

    let stats = bridge.pin_mut().fetch_processing_stats();
    assert!(stats.valid, "fetch_processing_stats returned invalid");
    // The scale we set should round-trip.
    assert!((stats.pixel_to_micron - 2.5).abs() < 1e-9, "pixel_to_micron not applied");

    assert!(bridge.pin_mut().stop_capture().ok);
    bridge.pin_mut().shutdown();
    let _ = std::fs::remove_dir_all(&frame_dir);
    let _ = std::fs::remove_dir_all(&data_dir);
}

#[test]
#[serial]
fn lifecycle_produces_status_and_frame_events() {
    let frame_dir = make_frame_dir();
    let data_dir = std::env::temp_dir().join(format!("mib_bridge_data_{}", std::process::id()));

    let mut bridge = ffi::new_backend_bridge();
    assert!(!bridge.is_initialized());

    assert!(
        bridge.pin_mut().initialize(&data_dir.to_string_lossy()),
        "backend facade initialize failed"
    );
    assert!(bridge.is_initialized());

    // Configure the mock camera through the command channel.
    let res = bridge.pin_mut().configure_mock_camera(
        &frame_dir.to_string_lossy(),
        5, // ms between frames
        true,
    );
    assert!(res.ok, "configure_mock_camera failed: {}", res.message);

    // A CameraStatus event reporting configured should have been emitted.
    let configured = drain_until(
        &mut bridge,
        |e| e.kind == BridgeEventKind::CameraStatus && e.b0, // b0 = configured
        Duration::from_secs(2),
    );
    assert!(configured.is_some(), "no CameraStatus(configured) event");

    // Start capture; live frames flow into the playback store and are pulled on
    // demand (never pushed through the event channel, never base64 — ADR 0003).
    let res = bridge.pin_mut().start_capture();
    assert!(res.ok, "start_capture failed: {}", res.message);

    // Poll the pull path until a live frame is available.
    let mut frame = ffi::BridgeFrame::default();
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline {
        let f = bridge.pin_mut().fetch_latest_frame();
        if f.valid {
            frame = f;
            break;
        }
        std::thread::sleep(Duration::from_millis(10));
    }
    assert!(frame.valid, "no live frame available within 5s");
    // The sample frame is 512x96.
    assert_eq!(frame.width, 512, "unexpected frame width");
    assert_eq!(frame.height, 96, "unexpected frame height");
    assert!(!frame.data.is_empty(), "frame has no pixel bytes");
    if frame.stride_bytes > 0 {
        assert_eq!(
            frame.data.len() as u64,
            frame.stride_bytes * frame.height,
            "pixel byte count should equal stride * height"
        );
    }

    // Seek to latest — this is the push path: it emits FrameReady +
    // PlaybackPosition events the webview subscribes to.
    let res = bridge.pin_mut().playback_seek_latest();
    assert!(res.ok, "playback_seek_latest failed: {}", res.message);

    let frame_ready = drain_until(
        &mut bridge,
        |e| e.kind == BridgeEventKind::FrameReady,
        Duration::from_secs(2),
    );
    let fr = frame_ready.expect("no FrameReady event after seek");
    // u2 = width, u3 = height from the flattened FrameReady mapping.
    assert!(fr.u2 > 0 && fr.u3 > 0, "FrameReady reported zero dimensions");

    // Stop and shut down cleanly.
    let res = bridge.pin_mut().stop_capture();
    assert!(res.ok, "stop_capture failed: {}", res.message);
    bridge.pin_mut().shutdown();
    assert!(!bridge.is_initialized());

    let _ = std::fs::remove_dir_all(&frame_dir);
    let _ = std::fs::remove_dir_all(&data_dir);
}

#[test]
#[serial]
fn record_then_load_and_review() {
    let frame_dir = make_frame_dir();
    let data_dir = std::env::temp_dir().join(format!("mib_bridge_rec_data_{}", std::process::id()));
    let rec_path = std::env::temp_dir().join(format!("mib_bridge_rec_{}.h5", std::process::id()));

    let mut bridge = ffi::new_backend_bridge();
    assert!(bridge.pin_mut().initialize(&data_dir.to_string_lossy()));
    assert!(bridge
        .pin_mut()
        .configure_mock_camera(&frame_dir.to_string_lossy(), 5, true)
        .ok);
    assert!(bridge.pin_mut().start_capture().ok);

    // Let some frames flow, then record a short clip to HDF5.
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline && !bridge.pin_mut().fetch_latest_frame().valid {
        std::thread::sleep(Duration::from_millis(10));
    }
    let rec = bridge.pin_mut().start_frame_recording(&rec_path.to_string_lossy());
    assert!(rec.ok, "start_frame_recording failed: {}", rec.message);
    std::thread::sleep(Duration::from_millis(200));
    assert!(bridge.pin_mut().stop_frame_recording().ok);
    assert!(bridge.pin_mut().stop_capture().ok);

    // Load the recording back and review it by absolute index.
    let load = bridge.pin_mut().load_recording(&rec_path.to_string_lossy());
    assert!(load.ok, "load_recording failed: {}", load.message);

    let seek = bridge.pin_mut().playback_seek_index(0);
    assert!(seek.ok, "playback_seek_index failed: {}", seek.message);

    let frame = bridge.pin_mut().fetch_frame_by_index(0);
    assert!(frame.valid, "fetch_frame_by_index(0) returned invalid");
    assert!(frame.width > 0 && frame.height > 0 && !frame.data.is_empty());

    bridge.pin_mut().shutdown();
    let _ = std::fs::remove_dir_all(&frame_dir);
    let _ = std::fs::remove_dir_all(&data_dir);
    let _ = std::fs::remove_file(&rec_path);
}
