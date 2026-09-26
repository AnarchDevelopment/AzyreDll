cmake_minimum_required(VERSION 3.25)
project(AzyreSDK LANGUAGES C CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The SDK only supports x64 (Minecraft Bedrock Win10 DX11 is x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/heads/master.tar.gz
    SOURCE_SUBDIR __unused__
)

FetchContent_Declare(
    minhook
    URL https://github.com/TsudaKageyu/minhook/archive/refs/heads/master.tar.gz
    SOURCE_SUBDIR __unused__
)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/archive/refs/heads/develop.tar.gz
)

FetchContent_MakeAvailable(imgui minhook json)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

set(MINHOOK_SOURCES
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c
)

file(GLOB_RECURSE AZYRE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(AzyreSDK SHARED
    ${AZYRE_SOURCES}
    ${IMGUI_SOURCES}
    ${MINHOOK_SOURCES}
)

target_sources(AzyreSDK PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/resource.rc")

set(_kits_include "")
if(EXISTS "C:/Program Files (x86)/Windows Kits/10/Include")
    file(GLOB _kit_versions LIST_DIRECTORIES true "C:/Program Files (x86)/Windows Kits/10/Include/*")
    list(SORT _kit_versions)
    list(REVERSE _kit_versions)
    foreach(_v IN LISTS _kit_versions)
        if(EXISTS "${_v}/cppwinrt/winrt/base.h")
            set(_kits_include "${_v}/cppwinrt")
            break()
        endif()
    endforeach()
endif()
if(NOT _kits_include)
    get_filename_component(_kits_reg "[HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v10.0;InstallationFolder]" ABSOLUTE)
    if(EXISTS "${_kits_reg}")
        file(GLOB _kit_versions LIST_DIRECTORIES true "${_kits_reg}Include/*")
        list(SORT _kit_versions)
        list(REVERSE _kit_versions)
        foreach(_v IN LISTS _kit_versions)
            if(EXISTS "${_v}/cppwinrt/winrt/base.h")
                set(_kits_include "${_v}/cppwinrt")
                break()
            endif()
        endforeach()
    endif()
endif()

target_include_directories(AzyreSDK PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${minhook_SOURCE_DIR}/include
    ${minhook_SOURCE_DIR}/src
    ${minhook_SOURCE_DIR}/src/hde
)
if(_kits_include)
    target_include_directories(AzyreSDK PRIVATE "${_kits_include}")
else()
    message(WARNING "Cannot find cppwinrt directory of Windows SDK; WinRT input may fail to compile.")
endif()

target_compile_definitions(AzyreSDK PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _USE_MATH_DEFINES
)

target_compile_options(AzyreSDK PRIVATE /utf-8 /MP /W3)

target_precompile_headers(AzyreSDK PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.hpp>"
)

target_link_libraries(AzyreSDK PRIVATE
    d3d11
    dxgi
    user32
    shell32
    ole32
    windowsapp
    nlohmann_json::nlohmann_json
)

set_target_properties(AzyreSDK PROPERTIES OUTPUT_NAME "AzyreSDK")
