#include "app/app_config.hpp"

int main()
{
    const AppConfig config = load_config("config/runtime.yaml");
    if (config.detection.backend != "dx_app_async" || config.detection.max_inflight != 4) return 1;
    if (config.model.camera_input.opencv_type != "CV_16UC1") return 2;
    if (config.model.camera_input.width != 640 || config.model.camera_input.height != 480) return 3;
    if (config.model.input.width != 640 || config.model.input.height != 640) return 4;
    if (config.model.preprocess.pad_value != 114 || config.model.postprocess.num_classes != 1) return 5;
    return 0;
}
