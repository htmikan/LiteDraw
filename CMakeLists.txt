cmake_minimum_required(VERSION 3.25)

project(LiteDraw VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "LiteDraw is a Windows desktop application.")
endif()

add_executable(LiteDraw WIN32
    src/main.cpp
    src/export_codec.cpp
    src/LiteDraw.rc
)

find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(libdeflate CONFIG REQUIRED)

target_compile_features(LiteDraw PRIVATE cxx_std_20)
target_compile_definitions(LiteDraw PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_compile_options(LiteDraw PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8 /EHsc>
)
target_link_libraries(LiteDraw PRIVATE
    d2d1
    dwrite
    windowscodecs
    comctl32
    comdlg32
    shell32
    ole32
    uuid
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::jpeg>,libjpeg-turbo::jpeg,libjpeg-turbo::jpeg-static>
    $<IF:$<TARGET_EXISTS:libdeflate::libdeflate_shared>,libdeflate::libdeflate_shared,libdeflate::libdeflate_static>
)

if(MSVC)
    set_property(TARGET LiteDraw PROPERTY VS_DPI_AWARE "PerMonitor")
    target_sources(LiteDraw PRIVATE src/LiteDraw.manifest)
    set_property(TARGET LiteDraw PROPERTY VS_GLOBAL_ApplicationManifest "${CMAKE_CURRENT_SOURCE_DIR}/src/LiteDraw.manifest")
endif()

target_include_directories(LiteDraw PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
)

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT LiteDraw)
