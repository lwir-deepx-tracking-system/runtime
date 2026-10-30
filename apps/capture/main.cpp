#include <string>

#include "capture.hpp"

int main(int argc, char* argv[])
{
    // 인자가 없으면 기본 capture 설정을 사용한다.
    const std::string config_path =
        argc > 1 ? argv[1] : "config/capture.yaml";

    return run_capture(config_path);
}