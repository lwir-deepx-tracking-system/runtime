cmake_minimum_required(VERSION 3.16)

project(lwir_deepx_tracking_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 빌드에 필요한 라이브러리
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

# 메인 runtime 실행 파일
add_executable(lwir_runtime
    src/main.cpp

    src/common/logger.cpp

    src/app/app_config.cpp
    src/app/application.cpp

    src/factory/component_factory.cpp

    src/camera/camera.cpp

    src/preprocess/yolov8_preprocessor.cpp

    src/inference/inference.cpp

    src/postprocess/yolov8_postprocessor.cpp

    src/tracking/bytetrack_tracker.cpp

    src/target/target_selector.cpp
    src/control/gimbal_controller.cpp

    src/pipeline/camera_thread.cpp
    src/pipeline/preprocess_thread.cpp
    src/pipeline/inference_thread.cpp
    src/pipeline/postprocess_thread.cpp
    src/pipeline/tracking_thread.cpp
    src/pipeline/target_selection_thread.cpp
    src/pipeline/control_thread.cpp
)

# 헤더 검색 경로
target_include_directories(lwir_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

# 실행 파일에 연결할 라이브러리
target_link_libraries(lwir_runtime PRIVATE
    Threads::Threads
    yaml-cpp
    ${OpenCV_LIBS}
)

# 컴포넌트 테스트 예시. 담당 구현이 준비되면 필요한 대상만 주석 해제한다.
# enable_testing()
# add_executable(test_target_selector
#     tests/target/test_target_selector.cpp
#     src/target/target_selector.cpp
# )
# target_include_directories(test_target_selector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# add_test(NAME target_selector COMMAND test_target_selector)
#
# add_executable(test_bytetrack
#     tests/tracking/test_bytetrack.cpp
#     src/tracking/bytetrack_tracker.cpp
#     src/common/logger.cpp
# )
# target_include_directories(test_bytetrack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# target_link_libraries(test_bytetrack PRIVATE Threads::Threads)
# add_test(NAME bytetrack COMMAND test_bytetrack)
# set_tests_properties(bytetrack PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
