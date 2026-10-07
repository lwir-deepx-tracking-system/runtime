# Orange Pi에 설치된 i3system Thermal Expert SDK를 찾는다.
set(I3SYSTEM_ROOT "/usr/local" CACHE PATH "i3system SDK 설치 루트")

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    message(FATAL_ERROR
        "i3system SDK는 AArch64용입니다. 현재 대상: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

find_path(I3SYSTEM_INCLUDE_DIR
    NAMES i3system_TE.h
    HINTS "${I3SYSTEM_ROOT}/include/i3system"
)
find_library(I3SYSTEM_TE_LIBRARY
    NAMES i3system_te_64
    HINTS "${I3SYSTEM_ROOT}/lib"
)
find_library(I3SYSTEM_USB_LIBRARY
    NAMES i3system_usb_64
    HINTS "${I3SYSTEM_ROOT}/lib"
)
find_library(I3SYSTEM_IMGPROC_IMPL_LIBRARY
    NAMES i3system_imgproc_impl_64
    HINTS "${I3SYSTEM_ROOT}/lib"
)

if(NOT I3SYSTEM_INCLUDE_DIR OR
   NOT I3SYSTEM_TE_LIBRARY OR
   NOT I3SYSTEM_USB_LIBRARY OR
   NOT I3SYSTEM_IMGPROC_IMPL_LIBRARY)
    message(FATAL_ERROR
        "i3system SDK를 찾을 수 없습니다: ${I3SYSTEM_ROOT}\n"
        "-DI3SYSTEM_ROOT=<SDK 설치 루트>를 지정하세요.")
endif()

add_library(i3system_dependency INTERFACE)
target_include_directories(i3system_dependency INTERFACE
    "${I3SYSTEM_INCLUDE_DIR}"
)
target_compile_definitions(i3system_dependency INTERFACE
    _LINUX
)
target_link_libraries(i3system_dependency INTERFACE
    "${I3SYSTEM_TE_LIBRARY}"
    "${I3SYSTEM_USB_LIBRARY}"
    "${I3SYSTEM_IMGPROC_IMPL_LIBRARY}"
)
