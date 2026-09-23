cmake_minimum_required(VERSION 3.16)
project(lwir_deepx_tracking_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

option(LWIR_ENABLE_DX_APP "dx_app 및 DX-RT 의존성을 활성화합니다" OFF)
if(LWIR_ENABLE_DX_APP)
    include(cmake/DxApp.cmake)
endif()

add_library(lwir_core
    src/common/logger.cpp
    src/app/app_config.cpp
    src/camera/camera.cpp
    src/detection/lwir_preprocessor.cpp
    src/detection/dxapp_detection_pipeline.cpp
    src/factory/component_factory.cpp
    src/tracking/bytetrack_tracker.cpp
    src/target/target_selector.cpp
    src/control/gimbal_controller.cpp
    src/pipeline/camera_thread.cpp
    src/pipeline/detection_thread.cpp
    src/pipeline/tracking_thread.cpp
    src/pipeline/target_selection_thread.cpp
    src/pipeline/control_thread.cpp
)
target_include_directories(lwir_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(lwir_core PUBLIC Threads::Threads yaml-cpp ${OpenCV_LIBS})
if(LWIR_ENABLE_DX_APP)
    target_link_libraries(lwir_core PRIVATE dx_app_dependency)
    target_compile_definitions(lwir_core PRIVATE LWIR_ENABLE_DX_APP=1)
endif()

add_executable(lwir_runtime src/main.cpp src/app/application.cpp)
target_link_libraries(lwir_runtime PRIVATE lwir_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_app_config tests/app/test_app_config.cpp)
    target_link_libraries(test_app_config PRIVATE lwir_core)
    add_test(NAME app_config COMMAND test_app_config)

    add_executable(test_lwir_preprocessor tests/detection/test_lwir_preprocessor.cpp)
    target_link_libraries(test_lwir_preprocessor PRIVATE lwir_core)
    add_test(NAME lwir_preprocessor COMMAND test_lwir_preprocessor)

    add_executable(test_detection_thread tests/detection/test_detection_thread.cpp)
    target_link_libraries(test_detection_thread PRIVATE lwir_core)
    add_test(NAME detection_thread COMMAND test_detection_thread)

    add_executable(test_target_selector tests/target/test_target_selector.cpp src/target/target_selector.cpp)
    target_include_directories(test_target_selector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME target_selector COMMAND test_target_selector)

    set_tests_properties(app_config lwir_preprocessor PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
