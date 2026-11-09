#include <string>
#include <iostream>

#include "runtime.hpp"


int main(int argc, char* argv[])
{
    // 인자가 없으면 팀 공용 기준 설정을 사용한다. 개인 실험 설정은
    // config/local/에 복사한 뒤 첫 번째 인자로 전달할 수 있다.
    std::string config_path = "config/runtime.yaml";
    bool console_enabled = false, has_config = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--console") console_enabled = true;
        else if (arg == "--help") {
            std::cout << "Usage: lwir_runtime [config.yaml] [--console]\n";
            return 0;
        } else if (arg.empty() || arg[0] == '-' || has_config) {
            std::cerr << "Usage: lwir_runtime [config.yaml] [--console]\n";
            return 1;
        } else { config_path = arg; has_config = true; }
    }
    return run_runtime(config_path, console_enabled);
}
