#include "capture.hpp"

#include "camera/camera.hpp"
#include "common/frame.hpp"
#include "common/logger.hpp"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include <opencv2/imgcodecs.hpp>
#include <yaml-cpp/yaml.h>


// Ctrl+C가 입력되면 촬영 루프를 종료한다.
static volatile std::sig_atomic_t capture_running = 1;

static void handle_sigint(int)
{
    capture_running = 0;
}


// 시간을 sequence.yaml에 저장할 문자열로 변환한다.
static std::string format_time(const std::chrono::system_clock::time_point& time)
{
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(time);

    std::tm local_time{};
    localtime_r(&raw_time, &local_time);

    std::ostringstream stream;
    stream << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S");
    return stream.str();
}


// capture.yaml을 읽고 CaptureConfig로 변환한다.
CaptureConfig load_capture_config(const std::string& path)
{
    const YAML::Node root = config_parser::load_yaml(path);

    CaptureConfig c;
    c.logging = config_parser::load_logging_config(root["logging"]);
    Logger::set_level(c.logging.level);

    c.dataset = config_parser::load_capture_dataset_config(root["dataset"]);
    c.sequence = config_parser::load_capture_sequence_config(root["sequence"]);
    c.condition = config_parser::load_capture_condition_config(root["condition"]);

    Logger::info("[Config] Capture Config loaded");
    return c;
}


// 촬영 결과를 sequence.yaml에 기록한다.
static void save_sequence_config(
    const std::filesystem::path& sequence_dir,
    const CaptureConfig& config,
    std::size_t saved_frame_count,
    int width,
    int height,
    const std::chrono::system_clock::time_point& started_at,
    const std::chrono::system_clock::time_point& finished_at)
{
    YAML::Node root;

    root["sequence_id"] = config.sequence.id;

    root["condition"]["distance_m"] = config.condition.distance_m;
    root["condition"]["person_count"] = config.condition.person_count;
    root["condition"]["motion"] = config.condition.motion;
    root["condition"]["temperature_c"] = config.condition.temperature_c;

    // 실제 촬영 결과
    root["capture"]["saved_frame_count"] = saved_frame_count;
    root["capture"]["started_at"] = format_time(started_at);
    root["capture"]["finished_at"] = format_time(finished_at);

    // 실제 저장된 영상 정보
    root["image"]["width"] = width;
    root["image"]["height"] = height;
    root["image"]["type"] = "CV_16UC1";
    root["image"]["format"] = "png";

    std::ofstream file(sequence_dir / "sequence.yaml");
    if (!file)
        throw std::runtime_error("sequence.yaml 파일을 생성하지 못했습니다");

    file << root;
}


// 촬영 프로그램의 실행을 관리한다.
class CaptureApplication
{
private:
    CaptureConfig config_;
    Camera camera_;

public:
    explicit CaptureApplication(const std::string& config_path);
    void run();
};


CaptureApplication::CaptureApplication(const std::string& config_path)
    : config_(load_capture_config(config_path))
{
}


// TE-EV1 프레임을 16-bit PNG로 저장한다.
void CaptureApplication::run()
{
    const std::filesystem::path sequence_dir =
        std::filesystem::path(config_.dataset.root) / config_.sequence.id;

    const std::filesystem::path image_dir = sequence_dir / "images";

    // 기존 Sequence를 실수로 덮어쓰지 않는다.
    if (std::filesystem::exists(sequence_dir))
        throw std::runtime_error("이미 존재하는 Sequence입니다: " + sequence_dir.string());

    std::filesystem::create_directories(image_dir);

    if (!camera_.open())
        throw std::runtime_error("TE-EV1 카메라를 열지 못했습니다");

    std::signal(SIGINT, handle_sigint);

    Logger::info("[Capture] 촬영 시작: " + config_.sequence.id);
    Logger::info("[Capture] 종료하려면 Ctrl+C를 입력하세요");

    const auto started_at = std::chrono::system_clock::now();

    std::size_t saved_frame_count = 0;
    int width = 0;
    int height = 0;

    while (capture_running)
    {
        FrameContext frame;

        if (!camera_.capture(frame))
        {
            continue;
        }

        ++saved_frame_count;

        // 000001.png 형식으로 저장한다.
        std::ostringstream filename;
        filename << std::setw(6) << std::setfill('0') << saved_frame_count << ".png";

        const std::filesystem::path image_path = image_dir / filename.str();

        // Camera에서 받은 CV_16UC1 데이터를 그대로 저장한다.
        if (!cv::imwrite(image_path.string(), frame.image))
            throw std::runtime_error("PNG 저장 실패: " + image_path.string());

        width = frame.image.cols;
        height = frame.image.rows;

        if (saved_frame_count % 30 == 0)
            Logger::info("[Capture] 저장 프레임: " + std::to_string(saved_frame_count));
    }

    const auto finished_at = std::chrono::system_clock::now();

    camera_.close();

    // 실제 촬영 결과를 기반으로 sequence.yaml을 생성한다.
    save_sequence_config(
        sequence_dir,
        config_,
        saved_frame_count,
        width,
        height,
        started_at,
        finished_at
    );

    Logger::info("[Capture] 촬영 종료");
    Logger::info("[Capture] 저장 프레임 수: " + std::to_string(saved_frame_count));
    Logger::info("[Capture] 저장 위치: " + sequence_dir.string());
}


int run_capture(const std::string& config_path)
{
    CaptureApplication app(config_path);
    app.run();
    return 0;
}