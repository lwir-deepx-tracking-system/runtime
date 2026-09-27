# 독립 저장소에서는 프로젝트 내부의 고정된 의존성을 기본값으로 사용한다.
set(DX_APP_ROOT "${PROJECT_SOURCE_DIR}/third_party/dx_app" CACHE PATH "dx_app 소스 루트")

if(NOT EXISTS "${DX_APP_ROOT}/src/cpp_example/common/base/i_factory.hpp")
    message(FATAL_ERROR
        "dx_app을 찾을 수 없습니다: ${DX_APP_ROOT}\n"
        "third_party/dx_app submodule을 초기화하거나 -DDX_APP_ROOT=<경로>를 지정하세요.")
endif()

# dx_app 공용 헤더가 요구하는 DX-RT SDK를 별도로 확인한다.
find_path(DXRT_INCLUDE_DIR NAMES dxrt/dxrt_api.h REQUIRED)
find_library(DXRT_LIBRARY NAMES dxrt REQUIRED)

add_library(dx_app_dependency INTERFACE)
target_include_directories(dx_app_dependency INTERFACE
    "${DX_APP_ROOT}/src/cpp_example"
    "${DXRT_INCLUDE_DIR}"
)
target_link_libraries(dx_app_dependency INTERFACE "${DXRT_LIBRARY}")
