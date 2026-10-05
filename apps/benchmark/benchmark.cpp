
#include "benchmark.hpp"

#include "common/logger.hpp"
#include "common/stage_metric.hpp"

#include "detection/dxapp_detection_pipeline.hpp"
#include "tracking/bytetrack_tracker.hpp"
#include "tracking/tracker.hpp"

#include "common/detection.hpp"
#include "common/track.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

// runtime 설정을 읽고 별도 모델 YAML까지 한 번에 준비한다.
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

// 종료된 worker의 측정값을 한 CSV에 모은다.
static void append_metrics(
    std::ofstream& file,
    const char* stage,
    const std::vector<StageMetric>& metrics)
{
    for (const StageMetric& metric : metrics)
    {
        file << "integrated," << metric.frame_id << ',' << stage << ',';
        if (metric.queue_wait_ms)
            file << *metric.queue_wait_ms;
        file << ',' << metric.processing_ms << ',';
        if (metric.e2e_ms)
            file << *metric.e2e_ms;
        file << '\n';
    }
}



// 프로그램의 구성 요소를 생성하고
// Pipeline 실행을 관리하는 최상위 Application 클래스
class BenchmarkApplication
{
private:
    // 설정 파일의 경로
    std::string config_path_;
    std::vector<std::string> config_snapshot_paths_;
    bool measurement_enabled_ = false;

    /*
        벤치마크용 어플리케이션으로 탐지와 추적용 객체만 소유
    */
    // DX 전처리, 추론, 후처리를 소유하는 통합 Detection 단계
    std::unique_ptr<DetectionPipeline> detection_pipeline_;

    // 현재 사용하는 Tracker 객체를 Application이 소유한다.
    std::unique_ptr<Tracker> tracker_;

public:
    // YAML 설정을 읽고 필요한 객체를 생성
    explicit BenchmarkApplication(const std::string& config_path);

    // Pipeline Worker 실행
    void run();
};

// YAML을 읽고 설정에 맞는 Component들을 생성한다.
BenchmarkApplication::BenchmarkApplication(const std::string& config_path)
    : config_path_(config_path)
{
    // YAML 로그 레벨을 먼저 적용한 뒤 초기 큐 상태를 출력한다.
    BenchmarkConfig config = load_benchmark_config(config_path);
    
    // 결과 폴더에는 실행 당시 참조한 YAML을 사본으로 남긴다.
    config_snapshot_paths_ = {
        config_path_,
        config.model_config_path
    };

    // 이 런타임의 고정 조합인 YOLOv8 + DX App pipeline을 생성한다.
    detection_pipeline_ = std::make_unique<DxAppDetectionPipeline>(
        config.model, config.detection);

    // AppConfig가 검증한 runtime 설정으로 고정 ByteTrack 구현체를 생성한다.
    tracker_ = std::make_unique<ByteTrackTracker>(config.tracking);
}

// Pipeline Worker를 실행한다.
void BenchmarkApplication::run()
{
    while(true)
    {
        // for loop 통해서 datasets 에 있는 파일들을 순차적으로 측정
    }

}

int run_benchmark(const std::string& config_path)
{
    BenchmarkApplication app(config_path);
    app.run();
    return 0;
}
