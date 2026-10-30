#include <string>

#include "benchmark.hpp"

int main(int argc, char* argv[])
{
    // 인자가 없으면 팀 공용 기준 설정을 사용한다. 개인 실험 설정은
    // config/local/에 복사한 뒤 첫 번째 인자로 전달할 수 있다.
    const std::string config_path =
        argc > 1 ? argv[1] : "config/benchmark.yaml";

    return run_benchmark(config_path);
}
