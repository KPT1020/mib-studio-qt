#pragma once

#include <cstdint>
#include <string>

namespace bridge
{

enum class BackendCommandType
{
    StartCamera,
    StopCamera,
    StartRecording,
    StopRecording,
    UpdateProcessingSettings,
    LoadRecording,
    SeekPlayback
};

enum class BackendEventType
{
    FrameReady,
    CameraStatusChanged,
    RecordingStatusChanged,
    ProcessingResultReady,
    PlaybackPositionChanged,
    Error
};

struct BackendEvent
{
    BackendEventType type{BackendEventType::Error};
    std::uint64_t frameIndex{0};
    std::uint64_t timestampNs{0};
    std::string payloadJson;
};

} // namespace bridge
