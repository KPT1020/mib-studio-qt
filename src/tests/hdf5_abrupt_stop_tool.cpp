#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/ProcessingService.h"

#include <QImage>
#include <QImageReader>
#include <QString>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace std::chrono_literals;

constexpr const char *kMockDir = "/workspace/data/mock_frames";

QString toQString(const std::filesystem::path &path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromUtf8(path.u8string().c_str());
#endif
}

bool hasSupportedExtension(const std::filesystem::path &path)
{
    static const std::vector<std::string> exts = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"};
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

void configureMockEnvironment()
{
    setenv("MIB_CAMERA_MODE", "mock", 1);
    setenv("MIB_MOCK_CAMERA_DIR", kMockDir, 1);
    setenv("MIB_MOCK_CAMERA_INTERVAL_MS", "2", 1);
    setenv("MIB_MOCK_CAMERA_LOOP", "true", 1);
}

void cleanPathArtifacts(const std::string &path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + ".recovery.h5", ec);
    std::filesystem::remove(path + ".recovery.h5.bak", ec);
    std::filesystem::remove(path + ".recovery.h5.tmp", ec);
    std::filesystem::remove(path + ".preview.png", ec);
}

bool isNonEmptyFile(const std::string &path)
{
    std::error_code ec;
    const auto p = std::filesystem::path(path);
    if (!std::filesystem::is_regular_file(p, ec) || ec)
    {
        return false;
    }
    const auto size = std::filesystem::file_size(p, ec);
    return !ec && size > 0;
}

bool startBackend(backend::AppBackend &backend, bool startRealtime)
{
    configureMockEnvironment();
    if (!backend.initialize("/workspace/data"))
    {
        SPDLOG_ERROR("AppBackend initialize failed");
        return false;
    }
    if (!backend.capture().start())
    {
        SPDLOG_ERROR("Capture start failed");
        return false;
    }
    if (startRealtime)
    {
        backend.processing().startRealtime(backend.getFrameStore());
    }
    return true;
}

std::vector<cv::Mat> loadMockReferenceFrames()
{
    std::vector<cv::Mat> refs;
    std::vector<std::filesystem::path> files;
    const std::filesystem::path dir(kMockDir);
    if (!std::filesystem::exists(dir))
    {
        return refs;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (!hasSupportedExtension(entry.path()))
        {
            continue;
        }
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto &path : files)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });

        QImageReader reader(toQString(path));
        reader.setAutoTransform(true);
        QImage image = reader.read();

        cv::Mat gray;
        if (!image.isNull())
        {
            QImage mono = image.convertToFormat(QImage::Format_Grayscale8);
            cv::Mat wrapped(mono.height(), mono.width(), CV_8UC1,
                            const_cast<uchar *>(mono.constBits()),
                            static_cast<size_t>(mono.bytesPerLine()));
            gray = wrapped.clone();
        }
        else if (ext == ".tif" || ext == ".tiff")
        {
            // MockCamera only falls back to OpenCV for TIFF files.
            cv::Mat fallback = cv::imread(path.string(), cv::IMREAD_GRAYSCALE);
            if (!fallback.empty())
            {
                gray = fallback;
            }
        }

        if (!gray.empty())
        {
            refs.push_back(gray);
        }
    }

    return refs;
}

cv::Mat toGray8(const cv::Mat &src)
{
    cv::Mat gray;
    if (src.empty())
    {
        return gray;
    }
    if (src.channels() == 1)
    {
        if (src.type() == CV_8UC1)
        {
            gray = src;
        }
        else
        {
            src.convertTo(gray, CV_8UC1);
        }
    }
    else
    {
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    }
    return gray;
}

bool areMatsBitIdentical(const cv::Mat &lhs, const cv::Mat &rhs)
{
    if (lhs.empty() || rhs.empty())
    {
        return false;
    }
    if (lhs.type() != rhs.type() || lhs.rows != rhs.rows || lhs.cols != rhs.cols)
    {
        return false;
    }

    const size_t rowBytes = static_cast<size_t>(lhs.cols) * lhs.elemSize();
    for (int r = 0; r < lhs.rows; ++r)
    {
        const uint8_t *a = lhs.ptr<uint8_t>(r);
        const uint8_t *b = rhs.ptr<uint8_t>(r);
        if (std::memcmp(a, b, rowBytes) != 0)
        {
            return false;
        }
    }
    return true;
}

struct IdentityPreviewRow
{
    size_t datasetIndex{0};
    uint64_t sourceIndex{0};
    bool identical{false};
    cv::Mat expected;
    cv::Mat actual;
};

bool writeIdentityComparisonSheet(const std::vector<IdentityPreviewRow> &rows, const std::string &outPath)
{
    if (rows.empty())
    {
        return false;
    }

    constexpr int tileW = 220;
    constexpr int tileH = 220;
    constexpr int headerH = 38;
    constexpr int colGap = 12;
    const int canvasW = (tileW * 2) + colGap;
    const int canvasH = static_cast<int>(rows.size()) * (tileH + headerH);
    cv::Mat canvas(canvasH, canvasW, CV_8UC3, cv::Scalar(12, 12, 12));

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto &row = rows[i];
        const int y0 = static_cast<int>(i) * (tileH + headerH);

        const std::string label = "src=" + std::to_string(row.sourceIndex) +
                                  " saved=" + std::to_string(row.datasetIndex) +
                                  (row.identical ? " OK" : " DIFF");
        const cv::Scalar textColor = row.identical ? cv::Scalar(80, 220, 80) : cv::Scalar(80, 80, 255);
        cv::putText(canvas, label, cv::Point(8, y0 + 24), cv::FONT_HERSHEY_SIMPLEX, 0.6, textColor, 1, cv::LINE_AA);

        if (!row.expected.empty())
        {
            cv::Mat expectedResized;
            cv::resize(row.expected, expectedResized, cv::Size(tileW, tileH), 0.0, 0.0, cv::INTER_AREA);
            cv::Mat expectedBgr;
            cv::cvtColor(expectedResized, expectedBgr, cv::COLOR_GRAY2BGR);
            expectedBgr.copyTo(canvas(cv::Rect(0, y0 + headerH, tileW, tileH)));
        }

        if (!row.actual.empty())
        {
            cv::Mat actualResized;
            cv::resize(row.actual, actualResized, cv::Size(tileW, tileH), 0.0, 0.0, cv::INTER_AREA);
            cv::Mat actualBgr;
            cv::cvtColor(actualResized, actualBgr, cv::COLOR_GRAY2BGR);
            actualBgr.copyTo(canvas(cv::Rect(tileW + colGap, y0 + headerH, tileW, tileH)));
        }
    }

    return cv::imwrite(outPath, canvas);
}

struct DatasetIdentityCheckResult
{
    bool readable{false};
    bool identical{false};
    size_t comparedFrames{0};
    size_t identicalFrames{0};
    size_t mismatchFrames{0};
    std::string previewPath;
};

DatasetIdentityCheckResult validateDatasetIdentity(backend::services::Hdf5Service &hdf5,
                                                   const std::string &datasetPath,
                                                   const std::vector<backend::services::ProcessedFrame> &metadata,
                                                   const std::string &previewPath,
                                                   const std::vector<cv::Mat> &refs)
{
    DatasetIdentityCheckResult result;
    result.previewPath = previewPath;

    size_t count = 0;
    int h = 0;
    int w = 0;
    int c = 0;
    if (!hdf5.getDatasetInfo(datasetPath, count, h, w, c) || count == 0)
    {
        return result;
    }
    result.readable = true;

    if (refs.empty() || metadata.empty())
    {
        SPDLOG_ERROR("Identity check cannot run (refs={}, metadata={})", refs.size(), metadata.size());
        return result;
    }

    const size_t compareCount = std::min(count, metadata.size());
    std::vector<IdentityPreviewRow> previewRows;
    previewRows.reserve(8);

    for (size_t i = 0; i < compareCount; ++i)
    {
        cv::Mat saved;
        if (!hdf5.readImageByIndex(datasetPath, i, saved))
        {
            result.mismatchFrames += 1;
            continue;
        }

        cv::Mat actual = toGray8(saved);
        if (actual.empty())
        {
            result.mismatchFrames += 1;
            continue;
        }

        const uint64_t sourceIndex = metadata[i].index;
        const cv::Mat &expectedRef = refs[sourceIndex % refs.size()];
        if (expectedRef.empty())
        {
            result.mismatchFrames += 1;
            continue;
        }

        const bool identical = areMatsBitIdentical(expectedRef, actual);
        result.comparedFrames += 1;
        if (identical)
        {
            result.identicalFrames += 1;
        }
        else
        {
            result.mismatchFrames += 1;
        }

        if (previewRows.size() < 8)
        {
            previewRows.push_back(IdentityPreviewRow{
                i, sourceIndex, identical, expectedRef, actual});
        }
    }

    if (!previewRows.empty() && !writeIdentityComparisonSheet(previewRows, previewPath))
    {
        SPDLOG_WARN("Failed to write identity comparison sheet: {}", previewPath);
    }

    result.identical = (count > 0 && metadata.size() >= count && result.comparedFrames == count && result.mismatchFrames == 0);
    return result;
}

int runExperiment(const std::string &path)
{
    cleanPathArtifacts(path);
    backend::AppBackend backend;
    if (!startBackend(backend, true))
    {
        return 2;
    }

    auto &hdf5 = backend.hdf5();
    if (!hdf5.openFile(path))
    {
        SPDLOG_ERROR("openFile failed: {}", path);
        return 3;
    }
    if (!hdf5.initializeDatasets())
    {
        SPDLOG_ERROR("initializeDatasets failed");
        return 4;
    }

    backend.processing().setFlushInterval(5);
    backend.processing().setInvalidFrameSamplingRate(1);
    backend.processing().startExperiment();

    std::atomic<bool> keepFlushing{true};
    std::thread flusher([&]() {
        while (keepFlushing.load(std::memory_order_relaxed))
        {
            if (hdf5.isFileOpen())
            {
                backend.processing().flushBufferedFrames(hdf5);
            }
            std::this_thread::sleep_for(50ms);
        }
    });

    SPDLOG_INFO("Experiment writer running: {}", path);
    while (true)
    {
        std::this_thread::sleep_for(1s);
    }

    keepFlushing.store(false, std::memory_order_relaxed);
    flusher.join();
    return 0;
}

int runRecording(const std::string &path)
{
    cleanPathArtifacts(path);
    backend::AppBackend backend;
    if (!startBackend(backend, false))
    {
        return 2;
    }
    if (!backend.startFrameRecording(path))
    {
        SPDLOG_ERROR("startFrameRecording failed: {}", path);
        return 3;
    }

    SPDLOG_INFO("Recording writer running: {}", path);
    while (true)
    {
        std::this_thread::sleep_for(1s);
    }
    return 0;
}

int checkExperiment(const std::string &path)
{
    backend::services::Hdf5Service hdf5;
    hdf5.initialize("/tmp");
    if (!hdf5.loadFile(path))
    {
        SPDLOG_ERROR("Experiment check: loadFile failed for {}", path);
        return 1;
    }

    size_t count = 0;
    int h = 0;
    int w = 0;
    int c = 0;
    const bool validOk = hdf5.getDatasetInfo("/valid_frames/images", count, h, w, c);
    const size_t validCount = validOk ? count : 0;
    const bool invalidOk = hdf5.getDatasetInfo("/invalid_frames/images", count, h, w, c);
    const size_t invalidCount = invalidOk ? count : 0;

    const std::string datasetPath =
        validCount > 0 ? "/valid_frames/images" :
        invalidCount > 0 ? "/invalid_frames/images" :
        "";
    if (datasetPath.empty())
    {
        SPDLOG_ERROR("Experiment check: no image dataset found");
        hdf5.closeFile();
        return 2;
    }

    std::vector<backend::services::ProcessedFrame> metadata;
    const bool metadataOk =
        (datasetPath == "/valid_frames/images") ? hdf5.readValidMetadata(metadata)
                                                 : hdf5.readInvalidMetadata(metadata);

    const auto refs = loadMockReferenceFrames();
    const auto identity = validateDatasetIdentity(hdf5, datasetPath, metadata, path + ".preview.png", refs);
    hdf5.closeFile();

    SPDLOG_INFO("Experiment check: valid={} invalid={} metadata={} compared={} identical={} mismatches={} preview={}",
                validCount, invalidCount, metadata.size(), identity.comparedFrames,
                identity.identicalFrames, identity.mismatchFrames, identity.previewPath);

    if (!metadataOk || metadata.empty())
    {
        SPDLOG_ERROR("Experiment check failed: metadata not readable");
        return 4;
    }
    if (!identity.readable || !identity.identical)
    {
        SPDLOG_ERROR("Experiment check failed: input and saved frames are not bit-identical");
        return 3;
    }
    return 0;
}

int checkRecording(const std::string &path)
{
    backend::services::Hdf5Service hdf5;
    hdf5.initialize("/tmp");
    if (!hdf5.loadFile(path))
    {
        SPDLOG_ERROR("Recording check: loadFile failed for {}", path);
        return 1;
    }

    size_t count = 0;
    int h = 0;
    int w = 0;
    int c = 0;
    const bool imagesOk = hdf5.getDatasetInfo("/recorded_frames/images", count, h, w, c);
    const size_t imageCount = imagesOk ? count : 0;
    std::vector<backend::services::ProcessedFrame> metadata;
    const bool metadataOk = hdf5.readRecordingMetadata(metadata);

    const auto refs = loadMockReferenceFrames();
    const auto identity = validateDatasetIdentity(hdf5, "/recorded_frames/images", metadata,
                                                  path + ".preview.png", refs);
    hdf5.closeFile();

    SPDLOG_INFO("Recording check: images={} metadata={} compared={} identical={} mismatches={} preview={}",
                imageCount, metadata.size(), identity.comparedFrames,
                identity.identicalFrames, identity.mismatchFrames, identity.previewPath);

    if (!imagesOk || imageCount == 0 || !metadataOk || metadata.empty())
    {
        SPDLOG_ERROR("Recording check failed: missing readable image/metadata datasets");
        return 2;
    }
    if (!identity.readable || !identity.identical)
    {
        SPDLOG_ERROR("Recording check failed: input and saved frames are not bit-identical");
        return 3;
    }
    return 0;
}

int checkCheckpoint(const std::string &path)
{
    const std::string recoveryPath = path + ".recovery.h5";
    if (!isNonEmptyFile(recoveryPath))
    {
        SPDLOG_ERROR("Recovery checkpoint missing or empty: {}", recoveryPath);
        return 1;
    }
    SPDLOG_INFO("Recovery checkpoint is non-empty: {}", recoveryPath);
    return 0;
}

int runMode(const std::string &mode, const std::string &path)
{
    if (mode == "run-experiment")
        return runExperiment(path);
    if (mode == "run-recording")
        return runRecording(path);
    if (mode == "check-experiment")
        return checkExperiment(path);
    if (mode == "check-recording")
        return checkRecording(path);
    if (mode == "check-checkpoint")
        return checkCheckpoint(path);

    SPDLOG_ERROR("Unknown mode: {}", mode);
    return 64;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        SPDLOG_ERROR("usage: hdf5_abrupt_stop_tool <mode> <h5-path>");
        SPDLOG_ERROR("modes: run-experiment | run-recording | check-experiment | check-recording | check-checkpoint");
        return 64;
    }
    return runMode(argv[1], argv[2]);
}
