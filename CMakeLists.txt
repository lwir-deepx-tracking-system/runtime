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
find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS
    core
    imgproc
    videoio
)
pkg_check_modules(
    GSTREAMER
    REQUIRED
    IMPORTED_TARGET
    gstreamer-1.0>=1.16
    gstreamer-app-1.0>=1.16
)


# ==============================================================================
# i3system Thermal Expert camera SDK
# ==============================================================================

option(
    LWIR_ENABLE_I3_CAMERA
    "i3system Thermal Expert camera SDK를 활성화합니다"
    OFF
)

if(LWIR_ENABLE_I3_CAMERA)
    include(cmake/I3System.cmake)
    set(LWIR_CAMERA_SOURCE src/camera/camera.cpp)
else()
    set(LWIR_CAMERA_SOURCE src/camera/camera_stub.cpp)
endif()


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
    ${LWIR_CAMERA_SOURCE}

    # 객체 탐지
    src/detection/lwir_preprocessor.cpp
    src/detection/dxapp_detection_pipeline.cpp

    # 객체 추적
    src/tracking/bytetrack_tracker.cpp

    # 추적 대상 선택
    src/target/target_selector.cpp

    # 짐벌 제어
    src/control/gimbal_controller.cpp

    # GUI 통신 경계
    # GStreamer RTP/UDP 영상 송신과 TCP 선택 명령 수신을 포함한다.
    src/gui/gui_protocol.cpp
    src/gui/gui_sender.cpp
    src/gui/gui_receiver.cpp

    # 파이프라인
    src/pipeline/camera_thread.cpp
    src/pipeline/detection_thread.cpp
    src/pipeline/tracking_thread.cpp
    src/pipeline/control_thread.cpp
    src/pipeline/gui_sender_thread.cpp
    src/pipeline/gui_receiver_thread.cpp
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
        PkgConfig::GSTREAMER
        ${OpenCV_LIBS}
)

if(LWIR_ENABLE_I3_CAMERA)
    target_link_libraries(lwir_core
        PRIVATE
            i3system_dependency
    )
endif()

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
    # 실제 카메라 하드웨어 테스트
    # --------------------------------------------------------------------------

    if(LWIR_ENABLE_I3_CAMERA)
        add_executable(
            test_camera_capture
            tests/camera/test_camera_capture.cpp
        )
        target_link_libraries(test_camera_capture PRIVATE lwir_core)
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
    )
    target_link_libraries(test_app_config PRIVATE lwir_core)
    add_test(NAME app_config COMMAND test_app_config)

    add_executable(
        test_gui_protocol
        tests/gui/test_gui_protocol.cpp
    )
    target_link_libraries(test_gui_protocol PRIVATE lwir_core)
    add_test(NAME gui_protocol COMMAND test_gui_protocol)

    add_executable(
        test_gui_receiver
        tests/gui/test_gui_receiver.cpp
    )
    target_link_libraries(test_gui_receiver PRIVATE lwir_core)
    add_test(NAME gui_receiver COMMAND test_gui_receiver)

    add_executable(
        test_gui_sender
        tests/gui/test_gui_sender.cpp
    )
    target_link_libraries(test_gui_sender PRIVATE lwir_core)
    add_test(NAME gui_sender COMMAND test_gui_sender)


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
    # 선택 ID 상태 테스트
    # --------------------------------------------------------------------------

    add_executable(
        test_target_selector
        tests/target/test_target_selector.cpp
    )
    target_link_libraries(test_target_selector PRIVATE lwir_core)
    add_test(NAME target_selector COMMAND test_target_selector)

    add_executable(
        test_control_thread
        tests/control/test_control_thread.cpp
    )
    target_link_libraries(test_control_thread PRIVATE lwir_core)
    add_test(NAME control_thread COMMAND test_control_thread)


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
    target_link_libraries(test_bytetrack_scenarios PRIVATE lwir_core)
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
