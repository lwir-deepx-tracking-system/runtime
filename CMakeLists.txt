cmake_minimum_required(VERSION 3.16)

project(lwir_deepx_tracking_system LANGUAGES CXX)


# ==============================================================================
# C++ 빌드 설정
# ==============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# ==============================================================================
# 빌드 옵션
# ==============================================================================

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
if(LWIR_BUILD_CAPTURE)
    list(APPEND LWIR_OPENCV_COMPONENTS imgcodecs)
endif()
list(REMOVE_DUPLICATES LWIR_OPENCV_COMPONENTS)

if(LWIR_OPENCV_COMPONENTS)
    find_package(OpenCV REQUIRED COMPONENTS ${LWIR_OPENCV_COMPONENTS})
endif()

if(LWIR_NEEDS_RUNTIME_CORE)
    find_package(Threads REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(
        GSTREAMER
        REQUIRED
        IMPORTED_TARGET
        gstreamer-1.0>=1.16
        gstreamer-app-1.0>=1.16
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
        src/detection/dxapp_detection_pipeline.cpp
        src/tracking/bytetrack_tracker.cpp
    )

    target_include_directories(lwir_vision
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${OpenCV_INCLUDE_DIRS}
    )

    target_link_libraries(lwir_vision
        PUBLIC
            lwir_common
            ${OpenCV_LIBS}
    )

    if(LWIR_ENABLE_DX_APP)
        target_sources(lwir_vision PRIVATE src/detection/postprocessor.cpp)
        target_link_libraries(lwir_vision
            PRIVATE
                dx_app_dependency
        )
        target_compile_definitions(lwir_vision
            PRIVATE
                LWIR_ENABLE_DX_APP=1
        )
    endif()
endif()


# ==============================================================================
# Runtime 전용 라이브러리
# ==============================================================================

if(LWIR_NEEDS_RUNTIME_CORE)
    add_library(lwir_runtime_core
        src/target/target_selector.cpp
        src/control/gimbal_controller.cpp
        src/gui/gui_protocol.cpp
        src/gui/gui_sender.cpp
        src/gui/gui_receiver.cpp
        src/pipeline/camera_thread.cpp
        src/pipeline/detection_thread.cpp
        src/pipeline/tracking_thread.cpp
        src/pipeline/control_thread.cpp
        src/pipeline/gui_sender_thread.cpp
        src/pipeline/gui_receiver_thread.cpp
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
    add_executable(lwir_runtime
        apps/runtime/main.cpp
        apps/runtime/runtime.cpp
    )

    target_link_libraries(lwir_runtime
        PRIVATE
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
    )

    target_link_libraries(lwir_benchmark
        PRIVATE
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
    )
    target_include_directories(test_app_config PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/apps
    )
    target_link_libraries(test_app_config PRIVATE
        lwir_common
        lwir_camera
        lwir_vision
        lwir_runtime_core
    )
    add_test(NAME app_config COMMAND test_app_config)

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

    add_executable(
        test_gui_sender
        tests/gui/test_gui_sender.cpp
    )
    target_link_libraries(test_gui_sender PRIVATE lwir_runtime_core)
    add_test(NAME gui_sender COMMAND test_gui_sender)


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


    # --------------------------------------------------------------------------
    # 객체 추적 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_bytetrack
        tests/tracking/test_bytetrack.cpp
    )
    target_link_libraries(test_bytetrack PRIVATE lwir_vision)
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
    target_link_libraries(test_bytetrack_scenarios PRIVATE lwir_vision)
    add_test(NAME bytetrack_scenarios COMMAND test_bytetrack_scenarios)


    # --------------------------------------------------------------------------
    # 테스트 실행 경로 설정
    # --------------------------------------------------------------------------

    set_tests_properties(
        app_config
        lwir_preprocessor
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

endif()
