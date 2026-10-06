#include "benchmark.hpp"

#include "common/detection.hpp"
#include "common/frame.hpp"
#include "common/logger.hpp"
#include "common/track.hpp"

#include "detection/dxapp_detection_pipeline.hpp"
#include "tracking/bytetrack_tracker.hpp"
#include "tracking/tracker.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;


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
    c.path = config_parser::load_benchmark_path_config(root["path"]);

    Logger::info("[Config] Benchmark Config loaded");
    return c;
}


// 기존 결과를 덮어쓰지 않도록 다음 run 디렉터리를 생성한다.
static fs::path create_run_directory(const fs::path& output_root)
{
    fs::create_directories(output_root);

    for (int run_id = 1;; ++run_id)
    {
        std::ostringstream name;
        name << "run_" << std::setw(3) << std::setfill('0') << run_id;

        const fs::path run_dir = output_root / name.str();

        if (!fs::exists(run_dir))
        {
            fs::create_directories(run_dir / "detections");
            fs::create_directories(run_dir / "tracks");
            return run_dir;
        }
    }
}


// Capture의 파일명 000001.png에서 frame_id를 가져온다.
static std::uint64_t get_frame_id(const fs::path& image_path)
{
    try
    {
        return std::stoull(image_path.stem().string());
    }
    catch (...)
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

    std::unique_ptr<DetectionPipeline> detection_pipeline_;
    std::unique_ptr<Tracker> tracker_;

public:
    explicit BenchmarkApplication(const std::string& config_path);
    void run();
};


BenchmarkApplication::BenchmarkApplication(const std::string& config_path)
    : config_(load_benchmark_config(config_path)),
      config_path_(config_path)
{
    detection_pipeline_ = std::make_unique<DxAppDetectionPipeline>(
        config_.model, config_.detection);

    tracker_ = std::make_unique<ByteTrackTracker>(
        config_.tracking);
}


// dataset_root 아래의 모든 Sequence를 순차적으로 처리한다.
void BenchmarkApplication::run()
{
    const fs::path dataset_root = config_.path.dataset_root;
    const fs::path output_root = config_.path.output_root;

    if (!fs::exists(dataset_root))
        throw std::runtime_error(
            "Dataset 경로가 존재하지 않습니다: " + dataset_root.string());

    const fs::path run_dir = create_run_directory(output_root);

    // 실행 당시 사용한 설정을 결과와 함께 보관한다.
    fs::copy_file(
        config_path_,
        run_dir / "benchmark.yaml",
        fs::copy_options::overwrite_existing);

    fs::copy_file(
        config_.model_config_path,
        run_dir / fs::path(config_.model_config_path).filename(),
        fs::copy_options::overwrite_existing);

    std::vector<fs::path> sequence_dirs;

    for (const auto& entry : fs::directory_iterator(dataset_root))
    {
        if (entry.is_directory())
            sequence_dirs.push_back(entry.path());
    }

    std::sort(sequence_dirs.begin(), sequence_dirs.end());

    Logger::info("[Benchmark] Sequence 수: " + std::to_string(sequence_dirs.size()));
    Logger::info("[Benchmark] 결과 경로: " + run_dir.string());

    for (const fs::path& sequence_dir : sequence_dirs)
    {
        const std::string sequence_name = sequence_dir.filename().string();
        const fs::path image_dir = sequence_dir / "images";

        if (!fs::exists(image_dir))
        {
            Logger::warn("[Benchmark] images 디렉터리 없음: " + image_dir.string());
            continue;
        }

        std::vector<fs::path> image_paths;

        for (const auto& entry : fs::directory_iterator(image_dir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".png")
                image_paths.push_back(entry.path());
        }

        std::sort(image_paths.begin(), image_paths.end());

        if (image_paths.empty())
        {
            Logger::warn("[Benchmark] PNG가 없습니다: " + image_dir.string());
            continue;
        }

        std::ofstream detection_file(
            run_dir / "detections" / (sequence_name + ".txt"));

        std::ofstream track_file(
            run_dir / "tracks" / (sequence_name + ".txt"));

        if (!detection_file || !track_file)
            throw std::runtime_error(
                "Benchmark 결과 파일을 생성하지 못했습니다: " + sequence_name);

        detection_file << std::fixed << std::setprecision(6);
        track_file << std::fixed << std::setprecision(6);

        // Sequence 사이에서 Track 상태가 이어지지 않도록 초기화한다.
        tracker_->reset();

        Logger::info("[Benchmark] Sequence 시작: " + sequence_name);

        std::size_t processed_frames = 0;

        for (const fs::path& image_path : image_paths)
        {
            FrameContext frame;
            frame.image = cv::imread(image_path.string(), cv::IMREAD_UNCHANGED);

            if (frame.image.empty())
            {
                Logger::warn("[Benchmark] 이미지 읽기 실패: " + image_path.string());
                continue;
            }

            const std::uint64_t frame_id = get_frame_id(image_path);
            frame.metadata.frame_id = frame_id;

            const std::vector<Detection> detections =
                detection_pipeline_->detect(frame);

            write_detections(
                detection_file,
                frame_id,
                detections);

            const std::vector<Track> tracks =
                tracker_->track(detections);

            write_tracks(
                track_file,
                frame_id,
                tracks);

            ++processed_frames;

            if (processed_frames % 30 == 0)
                Logger::info(
                    "[Benchmark] " + sequence_name +
                    " 처리 프레임: " + std::to_string(processed_frames));
        }

        Logger::info(
            "[Benchmark] Sequence 완료: " + sequence_name +
            " / 처리 프레임: " + std::to_string(processed_frames));
    }

    Logger::info("[Benchmark] 전체 Benchmark 완료");
}


int run_benchmark(const std::string& config_path)
{
    BenchmarkApplication app(config_path);
    app.run();
    return 0;
}