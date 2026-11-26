#pragma once
#include "app/raw_recorder.hpp"
#include "app/pipeline_requests.hpp"
#include <filesystem>

// Reads completed recordings from the same root used by RawRecorder.
class RecordingArchive {
    const RawRecorder& recorder_;
    std::filesystem::path root_;
public:
    RecordingArchive(const RawRecorder& recorder, std::filesystem::path root);

    std::vector<std::string> list_recordings() const;

    PipelineRecordingFileChunk read_recording_file(
        const std::string& name, std::uint8_t file_id, std::uint64_t offset,
        std::uint16_t limit) const;
};
