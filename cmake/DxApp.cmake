# Orange Pi에 설치된 DX-RT SDK를 시스템 경로에서 찾는다.
find_path(DXRT_INCLUDE_DIR NAMES dxrt/dxrt_api.h REQUIRED)
find_library(DXRT_LIBRARY NAMES dxrt REQUIRED)

add_library(dx_app_dependency INTERFACE)
target_include_directories(dx_app_dependency INTERFACE
    "${DX_APP_ROOT}/src/cpp_example"
    "${DXRT_INCLUDE_DIR}"
)
target_link_libraries(dx_app_dependency INTERFACE "${DXRT_LIBRARY}")
