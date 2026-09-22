#include <string>

#include "app/application.hpp"

int main()
{
    // Runtime 설정 파일 경로
    std::string config_path = "config/runtime.yaml";

    // 설정 파일을 기반으로 Application 구성
    Application app(config_path);

    // Pipeline 실행
    app.run();

    return 0;
}