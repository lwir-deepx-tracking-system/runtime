#include "config/app_config.hpp"

#include <stdexcept>

#include <yaml-cpp/yaml.h>

int main()
{
    const YAML::Node runtime_root =
        config_parser::load_yaml("config/runtime.yaml");
    const InferenceConfig runtime =
        config_parser::load_inference_config(runtime_root["detection"]["inference"], "detection.inference");
    if (runtime.max_inflight != 4 ||
        runtime.buffer_count != 6 ||
        runtime.use_ort || runtime.npu_cores != NpuCoreBinding::All)
        return 1;

    const YAML::Node benchmark_root =
        config_parser::load_yaml("config/benchmark.yaml");
    const InferenceConfig benchmark =
        config_parser::load_inference_config(benchmark_root["detection"]["inference"], "detection.inference");
    if (benchmark.max_inflight != 1 ||
        benchmark.buffer_count != 6 ||
        benchmark.use_ort)
        return 2;

    const InferenceConfig reid = config_parser::load_inference_config(
        benchmark_root["reid"]["inference"], "reid.inference");
    if (reid.max_inflight != 1 || reid.buffer_count != 0 || reid.use_ort)
        return 8;

    const YAML::Node single_core = YAML::Load(
        "max_inflight: 1\n"
        "buffer_count: 0\n"
        "use_ort: false\n"
        "npu_cores: '1'\n");
    if (config_parser::load_inference_config(single_core, "reid.inference").npu_cores !=
        NpuCoreBinding::Core1)
        return 9;

    const BenchmarkSequenceConfig configured_range =
        config_parser::load_benchmark_sequence_config(
            benchmark_root["sequence"]);
    if (!configured_range.range_enabled ||
        configured_range.start != "seq_001" ||
        configured_range.end != "seq_001")
        return 3;

    const YAML::Node selected_range = YAML::Load(
        "range_enabled: true\n"
        "start: seq_003\n"
        "end: seq_007\n");
    const BenchmarkSequenceConfig selected =
        config_parser::load_benchmark_sequence_config(selected_range);
    if (!selected.range_enabled ||
        selected.start != "seq_003" ||
        selected.end != "seq_007" ||
        selected.contains("seq_002") ||
        !selected.contains("seq_003") ||
        !selected.contains("seq_005") ||
        !selected.contains("seq_007") ||
        selected.contains("seq_008"))
        return 4;

    const YAML::Node reversed_range = YAML::Load(
        "range_enabled: true\n"
        "start: seq_008\n"
        "end: seq_002\n");
    try
    {
        (void)config_parser::load_benchmark_sequence_config(reversed_range);
        return 5;
    }
    catch (const std::runtime_error&)
    {
    }

    // 0은 DXRT 자동 설정이고, 100을 초과한 값은 거부한다.
    const YAML::Node automatic = YAML::Load(
        "max_inflight: 1\n"
        "buffer_count: 0\n"
        "use_ort: false\n");
    if (config_parser::load_inference_config(automatic, "detection.inference").buffer_count != 0)
        return 6;

    const YAML::Node invalid = YAML::Load(
        "max_inflight: 1\n"
        "buffer_count: 101\n"
        "use_ort: false\n");
    try
    {
        (void)config_parser::load_inference_config(invalid, "detection.inference");
        return 7;
    }
    catch (const std::runtime_error&)
    {
    }

    const YAML::Node invalid_core = YAML::Load(
        "max_inflight: 1\n"
        "buffer_count: 0\n"
        "use_ort: false\n"
        "npu_cores: '3'\n");
    try
    {
        (void)config_parser::load_inference_config(invalid_core, "reid.inference");
        return 10;
    }
    catch (const std::runtime_error&) {}

    return 0;
}
