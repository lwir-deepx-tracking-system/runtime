cmake_minimum_required(VERSION 3.16)

project(lwir_deepx_tracking_system LANGUAGES CXX)


# ==============================================================================
# C++ 빌드 설정
# ==============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# ==============================================================================
# 외부 의존성
# ==============================================================================

find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS
    core
    imgproc
    videoio
)


# ==============================================================================
# DEEPX / dx_app 설정
# ==============================================================================

option(
    LWIR_ENABLE_DX_APP
    "dx_app 및 DX-RT 의존성을 활성화합니다"
    OFF
)

if(LWIR_ENABLE_DX_APP)
    include(cmake/DxApp.cmake)
endif()


# ==============================================================================
# 핵심 라이브러리
# ==============================================================================

add_library(lwir_core

    # 공통
    src/common/logger.cpp

    # 애플리케이션
    src/app/app_config.cpp

    # 카메라
    src/camera/camera.cpp

    # 객체 탐지
    src/detection/lwir_preprocessor.cpp
    src/detection/dxapp_detection_pipeline.cpp

    # 컴포넌트 생성
    src/detection/yolov8_postprocessor.cpp
    src/factory/component_factory.cpp

    # 객체 추적
    src/tracking/bytetrack_tracker.cpp

    # 추적 대상 선택
    src/target/target_selector.cpp

    # 짐벌 제어
    src/control/gimbal_controller.cpp

    # 파이프라인
    src/pipeline/camera_thread.cpp
    src/pipeline/detection_thread.cpp
    src/pipeline/tracking_thread.cpp
    src/pipeline/target_selection_thread.cpp
    src/pipeline/control_thread.cpp
)

target_include_directories(lwir_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(lwir_core
    PUBLIC
        Threads::Threads
        yaml-cpp
        ${OpenCV_LIBS}
)

if(LWIR_ENABLE_DX_APP)
    target_link_libraries(lwir_core
        PRIVATE
            dx_app_dependency
    )

    target_compile_definitions(lwir_core
        PRIVATE
            LWIR_ENABLE_DX_APP=1
    )
endif()


# ==============================================================================
# 런타임 실행 파일
# ==============================================================================

add_executable(lwir_runtime
    src/main.cpp
    src/app/application.cpp
)

target_link_libraries(lwir_runtime
    PRIVATE
        lwir_core
)


# ==============================================================================
# 테스트
# ==============================================================================

include(CTest)

if(BUILD_TESTING)

    # --------------------------------------------------------------------------
    # 애플리케이션 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_app_config
        tests/app/test_app_config.cpp
    )
    target_link_libraries(test_app_config PRIVATE lwir_core)
    add_test(NAME app_config COMMAND test_app_config)


    # --------------------------------------------------------------------------
    # 객체 탐지 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_lwir_preprocessor
        tests/detection/test_lwir_preprocessor.cpp
    )
    target_link_libraries(test_lwir_preprocessor PRIVATE lwir_core)
    add_test(NAME lwir_preprocessor COMMAND test_lwir_preprocessor)

    add_executable(
        test_detection_thread
        tests/detection/test_detection_thread.cpp
    )
    target_link_libraries(test_detection_thread PRIVATE lwir_core)
    add_test(NAME detection_thread COMMAND test_detection_thread)


    # --------------------------------------------------------------------------
    # 추적 대상 선택 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_target_selector
        tests/target/test_target_selector.cpp
    )
    target_link_libraries(test_target_selector PRIVATE lwir_core)
    add_test(NAME target_selector COMMAND test_target_selector)


    # --------------------------------------------------------------------------
    # 파이프라인 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_shared_frame_pipeline
        tests/pipeline/test_shared_frame_pipeline.cpp
    )
    target_link_libraries(test_shared_frame_pipeline PRIVATE lwir_core)
    add_test(NAME shared_frame_pipeline COMMAND test_shared_frame_pipeline)


    # --------------------------------------------------------------------------
    # 객체 추적 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_bytetrack
        tests/tracking/test_bytetrack.cpp
    )
    target_link_libraries(test_bytetrack PRIVATE lwir_core)
    add_test(NAME bytetrack COMMAND test_bytetrack)

    set_tests_properties(
        bytetrack
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

        add_executable(
        test_bytetrack_scenarios
        tests/tracking/test_bytetrack_scenarios.cpp
    )
    target_link_libraries(
        test_bytetrack_scenarios
        PRIVATE lwir_core
    )
    add_test(
        NAME bytetrack_scenarios
        COMMAND test_bytetrack_scenarios
    )
    
    add_executable(
        test_bytetrack_cases
        tests/tracking/test_bytetrack_cases.cpp
    )
    target_link_libraries(
        test_bytetrack_cases
        PRIVATE lwir_core
    )
    add_test(
        NAME bytetrack_cases
        COMMAND test_bytetrack_cases
    )


    # --------------------------------------------------------------------------
    # YOLOv8 후처리 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_yolov8_postprocessor
        tests/detection/test_yolov8_postprocessor.cpp
    )
    target_link_libraries(
        test_yolov8_postprocessor
        PRIVATE lwir_core
    )
    add_test(
        NAME yolov8_postprocessor
        COMMAND test_yolov8_postprocessor
    )


    # --------------------------------------------------------------------------
    # 테스트 실행 경로 설정
    # --------------------------------------------------------------------------

    set_tests_properties(
        app_config
        lwir_preprocessor
        yolov8_postprocessor
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

endif()