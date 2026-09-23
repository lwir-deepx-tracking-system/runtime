#include "app/app_config.hpp"
#include "common/logger.hpp"

#include <stdexcept>
#include <string>

#include <yaml-cpp/yaml.h>


// 필수 string 설정 값을 읽고, 누락되거나 타입이 잘못되면 예외를 발생시킨다.
static std::string require_string(
    const YAML::Node& node,
    const std::string& key,
    const std::string& path)
{
    if (!node || !node[key])
        throw std::runtime_error("설정 항목이 없습니다: " + path + "." + key);

    try
    {
        return node[key].as<std::string>();
    }
    catch (const YAML::Exception&)
    {
        throw std::runtime_error("문자열 설정값이 올바르지 않습니다: " + path + "." + key);
    }
}


// 필수 bool 설정 값을 읽고, 누락되거나 타입이 잘못되면 예외를 발생시킨다.
static bool require_bool(
    const YAML::Node& node,
    const std::string& key,
    const std::string& path)
{
    if (!node || !node[key])
        throw std::runtime_error("설정 항목이 없습니다: " + path + "." + key);

    try
    {
        return node[key].as<bool>();
    }
    catch (const YAML::Exception&)
    {
        throw std::runtime_error("참/거짓 설정값이 올바르지 않습니다: " + path + "." + key);
    }
}


// runtime.yaml을 읽어 전체 실행 설정을 AppConfig로 변환한다.
AppConfig load_config(const std::string& config_path)
{
    YAML::Node root;

    try
    {
        root = YAML::LoadFile(config_path);
    }
    catch (const YAML::Exception& e)
    {
        throw std::runtime_error(
            "설정 파일을 읽지 못했습니다: " + config_path + " / " + e.what()
        );
    }

    AppConfig config;

    config.logging.level =
        require_string(root["logging"], "level", "logging");

    // 이후 설정 및 구성 요소 로그에 YAML의 출력 레벨을 적용한다.
    Logger::set_level(config.logging.level);

    config.detector.type =
        require_string(root["detector"], "type", "detector");

    config.detector.config_path =
        require_string(root["detector"], "config", "detector");

    config.preprocess.backend =
        require_string(root["preprocess"], "backend", "preprocess");

    config.inference.backend =
        require_string(root["inference"], "backend", "inference");

    config.postprocess.backend =
        require_string(root["postprocess"], "backend", "postprocess");

    const YAML::Node tracking = root["tracking"];

    config.tracking.type =
        require_string(tracking, "type", "tracking");

    config.tracking.config_path =
        require_string(tracking, "config", "tracking");

    config.control.enabled =
        require_bool(
            root["control"],
            "enabled",
            "control"
        );

    config.control.config_path =
        require_string(
            root["control"],
            "config",
            "control"
        );

    config.measurement.enabled =
        require_bool(root["measurement"], "enabled", "measurement");

    Logger::info("[Config] runtime 설정을 읽었습니다");

    Logger::debug(
        "[Config] detector.type: " +
        config.detector.type
    );

    Logger::debug(
        "[Config] detector.config: " +
        config.detector.config_path
    );

    Logger::debug(
        "[Config] preprocess.backend: " +
        config.preprocess.backend
    );

    Logger::debug(
        "[Config] inference.backend: " +
        config.inference.backend
    );

    Logger::debug(
        "[Config] postprocess.backend: " +
        config.postprocess.backend
    );

    Logger::debug(
        "[Config] tracking.type: " +
        config.tracking.type
    );

    return config;
}
