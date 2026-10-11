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

FetchContent_Declare(
    luau
    URL https://github.com/luau-lang/luau/archive/refs/heads/master.tar.gz
)

FetchContent_Declare(
    soloud
    URL https://github.com/jarikomppa/soloud/archive/refs/heads/master.tar.gz
)

FetchContent_Declare(
    entt
    URL https://github.com/skypjack/entt/archive/refs/heads/master.tar.gz
)

# Luau: solo librerias (VM + Compiler), sin herramientas de desarrollo.
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_FUZZER OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_ANALYSIS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(imgui minhook json luau soloud entt)

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

set(SOLOUD_SOURCES
    ${soloud_SOURCE_DIR}/src/core/soloud.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_audiosource.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_bus.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_3d.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_basicops.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_faderops.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_filterops.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_getters.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_setters.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_voicegroup.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_voiceops.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_fader.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_fft.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_fft_lut.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_file.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_filter.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_misc.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_queue.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_thread.cpp
    ${soloud_SOURCE_DIR}/src/audiosource/wav/soloud_wav.cpp
    ${soloud_SOURCE_DIR}/src/audiosource/wav/soloud_wavstream.cpp
    ${soloud_SOURCE_DIR}/src/audiosource/wav/dr_impl.cpp
    ${soloud_SOURCE_DIR}/src/audiosource/wav/stb_vorbis.c
    ${soloud_SOURCE_DIR}/src/backend/winmm/soloud_winmm.cpp
)

file(GLOB_RECURSE AZYRE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(AzyreSDK SHARED
    ${AZYRE_SOURCES}
    ${IMGUI_SOURCES}
    ${MINHOOK_SOURCES}
    ${SOLOUD_SOURCES}
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
    ${luau_SOURCE_DIR}/VM/include
    ${luau_SOURCE_DIR}/Compiler/include
    ${soloud_SOURCE_DIR}/include
)
if(_kits_include)
    target_include_directories(AzyreSDK PRIVATE "${_kits_include}")
else()
    message(WARNING "Cannot find cppwinrt directory of Windows SDK; WinRT input may fail to compile.")
endif()

target_compile_definitions(AzyreSDK PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    UNICODE
    _UNICODE
    _USE_MATH_DEFINES
    WITH_WINMM
)

# Flags por compilador:
#   - cl.exe:      /utf-8 + /MP (paralelo) + /W3
#   - clang-cl:    entiende /utf-8 y /W3, pero NO /MP (clang-cl lo rechaza)
#   - clang nativo / gcc: flags POSIX (-Wall ...) en vez de /x
set(AZYRE_FLAGS "")
if(MSVC)
    set(AZYRE_FLAGS "/utf-8" "/W3")
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        list(APPEND AZYRE_FLAGS "/MP")
    endif()
else()
    set(AZYRE_FLAGS "-Wall" "-Wextra" "-Wno-unused-parameter" "-Wno-unused-but-set-variable")
    # Compat.hpp SIEMPRE disponible en toolchains no-MSVC: el PCH de CMake no
    # se genera de forma fiable para GCC con genex, asi que se fuerza -include
    # (Compat es self-contained: incluye windows.h y remapea __try/__except).
    list(APPEND AZYRE_FLAGS "-include" "${CMAKE_CURRENT_SOURCE_DIR}/src/Framework/Compat.hpp")
endif()
target_compile_options(AzyreSDK PRIVATE ${AZYRE_FLAGS})

target_precompile_headers(AzyreSDK PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.hpp>"
)

# Directorio de import libs del Windows SDK (um/x64). MinGW no trae
# d3dcompiler_47.a: se apunta a d3dcompiler.lib del SDK (COFF import lib que
# ld de MinGW linkea via ruta/full path o -L).
set(_kits_lib_dir "")
if(EXISTS "C:/Program Files (x86)/Windows Kits/10/Lib")
    file(GLOB _kit_lib_versions LIST_DIRECTORIES true "C:/Program Files (x86)/Windows Kits/10/Lib/*")
    list(SORT _kit_lib_versions)
    list(REVERSE _kit_lib_versions)
    foreach(_l IN LISTS _kit_lib_versions)
        if(EXISTS "${_l}/um/x64")
            set(_kits_lib_dir "${_l}/um/x64")
            break()
        endif()
    endforeach()
endif()

# Librerias del sistema: nombres de import lib dependen del toolchain.
#   - MSVC:    d3dcompiler + windowsapp (WinRT)
#   - Clang/l: el d3dcompiler del SDK suele llamarse d3dcompiler_47 y
#              windowsapp.lib no existe → hay que encontrar la lib o dejarla
#              fuera si el SDK no la provee.
set(AZYRE_SYSTEM_LIBS d3d11 dxgi user32 shell32 ole32 winmm ws2_32 winhttp)
if(MSVC)
    list(APPEND AZYRE_SYSTEM_LIBS d3dcompiler windowsapp)
else()
    target_link_directories(AzyreSDK PRIVATE ${_kits_lib_dir})
    find_library(AZYRE_D3DCOMPILER_LIB NAMES d3dcompiler_47 d3dcompiler
                 HINTS ${_kits_lib_dir})
    if(AZYRE_D3DCOMPILER_LIB)
        list(APPEND AZYRE_SYSTEM_LIBS ${AZYRE_D3DCOMPILER_LIB})
    endif()
endif()

target_link_libraries(AzyreSDK PRIVATE
    ${AZYRE_SYSTEM_LIBS}
    nlohmann_json::nlohmann_json
    Luau.Compiler
    Luau.VM
    EnTT::EnTT
)

# Toolchains no-MSVC (GCC/MinGW): DLL autocontenida — no depender de
# libgcc/libstdc++/winpthreads dinámicas en el target.
if(NOT MSVC)
    target_link_options(AzyreSDK PRIVATE -static -static-libgcc)
endif()

set_target_properties(AzyreSDK PROPERTIES OUTPUT_NAME "AzyreSDK")
