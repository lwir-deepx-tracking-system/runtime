#include <string>

#include "app/application.hpp"

int main(int argc, char* argv[])
{
    // 인자가 없으면 팀 공용 기준 설정을 사용한다. 개인 실험 설정은
    // config/local/에 복사한 뒤 첫 번째 인자로 전달할 수 있다.
    const std::string config_path =
        argc > 1 ? argv[1] : "config/runtime.yaml";

    // 설정 파일을 기반으로 Application 구성
    Application app(config_path);

    // Pipeline 실행
    app.run();

    return 0;
}
