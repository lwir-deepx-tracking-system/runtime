#include "app/app_config.hpp"

// 공용 YAML이 runtime에서 지원하는 Detection 및 LWIR 계약으로 파싱되는지 확인한다.
int main()
{
    const AppConfig config = load_config("config/runtime.yaml");

    // runtime.yaml의 Detection 실행 설정 검증.
    if (config.detection.backend != "dx_app_async" || config.detection.max_inflight != 4) return 1;

    // 연결된 모델 YAML의 Camera와 모델 입력 계약 검증.
    if (config.model.camera_input.opencv_type != "CV_16UC1") return 2;
    if (config.model.camera_input.width != 640 || config.model.camera_input.height != 480) return 3;
    if (config.model.input.width != 640 || config.model.input.height != 640) return 4;

    // 전처리 padding과 class 개수가 후처리 설정까지 전달되는지 확인한다.
    if (config.model.preprocess.pad_value != 114 || config.model.postprocess.num_classes != 1) return 5;
    return 0;
}
