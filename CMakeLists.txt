cmake_minimum_required(VERSION 3.24)

project(PureView VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "PureView for Windows must be built with a Windows toolchain.")
endif()

include(CTest)

add_library(pureview_core STATIC
    src/core.cpp
)
target_include_directories(pureview_core PUBLIC include)
target_compile_features(pureview_core PUBLIC cxx_std_20)
target_compile_definitions(pureview_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

add_executable(PureView WIN32
    src/main.cpp
    src/image_loader.cpp
    src/viewer_window.cpp
    resources/PureView.rc
)
target_include_directories(PureView PRIVATE include src)
target_compile_features(PureView PRIVATE cxx_std_20)
target_compile_definitions(PureView PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(PureView PRIVATE
    pureview_core
    comctl32
    comdlg32
    d2d1
    dwmapi
    ole32
    shell32
    shlwapi
    windowscodecs
)

if(MSVC)
    target_compile_options(pureview_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(PureView PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(PureView PRIVATE
        /SUBSYSTEM:WINDOWS
        /MANIFEST:EMBED
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest"
    )
endif()

if(BUILD_TESTING)
    add_executable(PureViewCoreTests tests/core_tests.cpp)
    target_link_libraries(PureViewCoreTests PRIVATE pureview_core)
    target_compile_features(PureViewCoreTests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(PureViewCoreTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME PureViewCoreTests COMMAND PureViewCoreTests)
endif()

install(TARGETS PureView RUNTIME DESTINATION .)
install(FILES
    installer/install.cmd
    installer/install.ps1
    installer/uninstall.cmd
    installer/uninstall.ps1
    LICENSE
    README.md
    DESTINATION .
)
