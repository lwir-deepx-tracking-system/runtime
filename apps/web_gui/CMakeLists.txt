find_package(PkgConfig REQUIRED)
pkg_check_modules(WEB_GUI REQUIRED IMPORTED_TARGET
    cpp-httplib nlohmann_json gio-2.0 gstreamer-app-1.0>=1.20 cairo openssl
)

add_library(lwir_web_gui STATIC web_server.cpp lan.cpp video.cpp replay.cpp)
target_include_directories(lwir_web_gui PUBLIC ../../include PRIVATE ${OpenCV_INCLUDE_DIRS})
target_compile_definitions(lwir_web_gui INTERFACE LWIR_HAS_WEB_GUI)
target_link_libraries(lwir_web_gui PRIVATE lwir_runtime_core PkgConfig::WEB_GUI Threads::Threads ZLIB::ZLIB ${OpenCV_LIBS})

if(BUILD_TESTING)
    add_executable(test_web_server ../../tests/gui/test_web_server.cpp)
    target_link_libraries(test_web_server PRIVATE lwir_web_gui lwir_runtime_core PkgConfig::WEB_GUI)
    add_test(NAME web_server COMMAND test_web_server)
    set_tests_properties(web_server PROPERTIES TIMEOUT 15)
endif()
