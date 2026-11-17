#include "app/raw_recorder.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <zlib.h>

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("lwir-raw-recorder-test-" + std::to_string(getpid()));
    RawRecorder recorder(root);
    if (!recorder.start("roundtrip")) return 1;
    for (int n = 0; n < 3; ++n) {
        auto frame = std::make_shared<FrameContext>();
        frame->image = cv::Mat(480, 640, CV_16UC1, cv::Scalar(1234 + n)).clone();
        frame->metadata.frame_id = n + 1;
        frame->metadata.captured_at = std::chrono::steady_clock::now();
        recorder.submit(frame);
        auto result = std::make_shared<TrackingResult>();
        result->frame = frame;
        result->tracks.push_back(Track{n + 10, 1.0F, 2.0F, 3.0F, 4.0F, 0, 0.9F});
        recorder.submit_tracks(result, 2.0);
    }
    if (!recorder.stop()) return 2;
    const auto status = recorder.status();
    if (status.active || status.failed || status.frames != 3 || status.dropped) return 3;
    gzFile file = gzopen((recorder.directory() / "frames.raw.gz").string().c_str(), "rb");
    if (!file) return 4;
    cv::Mat image(480, 640, CV_16UC1);
    for (int n = 0; n < 3; ++n) {
        const int bytes = gzread(file, image.data, 640 * 480 * 2);
        if (bytes != 640 * 480 * 2 || image.at<std::uint16_t>(0, 0) != 1234 + n ||
            image.at<std::uint16_t>(479, 639) != 1234 + n) return 5;
    }
    if (gzread(file, image.data, 1) != 0 || gzclose(file) != Z_OK) return 6;
    if (!std::filesystem::exists(recorder.directory() / "frames.csv")) return 7;
    std::ifstream tracks(recorder.directory() / "tracks.csv");
    std::string header, row;
    if (!std::getline(tracks, header) || !std::getline(tracks, row) ||
        row.find("1,10,0,0.9,1,2,3,4,0") != 0) return 8;
    std::ifstream info(recorder.directory() / "recording.txt");
    std::string content((std::istreambuf_iterator<char>(info)), std::istreambuf_iterator<char>());
    if (content.find("delay_p50=") == std::string::npos ||
        content.find("interval_p95=") == std::string::npos ||
        content.find("tracker_p50=2") == std::string::npos) return 9;
    std::filesystem::remove_all(root);
    return 0;
}
