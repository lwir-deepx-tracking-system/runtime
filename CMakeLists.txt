cmake_minimum_required(VERSION 3.16)

project(lwir_deepx_tracking_system LANGUAGES CXX)

# PROJECT_ROOT_DIR is consumed by common_util.hpp in several targets.
add_compile_definitions(PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}")


# ==============================================================================
# C++ 빌드 설정
# ==============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# ==============================================================================
# 빌드 옵션
# ==============================================================================

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(LWIR_BUILD_RUNTIME "Build runtime application" ON)
option(LWIR_BUILD_BENCHMARK "Build benchmark application" ON)
option(LWIR_BUILD_CAPTURE "Build capture application" ON)
option(LWIR_ENABLE_I3_CAMERA "Enable i3system Thermal Expert camera SDK" OFF)
option(LWIR_ENABLE_DX_APP "Enable dx_app and DX-RT dependencies" OFF)

include(CTest)

set(LWIR_NEEDS_CAMERA OFF)
if(LWIR_BUILD_RUNTIME OR LWIR_BUILD_CAPTURE OR BUILD_TESTING)
    set(LWIR_NEEDS_CAMERA ON)
endif()

set(LWIR_NEEDS_VISION OFF)
if(LWIR_BUILD_RUNTIME OR LWIR_BUILD_BENCHMARK OR BUILD_TESTING)
    set(LWIR_NEEDS_VISION ON)
endif()

set(LWIR_NEEDS_RUNTIME_CORE OFF)
if(LWIR_BUILD_RUNTIME OR BUILD_TESTING)
    set(LWIR_NEEDS_RUNTIME_CORE ON)
endif()


# ==============================================================================
# 외부 의존성
# ==============================================================================

find_package(yaml-cpp REQUIRED)

set(LWIR_OPENCV_COMPONENTS)
if(LWIR_NEEDS_CAMERA OR LWIR_NEEDS_VISION OR LWIR_NEEDS_RUNTIME_CORE)
    list(APPEND LWIR_OPENCV_COMPONENTS core)
endif()
if(LWIR_NEEDS_VISION OR LWIR_NEEDS_RUNTIME_CORE)
    list(APPEND LWIR_OPENCV_COMPONENTS imgproc)
endif()
if(LWIR_BUILD_CAPTURE OR LWIR_BUILD_BENCHMARK OR LWIR_BUILD_RUNTIME)
    list(APPEND LWIR_OPENCV_COMPONENTS imgcodecs)
endif()
list(REMOVE_DUPLICATES LWIR_OPENCV_COMPONENTS)

if(LWIR_OPENCV_COMPONENTS)
    find_package(OpenCV REQUIRED COMPONENTS ${LWIR_OPENCV_COMPONENTS})
endif()

find_package(Threads REQUIRED)

if(LWIR_NEEDS_RUNTIME_CORE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(
        GSTREAMER
        REQUIRED
        IMPORTED_TARGET
        gstreamer-1.0>=1.20
        gstreamer-app-1.0>=1.20
    )
endif()

if(LWIR_NEEDS_CAMERA AND LWIR_ENABLE_I3_CAMERA)
    include(cmake/I3System.cmake)
endif()

if(LWIR_NEEDS_VISION AND LWIR_ENABLE_DX_APP)
    include(cmake/DxApp.cmake)
endif()


# ==============================================================================
# 공통 라이브러리
# ==============================================================================

add_library(lwir_common
    src/common/logger.cpp
    src/config/app_config.cpp
)

target_include_directories(lwir_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(lwir_common
    PUBLIC
        yaml-cpp
)


# 콘솔·측정·종료·실행 폴더는 특정 실행 파일이나 GUI 구현에 의존하지 않는다.
add_library(lwir_app_support
    src/app/pipeline_console.cpp
    src/app/metrics_writer.cpp
    src/app/shutdown.cpp
    src/app/run_directory.cpp
)
target_link_libraries(lwir_app_support PUBLIC lwir_common Threads::Threads)

# ==============================================================================
# 카메라 라이브러리
# ==============================================================================

if(LWIR_NEEDS_CAMERA)
    if(LWIR_ENABLE_I3_CAMERA)
        set(LWIR_CAMERA_SOURCE src/camera/camera.cpp)
    else()
        set(LWIR_CAMERA_SOURCE src/camera/camera_stub.cpp)
    endif()

    add_library(lwir_camera
        ${LWIR_CAMERA_SOURCE}
    )

    target_include_directories(lwir_camera
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${OpenCV_INCLUDE_DIRS}
    )

    target_link_libraries(lwir_camera
        PUBLIC
            lwir_common
            ${OpenCV_LIBS}
    )

    if(LWIR_ENABLE_I3_CAMERA)
        target_link_libraries(lwir_camera
            PRIVATE
                i3system_dependency
        )
    endif()
endif()


# ==============================================================================
# Detection / Tracking 라이브러리
# ==============================================================================

if(LWIR_NEEDS_VISION)
    add_library(lwir_vision
        src/detection/lwir_preprocessor.cpp
        src/detection/dxrt_detection_pipeline.cpp
        src/tracking/bytetrack_tracker.cpp
        src/tracking/tracker_factory.cpp
        src/tracking/tracker_manager.cpp
        src/tracking/kalman_filter.cpp
        src/tracking/ocsort_tracker.cpp
    )

    target_include_directories(lwir_vision
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${OpenCV_INCLUDE_DIRS}
    )

    target_link_libraries(lwir_vision
        PUBLIC
            lwir_common
            Threads::Threads
            ${OpenCV_LIBS}
    )

    if(LWIR_ENABLE_DX_APP)
        target_sources(lwir_vision PRIVATE
            src/detection/yolo_postprocessor_core.cpp
            src/detection/yolov8_postprocessor.cpp
            src/detection/yolo26_postprocessor.cpp
            src/detection/detector_factory.cpp)
        target_link_libraries(lwir_vision
            PRIVATE
                dx_app_dependency
        )
        target_compile_definitions(lwir_vision
            PRIVATE
                LWIR_ENABLE_DX_APP=1
                PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
    else()
        target_sources(lwir_vision PRIVATE src/detection/detector_factory_stub.cpp)
    endif()
endif()


# ==============================================================================
# Runtime 전용 라이브러리
# ==============================================================================

if(LWIR_NEEDS_RUNTIME_CORE)
    add_library(lwir_runtime_core
        src/target/target_selector.cpp
        src/control/gimbal_device.cpp
        src/control/pid_controller.cpp
        src/control/auto_tracking_controller.cpp
        src/control/gimbal_command_executor.cpp
        src/control/tracking_command_handler.cpp
        src/gui/gui_command_handler.cpp
        src/gui/gui_protocol.cpp
        src/gui/gui_sender.cpp
        src/gui/gui_receiver.cpp
        src/pipeline/camera_thread.cpp
        src/pipeline/tracker_change_service.cpp
        src/pipeline/model_manager_thread.cpp
        src/pipeline/detection_thread.cpp
        src/pipeline/tracking_thread.cpp
        src/pipeline/control_thread.cpp
        src/pipeline/gui_sender_thread.cpp
        src/pipeline/gui_receiver_thread.cpp
        src/pipeline/runtime_control.cpp
        src/pipeline/active_pipeline_slot.cpp
    )

    target_include_directories(lwir_runtime_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${OpenCV_INCLUDE_DIRS}
    )

    target_link_libraries(lwir_runtime_core
        PUBLIC
            lwir_common
            lwir_camera
            lwir_vision
            Threads::Threads
            PkgConfig::GSTREAMER
            ${OpenCV_LIBS}
    )
endif()


# ==============================================================================
# 애플리케이션
# ==============================================================================


# ------------------------------------------------------------------------------
# Runtime
# ------------------------------------------------------------------------------

if(LWIR_BUILD_RUNTIME)
    add_executable(lwir_video_send tools/video_send.cpp)
    target_link_libraries(lwir_video_send PRIVATE lwir_runtime_core)

    add_executable(lwir_runtime
        apps/runtime/main.cpp
        apps/runtime/runtime.cpp
        apps/runtime/runtime_workers.cpp
    )

    target_link_libraries(lwir_runtime
        PRIVATE
            lwir_app_support
            lwir_common
            lwir_camera
            lwir_vision
            lwir_runtime_core
    )
endif()


# ------------------------------------------------------------------------------
# Benchmark
# ------------------------------------------------------------------------------

if(LWIR_BUILD_BENCHMARK)
    add_executable(lwir_benchmark
        apps/benchmark/main.cpp
        apps/benchmark/benchmark.cpp
        apps/benchmark/result_writer.cpp
    )

    target_link_libraries(lwir_benchmark
        PRIVATE
            lwir_app_support
            lwir_common
            lwir_vision
    )
endif()


# ------------------------------------------------------------------------------
# Capture
# ------------------------------------------------------------------------------

if(LWIR_BUILD_CAPTURE)
    add_executable(lwir_capture
        apps/capture/main.cpp
        apps/capture/capture.cpp
    )

    target_link_libraries(lwir_capture
        PRIVATE
            lwir_common
            lwir_camera
            ${OpenCV_LIBS}
    )
endif()


# ==============================================================================
# 테스트
# ==============================================================================

if(BUILD_TESTING)

    # --------------------------------------------------------------------------
    # 실제 카메라 하드웨어 테스트
    # --------------------------------------------------------------------------

    if(LWIR_ENABLE_I3_CAMERA)
        add_executable(
            test_camera_capture
            tests/camera/test_camera_capture.cpp
        )
        target_link_libraries(test_camera_capture PRIVATE
            lwir_common
            lwir_camera
            lwir_runtime_core
        )
        add_test(NAME camera_capture COMMAND test_camera_capture)
        set_tests_properties(camera_capture PROPERTIES
            LABELS "hardware;camera"
            RESOURCE_LOCK i3_camera
            TIMEOUT 30
        )
    endif()

    # --------------------------------------------------------------------------
    # 애플리케이션 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_app_config
        tests/app/test_app_config.cpp
        apps/runtime/runtime.cpp
        apps/runtime/runtime_workers.cpp
    )
    target_include_directories(test_app_config PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/apps
    )
    target_link_libraries(test_app_config PRIVATE
        lwir_app_support
        lwir_common
        lwir_camera
        lwir_vision
        lwir_runtime_core
    )
    add_test(NAME app_config COMMAND test_app_config)

    add_executable(test_runtime_metrics_writer
        tests/app/test_runtime_metrics_writer.cpp)
    target_include_directories(test_runtime_metrics_writer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps)
    target_link_libraries(test_runtime_metrics_writer PRIVATE lwir_app_support)
    add_test(NAME runtime_metrics_writer COMMAND test_runtime_metrics_writer)

    add_executable(test_runtime_shutdown tests/app/test_runtime_shutdown.cpp)
    target_include_directories(test_runtime_shutdown PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps)
    target_link_libraries(test_runtime_shutdown PRIVATE lwir_app_support)
    add_test(NAME runtime_shutdown COMMAND test_runtime_shutdown)

    add_executable(test_runtime_console
        tests/app/test_runtime_console.cpp)
    target_include_directories(test_runtime_console PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps)
    target_link_libraries(test_runtime_console PRIVATE lwir_app_support)
    add_test(NAME runtime_console COMMAND test_runtime_console)

    add_executable(test_benchmark_config
        tests/app/test_benchmark_config.cpp
        apps/benchmark/benchmark.cpp
        apps/benchmark/result_writer.cpp)
    target_include_directories(test_benchmark_config PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/apps)
    target_link_libraries(test_benchmark_config PRIVATE lwir_vision lwir_app_support)
    add_test(NAME benchmark_config COMMAND test_benchmark_config)

    add_executable(
        test_detection_config
        tests/app/test_detection_config.cpp
    )
    target_link_libraries(test_detection_config PRIVATE lwir_common)
    add_test(NAME detection_config COMMAND test_detection_config)

    add_executable(
        test_benchmark_result_writer
        tests/benchmark/test_result_writer.cpp
        apps/benchmark/result_writer.cpp
    )
    target_include_directories(test_benchmark_result_writer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/benchmark
    )
    add_test(NAME benchmark_result_writer COMMAND test_benchmark_result_writer)

    add_executable(test_auto_control_e2e tests/control/test_auto_control_e2e.cpp)
    target_link_libraries(test_auto_control_e2e PRIVATE lwir_runtime_core)
    add_test(NAME auto_control_e2e COMMAND test_auto_control_e2e)
    set_tests_properties(auto_control_e2e PROPERTIES TIMEOUT 15)

    add_executable(test_pid_controller tests/control/test_pid_controller.cpp)
    target_link_libraries(test_pid_controller PRIVATE lwir_runtime_core)
    add_test(NAME pid_controller COMMAND test_pid_controller)

    add_executable(test_auto_control tests/control/test_auto_control.cpp)
    target_link_libraries(test_auto_control PRIVATE lwir_runtime_core)
    add_test(NAME auto_control COMMAND test_auto_control)
    set_tests_properties(auto_control PROPERTIES TIMEOUT 15)

    add_executable(test_gimbal_device tests/control/test_gimbal_device.cpp)
    target_link_libraries(test_gimbal_device PRIVATE lwir_runtime_core)
    add_test(NAME gimbal_device COMMAND test_gimbal_device)
    set_tests_properties(gimbal_device PROPERTIES TIMEOUT 15)

    add_executable(
        test_gui_protocol
        tests/gui/test_gui_protocol.cpp
    )
    target_link_libraries(test_gui_protocol PRIVATE lwir_runtime_core)
    add_test(NAME gui_protocol COMMAND test_gui_protocol)

    add_executable(
        test_gui_receiver
        tests/gui/test_gui_receiver.cpp
    )
    target_link_libraries(test_gui_receiver PRIVATE lwir_runtime_core)
    add_test(NAME gui_receiver COMMAND test_gui_receiver)

    add_executable(test_gui_pipeline_commands tests/gui/test_gui_pipeline_commands.cpp)
    target_link_libraries(test_gui_pipeline_commands PRIVATE lwir_runtime_core)
    add_test(NAME gui_pipeline_commands COMMAND test_gui_pipeline_commands)

    add_executable(
        test_gui_sender
        tests/gui/test_gui_sender.cpp
    )
    target_link_libraries(test_gui_sender PRIVATE lwir_runtime_core)
    add_test(NAME gui_sender COMMAND test_gui_sender x264enc)
    add_test(NAME gui_sender_jpeg COMMAND test_gui_sender jpegenc)
    add_test(NAME gui_sender_bypass COMMAND test_gui_sender x264enc bypass)
    add_test(NAME gui_sender_bypass_jpeg COMMAND test_gui_sender jpegenc bypass)
    option(LWIR_TEST_MPP "Run Orange Pi MPP hardware encoder tests" OFF)
    if(LWIR_TEST_MPP)
        add_test(NAME gui_sender_mpp_jpeg COMMAND test_gui_sender mppjpegenc)
        add_test(NAME gui_sender_mpp_h264 COMMAND test_gui_sender mpph264enc)
    endif()


    # --------------------------------------------------------------------------
    # 객체 탐지 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_lwir_preprocessor
        tests/detection/test_lwir_preprocessor.cpp
    )
    target_link_libraries(test_lwir_preprocessor PRIVATE lwir_vision)
    add_test(NAME lwir_preprocessor COMMAND test_lwir_preprocessor)

    add_executable(
        test_detection_thread
        tests/detection/test_detection_thread.cpp
    )
    target_link_libraries(test_detection_thread PRIVATE lwir_runtime_core)
    add_test(NAME detection_thread COMMAND test_detection_thread)

    if(LWIR_ENABLE_DX_APP)
        add_executable(
            test_postprocessor
            tests/detection/test_postprocessor.cpp
        )
        target_link_libraries(test_postprocessor PRIVATE lwir_vision)
        add_test(NAME postprocessor COMMAND test_postprocessor)
    endif()


    # --------------------------------------------------------------------------
    # 선택 ID 상태 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_target_selector
        tests/target/test_target_selector.cpp
    )
    target_link_libraries(test_target_selector PRIVATE lwir_runtime_core)
    add_test(NAME target_selector COMMAND test_target_selector)

    add_executable(
        test_control_thread
        tests/control/test_control_thread.cpp
    )
    target_link_libraries(test_control_thread PRIVATE lwir_runtime_core)
    add_test(NAME control_thread COMMAND test_control_thread)


    # --------------------------------------------------------------------------
    # 파이프라인 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_shared_frame_pipeline
        tests/pipeline/test_shared_frame_pipeline.cpp
    )
    target_link_libraries(test_shared_frame_pipeline PRIVATE lwir_runtime_core)
    add_test(NAME shared_frame_pipeline COMMAND test_shared_frame_pipeline)

    add_executable(test_model_manager_recovery
        tests/pipeline/test_model_manager_recovery.cpp)
    target_link_libraries(test_model_manager_recovery PRIVATE lwir_runtime_core)
    add_test(NAME model_manager_recovery COMMAND test_model_manager_recovery)

    add_executable(test_tracking_failure_shutdown tests/pipeline/test_tracking_failure_shutdown.cpp)
    target_link_libraries(test_tracking_failure_shutdown PRIVATE lwir_runtime_core)
    add_test(NAME tracking_failure_shutdown COMMAND test_tracking_failure_shutdown)
    set_tests_properties(tracking_failure_shutdown PROPERTIES TIMEOUT 10)

    add_executable(test_detection_quiescence tests/pipeline/test_detection_quiescence.cpp)
    target_link_libraries(test_detection_quiescence PRIVATE lwir_runtime_core)
    add_test(NAME detection_quiescence COMMAND test_detection_quiescence)
    set_tests_properties(detection_quiescence PROPERTIES TIMEOUT 10)

    add_executable(test_model_switch_wakeup
        tests/pipeline/test_model_switch_wakeup.cpp)
    target_link_libraries(test_model_switch_wakeup PRIVATE lwir_runtime_core)
    add_test(NAME model_switch_wakeup COMMAND test_model_switch_wakeup)

    add_executable(test_tracker_change_service tests/pipeline/test_tracker_change_service.cpp)
    target_link_libraries(test_tracker_change_service PRIVATE lwir_runtime_core)
    add_test(NAME tracker_change_service COMMAND test_tracker_change_service)
    set_tests_properties(tracker_change_service PROPERTIES TIMEOUT 10)

    add_executable(test_tracker_hotswap tests/pipeline/test_tracker_hotswap.cpp)
    target_link_libraries(test_tracker_hotswap PRIVATE lwir_runtime_core)
    add_test(NAME tracker_hotswap COMMAND test_tracker_hotswap)


    # --------------------------------------------------------------------------
    # 객체 추적 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_bytetrack
        tests/tracking/test_bytetrack.cpp
    )
    target_link_libraries(test_bytetrack PRIVATE lwir_vision)
    add_test(NAME bytetrack COMMAND test_bytetrack)

    add_executable(
        test_ocsort
        tests/tracking/test_ocsort.cpp
    )
    target_link_libraries(test_ocsort PRIVATE lwir_vision)
    add_test(NAME ocsort COMMAND test_ocsort)

    add_executable(test_tracker_factory
        tests/tracking/test_tracker_factory.cpp)
    target_link_libraries(test_tracker_factory PRIVATE lwir_vision)
    add_test(NAME tracker_factory COMMAND test_tracker_factory)

    set_tests_properties(
        bytetrack
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # MOT17 공개 데이터 실행 도구 (ctest 미등록, datasets/MOT17이 있을 때 직접 실행)
    add_executable(
        bench_mot17
        tests/tracking/bench_mot17.cpp
    )
    target_link_libraries(bench_mot17 PRIVATE lwir_vision)


    # --------------------------------------------------------------------------
    # 테스트 실행 경로 설정
    # --------------------------------------------------------------------------

    set_tests_properties(
        app_config
        benchmark_config
        detection_config
        lwir_preprocessor
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

endif()
