#pragma once
#include "app/raw_recorder.hpp"
#include "app/pipeline_requests.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

// Reads completed recordings from the same root used by RawRecorder.
class RecordingArchive {
    const RawRecorder& recorder_;
    std::filesystem::path root_;
public:
    RecordingArchive(const RawRecorder& recorder, std::filesystem::path root)
        : recorder_(recorder), root_(std::move(root)) {}

    std::vector<std::string> list_recordings() const
    {
        const auto root = root_;
        std::vector<std::string> names;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(root)) {
                if (!entry.is_directory()) continue;
                const auto name = entry.path().filename().string();
                if (name.size() < 10 || name.size() > 64 || name.rfind("MEASURE_", 0) != 0 ||
                    !std::all_of(name.begin() + 8, name.end(), [](unsigned char c) {
                        return (c >= '0' && c <= '9') || c == '_'; })) continue;
                if (recorder_.status().active &&
                    recorder_.directory().filename() == entry.path().filename()) continue;
                names.push_back(name);
            }
        } catch (const std::filesystem::filesystem_error&) {}
        std::sort(names.begin(), names.end(), std::greater<>());
        if (names.size() > 32) names.resize(32);
        return names;
    }

    PipelineRecordingFileChunk read_recording_file(
        const std::string& name, std::uint8_t file_id, std::uint64_t offset,
        std::uint16_t limit) const
    {
        if (name.size() < 10 || name.size() > 64 || name.rfind("MEASURE_", 0) != 0 ||
            !std::all_of(name.begin() + 8, name.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || c == '_'; }) ||
            !limit || limit > 32768 || file_id < 1 || file_id > 4 ||
            offset > std::uint64_t(std::numeric_limits<std::streamoff>::max())) return {};
        if (recorder_.status().active && recorder_.directory().filename() == name)
            return {};
        static const char* files[] = {"", "frames.raw.gz", "frames.csv", "tracks.csv", "recording.txt"};
        const auto path = root_ / name / files[file_id];
        try {
            if (!std::filesystem::is_regular_file(path)) return {};
            const auto total = std::filesystem::file_size(path);
            if (offset > total) return {};
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return {};
            stream.seekg(static_cast<std::streamoff>(offset));
            std::vector<std::uint8_t> data(static_cast<std::size_t>(
                std::min<std::uint64_t>(limit, total - offset)));
            if (!data.empty()) stream.read(reinterpret_cast<char*>(data.data()), data.size());
            if (!stream || static_cast<std::uint64_t>(stream.gcount()) != data.size()) return {};
            return {true, total, std::move(data)};
        } catch (const std::filesystem::filesystem_error&) { return {}; }
    }

};
