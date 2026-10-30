// RAW16 replay for codec comparison. No camera/NPU/inference results are simulated.
#include <algorithm>
#include <chrono>
#include <vector>
#include <opencv2/imgcodecs.hpp>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include "gui/gui_sender.hpp"

namespace {
double cpu_seconds()
{
    timespec ts{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
}

int main(int argc, char** argv)
try
{
    GuiVideoConfig video;
    video.host = "127.0.0.1";
    video.port = 5000;
    video.codec = GuiVideoCodec::JPEG;
    video.encoder = "mppjpegenc";
    video.bitrate_kbps = 3000;
    video.fps = 30;
    video.rtp_mtu = 1200;
    int seconds = 30;
    int clip_min = 0, clip_max = 65535;
    std::string codec = "jpeg", input, output;
    bool synthetic = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help")
        {
            std::cout << "lwir_video_send --input frames.raw [--codec jpeg|h264] [--host IP] [--port 5000]\n"
                         "  [--seconds 30] [--quality 80] [--bitrate 3000] [--gop 30] [--output result.csv] [--clip-min 0] [--clip-max 65535]\n"
                         "Input: RAW16 file or directory of 640x480 16-bit PNG frames; loops at EOF, 30 Hz.\n"
                         "--synthetic uses a moving gradient ONLY for smoke testing, not codec selection.\n";
            return 0;
        }
        if (arg == "--synthetic") { synthetic = true; continue; }
        if (++i >= argc) throw std::runtime_error("Missing value for " + arg);
        const std::string value = argv[i];
        if (arg == "--input") input = value;
        else if (arg == "--output") output = value;
        else if (arg == "--codec") codec = value;
        else if (arg == "--host") video.host = value;
        else
        {
            std::size_t used = 0;
            const int number = std::stoi(value, &used);
            if (used != value.size()) throw std::runtime_error("Invalid number: " + value);
            if (arg == "--port" && number > 0 && number <= 65535) video.port = number;
            else if (arg == "--seconds" && number > 0 && number <= 3600) seconds = number;
            else if (arg == "--quality" && number >= 1 && number <= 99) video.jpeg_quality = number;
            else if (arg == "--bitrate" && number > 0 && number <= 2147483) video.bitrate_kbps = number;
            else if (arg == "--clip-min" && number >= 0 && number <= 65535) clip_min = number;
            else if (arg == "--clip-max" && number >= 0 && number <= 65535) clip_max = number;
            else if (arg == "--gop" && number > 0) video.keyframe_interval = number;
            else throw std::runtime_error("Invalid option/value: " + arg + " " + value);
        }
    }
    if (codec != "jpeg" && codec != "h264") throw std::runtime_error("Codec must be jpeg or h264");
    if (synthetic == !input.empty()) throw std::runtime_error("Choose exactly one: --input or --synthetic");
    video.codec = codec == "jpeg" ? GuiVideoCodec::JPEG : GuiVideoCodec::H264;
    video.encoder = codec == "jpeg" ? "mppjpegenc" : "mpph264enc";
    if (clip_min >= clip_max) throw std::runtime_error("clip-min must be less than clip-max");
    constexpr std::size_t frame_bytes = 640 * 480 * sizeof(std::uint16_t);
    std::vector<std::filesystem::path> images;
    const bool directory = !synthetic && std::filesystem::is_directory(input);
    std::ifstream raw;
    if (directory)
    {
        for (const auto& entry : std::filesystem::directory_iterator(input))
            if (entry.is_regular_file() && entry.path().extension() == ".png") images.push_back(entry.path());
        std::sort(images.begin(), images.end());
        if (images.empty()) throw std::runtime_error("No .png frames in input directory");
    }
    else if (!synthetic)
    {
        raw.open(input, std::ios::binary | std::ios::ate);
        if (!raw || raw.tellg() <= 0 || std::uint64_t(raw.tellg()) % frame_bytes)
            throw std::runtime_error("Input must contain complete 640x480 RAW16 frames");
        raw.seekg(0);
    }
    std::ofstream csv;
    if (!output.empty())
    {
        if (!input.empty() && std::filesystem::weakly_canonical(output) == std::filesystem::weakly_canonical(input)) throw std::runtime_error("Output must differ from input");
        for (const auto& image : images)
            if (std::filesystem::weakly_canonical(output) == std::filesystem::weakly_canonical(image))
                throw std::runtime_error("Output must not overwrite an input frame");
        csv.open(output);
        if (!csv) throw std::runtime_error("Cannot open output");
    }
    GuiSender sender(video, GuiMetadataConfig{}, clip_min, clip_max);
    const auto started = std::chrono::steady_clock::now();
    const auto cpu_started = cpu_seconds();
    for (std::uint64_t i = 0; i < std::uint64_t(seconds) * 30; ++i)
    {
        std::this_thread::sleep_until(started + std::chrono::nanoseconds(i * 1000000000ULL / 30));
        auto frame = std::make_shared<FrameContext>();
        frame->image.create(480, 640, CV_16UC1);
        frame->metadata.frame_id = i;
        frame->metadata.captured_at = std::chrono::steady_clock::now();
        if (synthetic)
        {
            for (int y = 0; y < 480; ++y)
                for (int x = 0; x < 640; ++x)
                    frame->image.at<std::uint16_t>(y, x) = 8000 + ((x + y + i * 4) % 640) * 25;
        }
        else if (directory)
        {
            frame->image = cv::imread(images[i % images.size()].string(), cv::IMREAD_UNCHANGED);
            if (frame->image.type() != CV_16UC1 || frame->image.size() != cv::Size(640, 480))
                throw std::runtime_error("PNG input must be 640x480 CV_16UC1");
        }
        else
        {
            if (raw.peek() == std::char_traits<char>::eof()) { raw.clear(); raw.seekg(0); }
            if (!raw.read(reinterpret_cast<char*>(frame->image.data), frame_bytes))
                throw std::runtime_error("RAW16 read failed");
        }
        TrackingResult result;
        result.frame = frame;
        if (!sender.send(result)) throw std::runtime_error("Video send failed");
    }
    if (!sender.finish()) throw std::runtime_error("Pipeline drain failed");
    std::this_thread::sleep_until(started + std::chrono::seconds(seconds));
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double cpu = (cpu_seconds() - cpu_started) * 100 / elapsed;
    const auto stats = sender.stats();
    if (!stats.encoded_frames || !stats.processing_samples) throw std::runtime_error("No encoded frames/timings");
    const auto report = [&](std::ostream& out) {
        out << "codec,encoder,input,quality,bitrate_kbps,gop,clip_min,clip_max,seconds,submitted,encoded,tx_fps,cpu_percent_one_core,rtp_mbps,preprocess_ms,encode_rtp_ms,timing_samples\n";
        out << std::fixed << std::setprecision(3) << codec << ',' << video.encoder << ','
            << (synthetic ? "synthetic" : directory ? "png16" : "raw16") << ',' << video.jpeg_quality << ',' << video.bitrate_kbps
            << ',' << video.keyframe_interval << ',' << clip_min << ',' << clip_max << ',' << elapsed << ',' << stats.submitted_frames << ',' << stats.encoded_frames
            << ',' << stats.encoded_frames / elapsed << ',' << cpu << ',' << stats.rtp_bytes * 8 / elapsed / 1e6
            << ',' << double(stats.preprocessing_ns) / stats.submitted_frames / 1e6
            << ',' << double(stats.encode_rtp_ns) / stats.processing_samples / 1e6 << ',' << stats.processing_samples << '\n';
    };
    report(std::cout);
    if (csv) report(csv);
    sender.stop();
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
