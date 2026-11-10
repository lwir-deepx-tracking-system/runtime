#include "app/run_directory.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

std::filesystem::path create_run_directory(const std::filesystem::path &root)
{
    std::filesystem::create_directories(root);
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm    local{};
    // C++17에는 thread-safe localtime이 없어 이 경계에서 플랫폼 함수를 사용한다.
    if(!localtime_r(&now, &local)) throw std::runtime_error("Run timestamp 생성 실패");
    std::ostringstream timestamp;
    timestamp << std::put_time(&local, "%Y%m%d_%H%M%S");
    for(unsigned suffix = 0;; ++suffix)
    {
        auto path = root / (timestamp.str() + (suffix ? "_" + std::to_string(suffix) : ""));
        if(std::filesystem::create_directory(path)) return path;
    }
}
