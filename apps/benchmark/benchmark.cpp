#include "benchmark.hpp"
#include "result_writer.hpp"

#include "common/detection.hpp"
#include "common/frame.hpp"
#include "common/logger.hpp"
#include "common/track.hpp"

#include "detection/detector_factory.hpp"
#include "tracking/bytetrack_tracker.hpp"
#include "tracking/tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace {
// 현재 직접 생성하는 ByteTrack 구현의 결과 이름이다.
// tracker 선택 기능이 생기면 선택값으로 교체한다.
constexpr const char* kTrackerName = "bytetrack";
}  // namespace


// benchmark.yaml과 model YAML을 읽어 Benchmark 설정을 구성한다.
BenchmarkConfig load_benchmark_config(const std::string& path)
{
    const YAML::Node root = config_parser::load_yaml(path);

    BenchmarkConfig c;
    c.logging = config_parser::load_logging_config(root["logging"]);
    Logger::set_level(c.logging.level);

    c.model_config_path = config_parser::required_string(
        root["model"], "config", "model");

    if (c.model_config_path.empty())
        throw std::runtime_error("model.config must not be empty");

    c.model = load_model_config(c.model_config_path);
    c.detection = config_parser::load_detection_config(root["detection"]);
    c.tracking = config_parser::load_tracking_config(root["tracking"]);
    c.sequence = config_parser::load_benchmark_sequence_config(root["sequence"]);
    c.path = config_parser::load_benchmark_path_config(root["path"]);

    Logger::info("[Config] Benchmark Config loaded");
    return c;
}


// model/tracker 디렉터리 아래에 timestamp 단위 실행 디렉터리를 만든다.
static fs::path create_run_directory(
    const fs::path& output_root,
    const std::string& model_name,
    const std::string& tracker_name)
{
    const fs::path model_tracker_dir =
        output_root / (model_name + "_" + tracker_name);
    fs::create_directories(model_tracker_dir);

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};

    if (localtime_r(&now_time, &local_time) == nullptr)
        throw std::runtime_error("Benchmark timestamp 생성에 실패했습니다");

    std::ostringstream timestamp_stream;
    timestamp_stream << std::put_time(&local_time, "%Y%m%d_%H%M%S");

    if (!timestamp_stream)
        throw std::runtime_error("Benchmark timestamp 생성에 실패했습니다");

    const std::string timestamp = timestamp_stream.str();

    // 같은 초의 실행 결과가 있으면 _1, _2 순서로 충돌을 피한다.
    for (std::uint64_t suffix = 0;; ++suffix)
    {
        const std::string directory_name = timestamp +
            (suffix == 0 ? "" : "_" + std::to_string(suffix));
        const fs::path run_dir = model_tracker_dir / directory_name;

        if (fs::create_directory(run_dir))
        {
            try
            {
                if (!fs::create_directory(run_dir / "detections") ||
                    !fs::create_directory(run_dir / "tracks") ||
                    !fs::create_directory(run_dir / "timing"))
                {
                    throw std::runtime_error(
                        "Benchmark 결과 디렉터리를 생성하지 못했습니다: " +
                        run_dir.string());
                }
                return run_dir;
            }
            catch (...)
            {
                std::error_code cleanup_error;
                fs::remove_all(run_dir, cleanup_error);
                throw;
            }
        }
    }
}


// Capture의 파일명 000001.png에서 frame_id를 가져온다.
static std::uint64_t get_frame_id(const fs::path& image_path)
{
    const std::string stem = image_path.stem().string();

    if (stem.empty() || !std::all_of(stem.begin(), stem.end(), [](char c) {
            return c >= '0' && c <= '9';
        }))
    {
        throw std::runtime_error(
            "올바르지 않은 Frame 파일명입니다: " + image_path.filename().string());
    }

    try
    {
        const std::uint64_t frame_id = std::stoull(stem);

        // Capture의 최소 6자리 zero-padding 규칙과 정확히 일치해야 한다.
        std::ostringstream expected_filename;
        expected_filename << std::setw(6) << std::setfill('0')
                          << frame_id << ".png";

        if (image_path.filename().string() != expected_filename.str())
            throw std::runtime_error(
                "올바르지 않은 Frame 파일명입니다: " + image_path.filename().string());

        return frame_id;
    }
    catch (const std::invalid_argument&)
    {
        throw std::runtime_error(
            "올바르지 않은 Frame 파일명입니다: " + image_path.filename().string());
    }
    catch (const std::out_of_range&)
    {
        throw std::runtime_error(
            "올바르지 않은 Frame 파일명입니다: " + image_path.filename().string());
    }
}


// Detection prediction을 Detection 평가용 형식으로 저장한다.
static void write_detections(
    std::ofstream& file,
    std::uint64_t frame_id,
    const std::vector<Detection>& detections)
{
    for (const Detection& detection : detections)
    {
        file << frame_id << ','
             << detection.class_id << ','
             << detection.confidence << ','
             << detection.x << ','
             << detection.y << ','
             << detection.width << ','
             << detection.height << '\n';
    }
}


// Tracking prediction을 MOTChallenge / TrackEval 형식으로 저장한다.
static void write_tracks(
    std::ofstream& file,
    std::uint64_t frame_id,
    const std::vector<Track>& tracks)
{
    for (const Track& track : tracks)
    {
        file << frame_id << ','
             << track.track_id << ','
             << track.x << ','
             << track.y << ','
             << track.width << ','
             << track.height << ','
             << track.confidence
             << ",-1,-1,-1\n";
    }
}


// 저장된 데이터셋으로 Detection과 Tracking prediction을 생성한다.
class BenchmarkApplication
{
private:
    BenchmarkConfig config_;
    std::string config_path_;

    std::unique_ptr<IDetectionPipeline> detection_pipeline_;
    std::unique_ptr<Tracker> tracker_;

public:
    explicit BenchmarkApplication(const std::string& config_path);
    void run();
};


BenchmarkApplication::BenchmarkApplication(const std::string& config_path)
    : config_(load_benchmark_config(config_path)),
      config_path_(config_path)
{
    auto detector_factory = DetectorFactory::for_model(config_.model);
    detection_pipeline_ = detector_factory->create(
        config_.model, config_.detection);

    tracker_ = std::make_unique<ByteTrackTracker>(
        config_.tracking);
}


// dataset_root 아래에서 설정 범위에 해당하는 Sequence를 순차적으로 처리한다.
void BenchmarkApplication::run()
{
    const fs::path dataset_root = config_.path.dataset_root;
    const fs::path output_root = config_.path.output_root;

    if (!fs::is_directory(dataset_root))
        throw std::runtime_error(
            "Dataset 경로가 올바른 디렉터리가 아닙니다: " + dataset_root.string());

    std::vector<fs::path> sequence_dirs;

    for (const auto& entry : fs::directory_iterator(dataset_root))
    {
        if (entry.is_directory())
            sequence_dirs.push_back(entry.path());
    }

    std::sort(sequence_dirs.begin(), sequence_dirs.end());

    if (sequence_dirs.empty())
        throw std::runtime_error(
            "Dataset에 처리할 Sequence가 없습니다: " + dataset_root.string());

    if (config_.sequence.range_enabled)
    {
        sequence_dirs.erase(
            std::remove_if(
                sequence_dirs.begin(),
                sequence_dirs.end(),
                [this](const fs::path& sequence_dir)
                {
                    const std::string id = sequence_dir.filename().string();
                    return !config_.sequence.contains(id);
                }),
            sequence_dirs.end());

        if (sequence_dirs.empty())
            throw std::runtime_error(
                "설정한 범위에 처리할 Sequence가 없습니다: " +
                config_.sequence.start + " ~ " + config_.sequence.end);
    }

    const fs::path run_dir = create_run_directory(
        output_root, config_.model.name, kTrackerName);

    // run 디렉터리 생성 이후의 모든 실패는 불완전한 결과를 통째로 정리한다.
    try
    {
        // 실행 당시 사용한 설정을 결과와 함께 보관한다.
        fs::copy_file(config_path_, run_dir / "benchmark.yaml");
        fs::copy_file(
            config_.model_config_path,
            run_dir / fs::path(config_.model_config_path).filename());

        Logger::info("[Benchmark] Sequence 수: " + std::to_string(sequence_dirs.size()));
        if (config_.sequence.range_enabled)
            Logger::info(
                "[Benchmark] Sequence 범위: " + config_.sequence.start +
                " ~ " + config_.sequence.end);
        Logger::info("[Benchmark] 결과 경로: " + run_dir.string());

        for (const fs::path& sequence_dir : sequence_dirs)
        {
            const std::string sequence_name = sequence_dir.filename().string();
            const fs::path image_dir = sequence_dir / "images";

            if (!fs::is_directory(image_dir))
                throw std::runtime_error(
                    "images 디렉터리가 없습니다: " + image_dir.string());

            std::vector<std::pair<std::uint64_t, fs::path>> frames;

            for (const auto& entry : fs::directory_iterator(image_dir))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".png")
                    frames.emplace_back(get_frame_id(entry.path()), entry.path());
            }

            if (frames.empty())
                throw std::runtime_error(
                    "PNG가 없습니다: " + image_dir.string());

            std::sort(frames.begin(), frames.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });

            // 각 Sequence는 frame 1부터 누락이나 중복 없이 이어져야 한다.
            for (std::size_t index = 0; index < frames.size(); ++index)
            {
                const std::uint64_t expected_id = index + 1;
                const std::uint64_t actual_id = frames[index].first;

                if (index > 0 && actual_id == frames[index - 1].first)
                    throw std::runtime_error(
                        "중복된 Frame ID입니다: " + sequence_name + "/" +
                        std::to_string(actual_id));

                if (actual_id != expected_id)
                    throw std::runtime_error(
                        "Frame ID가 연속적이지 않습니다: " + sequence_name +
                        " (expected=" + std::to_string(expected_id) +
                        ", actual=" + std::to_string(actual_id) + ")");
            }

            std::ofstream detection_file(
                run_dir / "detections" / (sequence_name + ".txt"));

            std::ofstream track_file(
                run_dir / "tracks" / (sequence_name + ".txt"));

            std::ofstream timing_file(
                run_dir / "timing" / (sequence_name + ".csv"));

            if (!detection_file || !track_file || !timing_file)
                throw std::runtime_error(
                    "Benchmark 결과 파일을 생성하지 못했습니다: " + sequence_name);

            detection_file << std::fixed << std::setprecision(6);
            track_file << std::fixed << std::setprecision(6);
            benchmark_result_writer::write_timing_header(timing_file);

            if (!detection_file || !track_file || !timing_file)
                throw std::runtime_error(
                    "Benchmark 결과 파일 초기화에 실패했습니다: " + sequence_name);

            // Sequence 사이에서 Track 상태가 이어지지 않도록 초기화한다.
            tracker_->reset();

            Logger::info("[Benchmark] Sequence 시작: " + sequence_name);

            std::size_t processed_frames = 0;
            std::vector<benchmark_result_writer::DxrtProfilerSample>
                profiler_samples;
            profiler_samples.reserve(frames.size());

            for (const auto& frame_entry : frames)
            {
                const std::uint64_t frame_id = frame_entry.first;
                const fs::path& image_path = frame_entry.second;

                auto mutable_frame = std::make_shared<FrameContext>();
                mutable_frame->image =
                    cv::imread(image_path.string(), cv::IMREAD_UNCHANGED);

                if (mutable_frame->image.empty())
                    throw std::runtime_error(
                        "이미지 읽기에 실패했습니다: " + image_path.string());

                mutable_frame->metadata.frame_id = frame_id;
                FrameContextPtr frame = mutable_frame;

                // Runtime과 동일한 비동기 Detection 경로를 실행하고 이 프레임의
                // Completion이 전달하는 결과와 단계별 시간을 기다린다.
                using DetectionResult =
                    std::pair<std::vector<Detection>, DetectionTiming>;
                std::promise<DetectionResult> completion_promise;
                std::future<DetectionResult> completion_future =
                    completion_promise.get_future();
                detection_pipeline_->submit_async(
                    frame,
                    [&completion_promise](
                        std::vector<Detection> completed_detections,
                        std::exception_ptr error,
                        DetectionTiming timing)
                    {
                        if (error)
                            completion_promise.set_exception(error);
                        else
                            completion_promise.set_value(
                                DetectionResult{
                                    std::move(completed_detections), timing});
                    });

                DetectionResult detection_result = completion_future.get();
                std::vector<Detection> detections =
                    std::move(detection_result.first);
                const DetectionTiming& detection_timing = detection_result.second;
                if (detection_timing.frame_id != frame_id)
                    throw std::runtime_error(
                        "Detection timing Frame ID가 일치하지 않습니다: " +
                        sequence_name + "/" + std::to_string(frame_id));

                if (!detection_timing.npu_profiler_valid)
                    throw std::runtime_error(
                        "DXRT profiler timing이 유효하지 않습니다: " +
                        sequence_name + "/" + std::to_string(frame_id));

                profiler_samples.push_back({
                    detection_timing.preprocess_ms() * 1000.0,
                    detection_timing.npu_input_format_duration_us,
                    detection_timing.h2d_duration_us,
                    detection_timing.npu_processing_ms() * 1000.0,
                    detection_timing.d2h_duration_us,
                    detection_timing.npu_output_format_duration_us,
                    detection_timing.npu_task_total_duration_us,
                    detection_timing.postprocess_ms() * 1000.0,
                    0.0,
                    detection_timing.latency_ms() * 1000.0});
                for (const auto& device : detection_timing.npu_devices)
                    profiler_samples.back().npu_devices.push_back({
                        device.task_name, device.device_id,
                        device.input_format_us, device.h2d_us,
                        device.inference_core_all_us,
                        device.inference_core_0_us,
                        device.inference_core_1_us,
                        device.inference_core_2_us,
                        device.d2h_us, device.output_format_us,
                        device.total_us});
                if (detection_timing.cpu_task_valid)
                    profiler_samples.back().cpu_task_us =
                        detection_timing.cpu_task_us;

                write_detections(detection_file, frame_id, detections);
                if (!detection_file)
                    throw std::runtime_error(
                        "Detection 결과 저장에 실패했습니다: " + sequence_name);

                // 결과 저장 시간을 제외하고 track() 호출 자체만 측정한다.
                const auto tracking_started_at = std::chrono::steady_clock::now();
                const std::vector<Track> tracks = tracker_->track(detections);
                const auto tracking_finished_at = std::chrono::steady_clock::now();
                const double tracking_ms = std::chrono::duration<double, std::milli>(
                    tracking_finished_at - tracking_started_at).count();
                profiler_samples.back().tracking_us = tracking_ms * 1000.0;

                write_tracks(track_file, frame_id, tracks);
                if (!track_file)
                    throw std::runtime_error(
                        "Tracking 결과 저장에 실패했습니다: " + sequence_name);

                benchmark_result_writer::write_timing_row(
                    timing_file,
                    frame_id,
                    detection_timing.preprocess_ms(),
                    detection_timing.latency_ms(),
                    detection_timing.npu_processing_ms(),
                    detection_timing.postprocess_ms(),
                    tracking_ms);
                if (!timing_file)
                    throw std::runtime_error(
                        "Timing 결과 저장에 실패했습니다: " + sequence_name);

                ++processed_frames;

                if (processed_frames % 30 == 0)
                    Logger::info(
                        "[Benchmark] " + sequence_name +
                        " 처리 프레임: " + std::to_string(processed_frames));
            }

            // close 시점의 flush 오류까지 확인해 불완전한 파일을 성공 처리하지 않는다.
            benchmark_result_writer::write_timing_footer(timing_file);
            detection_file.close();
            track_file.close();
            timing_file.close();

            if (detection_file.fail() || track_file.fail() || timing_file.fail())
                throw std::runtime_error(
                    "Benchmark 결과 파일 마무리에 실패했습니다: " + sequence_name);

            const fs::path summary_path = run_dir / "timing" /
                (sequence_name + "_profiler_summary.txt");
            benchmark_result_writer::write_dxrt_profiler_summary_outputs(
                std::cout, summary_path, sequence_name, processed_frames,
                profiler_samples);

            Logger::info(
                "[Benchmark] Sequence 완료: " + sequence_name +
                " / 처리 프레임: " + std::to_string(processed_frames));
        }

        Logger::info("[Benchmark] 전체 Benchmark 완료");
    }
    catch (...)
    {
        std::error_code cleanup_error;
        fs::remove_all(run_dir, cleanup_error);

        if (cleanup_error)
            Logger::error(
                "[Benchmark] 실패 결과 정리에 실패했습니다: " +
                run_dir.string() + " (" + cleanup_error.message() + ")");
        else
            Logger::info("[Benchmark] 실패 결과 정리: " + run_dir.string());

        throw;
    }
}


int run_benchmark(const std::string& config_path)
{
    BenchmarkApplication app(config_path);
    app.run();
    return 0;
}
