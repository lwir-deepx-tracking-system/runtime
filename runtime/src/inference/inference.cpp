#include "inference/inference.hpp"

#include <string>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "common/logger.hpp"


Inference::Inference(std::string config_path)
    : config_path_(std::move(config_path))
{
    load_config();

    Logger::info("[Inference] 생성 완료");
}


// Model YAML에서 모델 경로를 읽고 DEEPX 모델을 초기화한다.
void Inference::load_config()
{
    const YAML::Node config = YAML::LoadFile(config_path_);

    model_path = config["model"]["path"].as<std::string>();

    // TODO:
    // model_path를 이용해 DX-RT 모델을 한 번 로드
    // 실제 DEEPX 모델 객체는 Inference의 member로 보관
}


// ModelInput을 NPU에 전달하고 Raw Output Tensor를 반환한다.
ModelOutput Inference::run(const ModelInput& input)
{
    (void)input;

    ModelOutput output;

    // TODO:
    // 1. ModelInput을 DX-RT 입력 Tensor에 연결
    // 2. NPU inference 실행
    // 3. Raw Output Tensor 획득
    // 4. ModelOutput 구성

    return output;
}
