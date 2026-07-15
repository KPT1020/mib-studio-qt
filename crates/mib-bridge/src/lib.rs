//! Rust <-> C++ bridge over the Qt-free `mib_backend` `BackendFacade`.
//!
//! Epic #246, ADR 0003. This crate wraps `backend::bridge::BackendFacade` behind
//! a `cxx` FFI boundary so the Tauri/Rust shell can drive the C++ backend without
//! Qt. The Rust side never sees `AppBackend`, OpenCV, or HDF5 — only flat command
//! submitters, a poll-drained event queue, and an on-demand frame pull.
//!
//! Threading: the backend emits events from its own capture/processing threads.
//! The C++ shim enqueues each event onto an internal mutex-guarded queue and
//! returns immediately (non-blocking sink, per ADR 0003); Rust drains it with
//! [`ffi::BackendBridge::poll_events`]. Frame pixels are pulled with
//! [`ffi::BackendBridge::fetch_latest_frame`] — never pushed through the event
//! channel and never base64-encoded per frame.

#[cxx::bridge(namespace = "mib_bridge")]
pub mod ffi {
    /// Flattened result of a dispatched command. `command` mirrors
    /// `backend::bridge::BackendCommandType` as a small integer.
    #[derive(Debug, Clone)]
    pub struct BridgeCommandResult {
        pub ok: bool,
        pub command: u32,
        pub message: String,
    }

    /// Discriminant for [`BridgeEvent::kind`], mirroring the
    /// `backend::bridge::BackendEvent` variant order.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    #[repr(u32)]
    pub enum BridgeEventKind {
        FrameReady = 0,
        CameraStatus = 1,
        RecordingStatus = 2,
        ProcessingResult = 3,
        PlaybackPosition = 4,
        BackendError = 5,
    }

    /// A flattened `BackendEvent`. Rather than mirror every variant field, the
    /// bridge carries a small pool of typed slots whose meaning depends on
    /// `kind`. This keeps the FFI schema stable and additive: new fields append
    /// slots, never repurpose them. See `event_to_bridge` in `shim.cpp` for the
    /// per-kind field mapping.
    #[derive(Debug, Clone)]
    pub struct BridgeEvent {
        pub kind: BridgeEventKind,
        /// Unsigned slots (indices, dims, counts, timestamps).
        pub u0: u64,
        pub u1: u64,
        pub u2: u64,
        pub u3: u64,
        pub u4: u64,
        pub u5: u64,
        /// Floating slots (rates, metrics).
        pub f0: f64,
        pub f1: f64,
        pub f2: f64,
        /// Boolean slots (configured/running, playing/hasFrame).
        pub b0: bool,
        pub b1: bool,
        /// Text slot (labels, error/file messages).
        pub text: String,
    }

    /// Pollable snapshot of the realtime processing pipeline. `valid` is false
    /// when the backend is not initialized.
    #[derive(Debug, Clone, Default)]
    pub struct BridgeProcessingStats {
        pub valid: bool,
        pub algo_fps1s: f64,
        pub valid_fps1s: f64,
        pub invalid_fps1s: f64,
        pub pixel_to_micron: f64,
    }

    /// A frame pulled on demand: metadata plus a single owned copy of the pixel
    /// bytes. `valid` is false when no frame is available.
    #[derive(Debug, Clone, Default)]
    pub struct BridgeFrame {
        pub valid: bool,
        pub frame_index: u64,
        pub timestamp_ns: u64,
        pub width: u64,
        pub height: u64,
        pub pixel_format: u64,
        pub stride_bytes: u64,
        pub data: Vec<u8>,
    }

    unsafe extern "C++" {
        include!("mib-bridge/src/shim.h");

        /// Opaque owner of an `AppBackend` + `BackendFacade`. Dropping it calls
        /// `shutdown()` then destroys the backend.
        type BackendBridge;

        /// Construct a fresh, uninitialized bridge.
        fn new_backend_bridge() -> UniquePtr<BackendBridge>;

        /// Schema version of the command/event contract (ADR 0003). Additive
        /// changes bump this.
        fn bridge_abi_version() -> u32;

        fn initialize(self: Pin<&mut BackendBridge>, data_dir: &str) -> bool;
        fn shutdown(self: Pin<&mut BackendBridge>);
        fn is_initialized(&self) -> bool;

        fn configure_mock_camera(
            self: Pin<&mut BackendBridge>,
            frame_dir: &str,
            frame_interval_ms: i32,
            loop_files: bool,
        ) -> BridgeCommandResult;
        fn start_capture(self: Pin<&mut BackendBridge>) -> BridgeCommandResult;
        fn stop_capture(self: Pin<&mut BackendBridge>) -> BridgeCommandResult;
        fn start_frame_recording(self: Pin<&mut BackendBridge>, file_path: &str)
            -> BridgeCommandResult;
        fn stop_frame_recording(self: Pin<&mut BackendBridge>) -> BridgeCommandResult;

        /// Resolve the latest live frame through the playback service. Emits a
        /// FrameReady + PlaybackPosition event pair (the push path the webview
        /// subscribes to).
        fn playback_seek_latest(self: Pin<&mut BackendBridge>) -> BridgeCommandResult;

        /// Load a recorded HDF5 file for review through the playback service.
        fn load_recording(self: Pin<&mut BackendBridge>, file_path: &str)
            -> BridgeCommandResult;

        /// Seek playback to an absolute frame index (review scrubbing). Emits a
        /// FrameReady + PlaybackPosition event pair.
        fn playback_seek_index(self: Pin<&mut BackendBridge>, frame_index: u64)
            -> BridgeCommandResult;

        /// Apply realtime processing settings (enable/disable + pixel→micron
        /// scale). Additive schema v3.
        fn apply_processing(
            self: Pin<&mut BackendBridge>,
            realtime_enabled: bool,
            pixel_to_micron: f64,
        ) -> BridgeCommandResult;

        /// Drain and return all events queued since the last poll.
        fn poll_events(self: Pin<&mut BackendBridge>) -> Vec<BridgeEvent>;

        /// Pull the latest frame's metadata + pixel bytes (one copy).
        fn fetch_latest_frame(self: Pin<&mut BackendBridge>) -> BridgeFrame;

        /// Pull a specific frame by absolute index (metadata + one byte copy).
        fn fetch_frame_by_index(self: Pin<&mut BackendBridge>, frame_index: u64)
            -> BridgeFrame;

        /// Pull the current realtime processing stats (fps + pixel→micron).
        fn fetch_processing_stats(self: Pin<&mut BackendBridge>) -> BridgeProcessingStats;
    }
}

// The bridge object may be MOVED between threads (e.g. held in a Tauri
// `State`/`Mutex`, or an async task), but it is NOT `Sync`: commands funnel
// through the single owner and are not safe to call concurrently. This matches
// the threading contract in ADR 0003 — events are the only thing that fan out,
// and they do so through the shim's own mutex-guarded queue, not through shared
// access to `BackendBridge`. Marking it `Send` (but never `Sync`) is therefore
// sound and is what lets a `Mutex<UniquePtr<BackendBridge>>` be `Send + Sync`.
unsafe impl Send for ffi::BackendBridge {}

// Compile-time guard for the Tauri consumption pattern: a
// `Mutex<UniquePtr<BackendBridge>>` (what a Tauri `State` holds) must be
// `Send + Sync`. This holds iff `BackendBridge: Send` (above) — and breaks
// loudly if someone ever adds a `Sync` requirement the type can't meet.
const _: fn() = || {
    fn assert_send_sync<T: Send + Sync>() {}
    assert_send_sync::<std::sync::Mutex<cxx::UniquePtr<ffi::BackendBridge>>>();
};
