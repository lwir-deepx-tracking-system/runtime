find_package(PkgConfig REQUIRED)
pkg_check_modules(WEB_GUI REQUIRED IMPORTED_TARGET
    cpp-httplib nlohmann_json gio-2.0 gstreamer-app-1.0>=1.20 cairo openssl
)

add_executable(lwir_web_gui main.cpp lan.cpp video.cpp ../../src/gui/gui_protocol.cpp)
target_include_directories(lwir_web_gui PRIVATE ../../include)
target_link_libraries(lwir_web_gui PRIVATE PkgConfig::WEB_GUI Threads::Threads)
