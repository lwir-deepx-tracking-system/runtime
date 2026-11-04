#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "common/stage_metric.hpp"
#include "common/detection_timing.hpp"

// 실행 결과 디렉터리와 단계별 측정 CSV의 생성·저장을 맡는다.
class RuntimeMetricsWriter {
public:
    // 모델·트래커·실행 모드별로 충돌 없는 결과 디렉터리를 만든다.
    static std::filesystem::path create_directory(
        const std::filesystem::path& output_root,
        const std::string& model_name, const std::string& tracker_name,
        const std::string& runtime_mode);
    // 종료된 worker의 단계별 시간과 검출 내부 시간을 CSV로 저장한다.
    static void write(const std::filesystem::path& result_dir,
        const std::vector<StageMetric>& camera,
        const std::vector<StageMetric>& detection,
        const std::vector<StageMetric>& tracking,
        const std::vector<StageMetric>* control,
        std::vector<DetectionTiming> timings);
};
