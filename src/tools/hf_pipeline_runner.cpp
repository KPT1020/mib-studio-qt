// Offline pipeline runner for HuggingFace-style image datasets.
//
// Streams a folder of grayscale images (produced by scripts/hf_dataset_download.py)
// through the real C++ ProcessingService and writes the standard experiment HDF5.
// This is the verification path: the algorithm exercised here is the exact
// production pipeline, not a reimplementation.
//
// Usage:
//   hf_pipeline_runner \
//     --input  <folder-of-PNGs> \
//     --output <file.h5> \
//     [--config <path/to/config.json>] \
//     [--data-dir <dir>] \
//     [--interval-ms <n>] \
//     [--invalid-sample-rate <n>]
//
// Lifecycle mirrors frontend::ExperimentController: openFile -> initializeDatasets
// -> start capture+realtime+experiment -> drain -> flushBufferedFrames ->
// writeExperimentInfo -> writeConfigJson -> closeFile.

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/Hdf5Service.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

#ifdef _WIN32
void setEnv(const char* key, const std::string& value) {
    _putenv_s(key, value.c_str());
}
#else
void setEnv(const char* key, const std::string& value) {
    setenv(key, value.c_str(), 1);
}
#endif

struct CliArgs {
    std::filesystem::path input;
    std::filesystem::path output;
    std::filesystem::path dataDir = "data";
    std::filesystem::path configPath;            // optional
    std::filesystem::path backgroundImagePath;   // optional
    int intervalMs = 1;
    size_t invalidSampleRate = 1;                // 1 = keep every invalid frame
    size_t flushIntervalFrames = 200;
    bool ok = true;
    std::string error;
};

void printUsage() {
    std::cerr <<
        "hf_pipeline_runner --input <dir> --output <file.h5> [options]\n"
        "  --input <dir>                Folder of PNG/JPG/TIFF frames (grayscale)\n"
        "  --output <file.h5>           Destination HDF5 (will be created/overwritten)\n"
        "  --config <path>              Optional config.json (defaults used otherwise)\n"
        "  --data-dir <dir>             Working data dir for logs/sqlite. Default: ./data\n"
        "  --background <path>          Optional background image (applied pre-processing)\n"
        "  --interval-ms <n>            MockCamera frame interval. Default: 1 (fastest)\n"
        "  --invalid-sample-rate <n>    Save every Nth invalid frame. Default: 1 (all)\n"
        "  --flush-every <n>            Flush buffered frames every N processed. Default: 200\n";
}

CliArgs parseArgs(int argc, char** argv) {
    CliArgs out;
    auto need = [&](int& i, const char* flag) -> const char* {
        if (i + 1 >= argc) {
            out.ok = false;
            out.error = std::string("missing value for ") + flag;
            return nullptr;
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            printUsage();
            std::exit(0);
        } else if (a == "--input") {
            if (auto v = need(i, "--input")) out.input = v;
        } else if (a == "--output") {
            if (auto v = need(i, "--output")) out.output = v;
        } else if (a == "--config") {
            if (auto v = need(i, "--config")) out.configPath = v;
        } else if (a == "--data-dir") {
            if (auto v = need(i, "--data-dir")) out.dataDir = v;
        } else if (a == "--background") {
            if (auto v = need(i, "--background")) out.backgroundImagePath = v;
        } else if (a == "--interval-ms") {
            if (auto v = need(i, "--interval-ms")) out.intervalMs = std::max(0, std::atoi(v));
        } else if (a == "--invalid-sample-rate") {
            if (auto v = need(i, "--invalid-sample-rate"))
                out.invalidSampleRate = std::max<size_t>(1, static_cast<size_t>(std::atoll(v)));
        } else if (a == "--flush-every") {
            if (auto v = need(i, "--flush-every"))
                out.flushIntervalFrames = std::max<size_t>(1, static_cast<size_t>(std::atoll(v)));
        } else {
            out.ok = false;
            out.error = "unknown argument: " + a;
            return out;
        }
        if (!out.ok) return out;
    }
    if (out.input.empty() || out.output.empty()) {
        out.ok = false;
        out.error = "--input and --output are required";
    }
    return out;
}

int _to_odd(int v) {
    if (v < 1) v = 1;
    if ((v % 2) == 0) v += 1;
    return v;
}

// Parse the same config.json schema that AppConfigWatcher applies in the UI.
// Returns (config, rawJson). rawJson is stashed in AppBackend so HDF5 persists it.
bool loadProcessingConfig(const std::filesystem::path& path,
                          backend::services::ProcessingConfig& pcfg,
                          std::string& rawJsonOut,
                          double& pixelToMicronOut,
                          backend::services::ProcessingService::Roi& roiOut) {
    std::ifstream f(path);
    if (!f) {
        SPDLOG_WARN("Config not readable: {} (using service defaults)", path.string());
        return false;
    }
    std::stringstream buf;
    buf << f.rdbuf();
    rawJsonOut = buf.str();

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(rawJsonOut);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Config parse failed: {}: {}", path.string(), e.what());
        return false;
    }

    if (root.contains("image_processing") && root["image_processing"].is_object()) {
        const auto& ip = root["image_processing"];
        auto getI = [&](const char* k, int defv) {
            return ip.value(k, defv);
        };
        auto getD = [&](const char* k, double defv) {
            return ip.value(k, defv);
        };
        pcfg.gaussian_blur_size = getI("gaussian_blur_size", pcfg.gaussian_blur_size);
        pcfg.bg_subtract_threshold = getI("bg_subtract_threshold", pcfg.bg_subtract_threshold);
        pcfg.morph_kernel_size = getI("morph_kernel_size", pcfg.morph_kernel_size);
        pcfg.morph_iterations = getI("morph_iterations", pcfg.morph_iterations);
        pcfg.area_threshold_min = getI("area_threshold_min", pcfg.area_threshold_min);
        pcfg.area_threshold_max = getI("area_threshold_max", pcfg.area_threshold_max);
        pcfg.deformability_threshold_min = getD("deformability_threshold_min", pcfg.deformability_threshold_min);
        pcfg.deformability_threshold_max = getD("deformability_threshold_max", pcfg.deformability_threshold_max);
        pcfg.area_ratio_threshold_max = getD("area_ratio_threshold_max", pcfg.area_ratio_threshold_max);
        pcfg.ring_ratio_min = getD("ring_ratio_min", pcfg.ring_ratio_min);
        pcfg.ring_ratio_max = getD("ring_ratio_max", pcfg.ring_ratio_max);
        pcfg.empty_frame_pixel_threshold = getI("empty_frame_pixel_threshold", pcfg.empty_frame_pixel_threshold);
        pcfg.auto_background_enabled = ip.value("auto_background_enabled", pcfg.auto_background_enabled);
        pcfg.auto_background_empty_frames = getI("auto_background_empty_frames", pcfg.auto_background_empty_frames);
        pcfg.auto_background_cooldown_frames = getI("auto_background_cooldown_frames", pcfg.auto_background_cooldown_frames);

        if (ip.contains("filters") && ip["filters"].is_object()) {
            const auto& fl = ip["filters"];
            pcfg.enable_border_check = fl.value("enable_border_check", pcfg.enable_border_check);
            pcfg.enable_area_range_check = fl.value("enable_area_range_check", pcfg.enable_area_range_check);
            pcfg.enable_deformability_range_check = fl.value("enable_deformability_range_check", pcfg.enable_deformability_range_check);
            pcfg.enable_area_ratio_check = fl.value("enable_area_ratio_check", pcfg.enable_area_ratio_check);
            pcfg.enable_ring_ratio_check = fl.value("enable_ring_ratio_check", pcfg.enable_ring_ratio_check);
            pcfg.require_single_inner_contour = fl.value("require_single_inner_contour", pcfg.require_single_inner_contour);
        }

        if (ip.contains("target_group") && ip["target_group"].is_object()) {
            const auto& tg = ip["target_group"];
            pcfg.enable_target_group = tg.value("enabled", pcfg.enable_target_group);
            pcfg.target_group_area_min = tg.value("area_min", pcfg.target_group_area_min);
            pcfg.target_group_area_max = tg.value("area_max", pcfg.target_group_area_max);
            pcfg.target_group_deformability_min = tg.value("deformability_min", pcfg.target_group_deformability_min);
            pcfg.target_group_deformability_max = tg.value("deformability_max", pcfg.target_group_deformability_max);
            pcfg.enable_target_group_emodulus = tg.value("emodulus_enabled", pcfg.enable_target_group_emodulus);
            pcfg.target_group_emodulus_min = tg.value("emodulus_min", pcfg.target_group_emodulus_min);
            pcfg.target_group_emodulus_max = tg.value("emodulus_max", pcfg.target_group_emodulus_max);
        }

        if (ip.contains("multi_image") && ip["multi_image"].is_object()) {
            const auto& mi = ip["multi_image"];
            pcfg.multi_image_enabled = mi.value("enabled", pcfg.multi_image_enabled);
            pcfg.multi_image_count = std::max(1, mi.value("count", pcfg.multi_image_count));
        }
    }

    // Enforce odd kernel sizes (same as ProcessingService does internally).
    pcfg.gaussian_blur_size = _to_odd(pcfg.gaussian_blur_size);
    pcfg.morph_kernel_size = _to_odd(pcfg.morph_kernel_size);

    if (root.contains("pixel_to_micron_factor")) {
        pixelToMicronOut = root.value("pixel_to_micron_factor", pixelToMicronOut);
    }

    if (root.contains("roi") && root["roi"].is_object()) {
        const auto& r = root["roi"];
        roiOut.x = r.value("x", roiOut.x);
        roiOut.y = r.value("y", roiOut.y);
        roiOut.w = r.value("w", roiOut.w);
        roiOut.h = r.value("h", roiOut.h);
    }

    return true;
}

uint64_t nowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

int main(int argc, char** argv) {
    auto args = parseArgs(argc, argv);
    if (!args.ok) {
        std::cerr << "ERROR: " << args.error << "\n\n";
        printUsage();
        return 2;
    }

    if (!std::filesystem::is_directory(args.input)) {
        std::cerr << "ERROR: --input is not a directory: " << args.input << "\n";
        return 2;
    }
    std::filesystem::create_directories(args.dataDir);
    std::filesystem::create_directories(args.output.parent_path().empty()
                                            ? std::filesystem::path(".")
                                            : args.output.parent_path());

    // Configure the MockCamera via env vars BEFORE AppBackend::initialize.
    setEnv("MIB_CAMERA_MODE", "mock");
    setEnv("MIB_MOCK_CAMERA_DIR", args.input.string());
    setEnv("MIB_MOCK_CAMERA_LOOP", "false");
    setEnv("MIB_MOCK_CAMERA_INTERVAL_MS", std::to_string(std::max(0, args.intervalMs)));

    try {
        backend::AppBackend app;
        if (!app.initialize(args.dataDir.string())) {
            SPDLOG_ERROR("AppBackend::initialize failed");
            return 3;
        }

        auto& processing = app.processing();
        auto& capture    = app.capture();
        auto& hdf5       = app.hdf5();

        // Load processing config (optional; defaults if missing).
        backend::services::ProcessingConfig pcfg = processing.getProcessingConfig();
        double pixelToMicron = processing.getPixelToMicronFactor();
        backend::services::ProcessingService::Roi roi = processing.getRealtimeRoi();
        std::string rawConfigJson;
        if (!args.configPath.empty()) {
            if (!loadProcessingConfig(args.configPath, pcfg, rawConfigJson, pixelToMicron, roi)) {
                SPDLOG_WARN("Continuing with service-default ProcessingConfig");
            }
        }
        processing.setProcessingConfig(pcfg);
        processing.setPixelToMicronFactor(pixelToMicron);
        processing.setRealtimeRoi(roi);
        processing.setInvalidFrameSamplingRate(args.invalidSampleRate);
        processing.setFlushInterval(args.flushIntervalFrames);
        if (!rawConfigJson.empty()) {
            app.setLastConfigJson(rawConfigJson);
        }

        // Optional background image (same semantics as UI "set background"):
        // pre-blurring happens inside ProcessingService.
        cv::Mat backgroundGray;
        if (!args.backgroundImagePath.empty()) {
            backgroundGray = cv::imread(args.backgroundImagePath.string(), cv::IMREAD_GRAYSCALE);
            if (backgroundGray.empty()) {
                SPDLOG_WARN("Failed to read background image: {}", args.backgroundImagePath.string());
            } else {
                processing.setRealtimeBackgroundGray(backgroundGray);
                SPDLOG_INFO("Applied background: {}x{}", backgroundGray.cols, backgroundGray.rows);
            }
        }

        // Open HDF5 + datasets (mirrors ExperimentController::startExperiment).
        if (!hdf5.openFile(args.output.string())) {
            SPDLOG_ERROR("Failed to open HDF5 for writing: {}", args.output.string());
            return 4;
        }
        if (!hdf5.initializeDatasets()) {
            SPDLOG_WARN("initializeDatasets failed (continuing; saveFrames will create on flush)");
        }

        // Start pipeline.
        const uint64_t startTimeNs = nowNs();
        auto frameStore = app.getFrameStore();
        if (!frameStore) {
            SPDLOG_ERROR("AppBackend FrameStore is null");
            hdf5.closeFile();
            return 5;
        }
        processing.startRealtime(frameStore);
        if (!capture.start()) {
            SPDLOG_ERROR("CaptureService::start failed (mock camera could not be opened)");
            processing.stopRealtime();
            hdf5.closeFile();
            return 5;
        }
        processing.startExperiment();
        SPDLOG_INFO("HF runner started: input={} output={}",
                    args.input.string(), args.output.string());

        // Drain loop.
        // - MockCamera with loopFiles=false exits start/grab when exhausted, so
        //   capture.isRunning() will flip to false once it finishes the folder.
        // - After that, wait for the processing job queue to drain.
        // - Flush buffered frames periodically to avoid RAM blowup on large sets.
        using clock = std::chrono::steady_clock;
        auto lastProgress = clock::now();
        uint64_t lastProcessed = 0;
        const auto quietDeadline = std::chrono::seconds(30);

        while (true) {
            const bool running = capture.isRunning();
            const auto& cs = capture.stats();
            const auto& ps = processing.stats();
            const uint64_t framesIn   = cs.framesProcessed.load();
            const uint64_t jobsQueued = ps.jobsQueued.load();
            const uint64_t jobsDone   = ps.jobsProcessed.load();

            // Periodic flush to bound memory.
            processing.flushBufferedFrames(hdf5);

            if (jobsDone != lastProcessed) {
                lastProcessed = jobsDone;
                lastProgress = clock::now();
            }

            // Exit when capture finished and queue drained.
            if (!running && jobsQueued == jobsDone) {
                break;
            }
            // Safety: break out if no progress for a long while (avoids hang on bad input).
            if (clock::now() - lastProgress > quietDeadline) {
                SPDLOG_WARN("No progress for 30s (capture_running={}, queued={}, done={}); exiting drain",
                            running, jobsQueued, jobsDone);
                break;
            }

            SPDLOG_DEBUG("runner: captured={} jobs {}/{}", framesIn, jobsDone, jobsQueued);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Stop capture (idempotent if already stopped by exhaustion) and tear
        // down the realtime processing thread.
        capture.stop();
        processing.stopRealtime();

        // Final flush + experiment info (mirrors MainWindow::stopExperiment order:
        // flushBufferedFrames -> endExperiment -> getValidFrames/getInvalidFrames ->
        // appendFrames(remaining) -> writeExperimentInfo).
        const size_t finalFlushed = processing.flushBufferedFrames(hdf5);
        processing.endExperiment();
        auto remainingValid   = processing.getValidFrames();
        auto remainingInvalid = processing.getInvalidFrames();
        if (!remainingValid.empty() || !remainingInvalid.empty()) {
            if (!hdf5.appendFrames(remainingValid, remainingInvalid)) {
                SPDLOG_WARN("appendFrames: failed to persist {} valid + {} invalid remaining frames",
                            remainingValid.size(), remainingInvalid.size());
            }
        }

        const uint64_t endTimeNs = nowNs();
        // Note: writeExperimentInfo's totals are only the tail that was still in-memory
        // (matching MainWindow behavior). The HDF5 datasets themselves are the
        // authoritative count and are correctly sized by appendFrames.
        const size_t tailValid   = remainingValid.size();
        const size_t tailInvalid = remainingInvalid.size();

        const auto finalPcfg = processing.getProcessingConfig();
        const auto finalRoi  = processing.getRealtimeRoi();
        const cv::Mat finalBg = processing.getRealtimeBackgroundGray();
        hdf5.writeExperimentInfo(startTimeNs, endTimeNs,
                                 tailValid, tailInvalid,
                                 finalPcfg, finalRoi,
                                 finalBg.empty() ? nullptr : &finalBg);
        if (!rawConfigJson.empty()) {
            hdf5.writeConfigJson(rawConfigJson);
        }
        hdf5.closeFile();

        const auto& cs = capture.stats();
        const auto& ps = processing.stats();
        const double elapsedS = std::max(1.0,
            static_cast<double>(endTimeNs - startTimeNs) / 1e9);
        std::cout << "hf_pipeline_runner: done\n"
                  << "  input:           " << args.input << "\n"
                  << "  output:          " << args.output << "\n"
                  << "  frames captured: " << cs.framesProcessed.load() << "\n"
                  << "  jobs processed:  " << ps.jobsProcessed.load()
                  << " / " << ps.jobsQueued.load() << "\n"
                  << "  final flush:     " << finalFlushed << "\n"
                  << "  elapsed:         " << elapsedS << " s\n"
                  << "  approx FPS:      " << (ps.jobsProcessed.load() / elapsedS) << "\n";
        return 0;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("hf_pipeline_runner exception: {}", ex.what());
        return 10;
    }
}
