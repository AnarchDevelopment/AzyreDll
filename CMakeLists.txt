cmake_minimum_required(VERSION 3.20)

project(Azyre
    VERSION 1.0.9
    LANGUAGES C CXX RC
)

# Standard C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Get git commit hash at configure time
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()
add_compile_definitions(GIT_COMMIT_HASH="${GIT_COMMIT_HASH}")

# Source files
set(SOURCES
    dllmain.cpp

    # Animations
    Animations/Animations.cpp

    # ArrayList
    ArrayList/ArrayList.cpp

    # Assets & stb implementations
    Assets/stb/stb_image_impl.cpp
    Assets/stb/stb_imgwrt_impl.cpp
    Assets/resources.rc

    # Config
    Config/ConfigManager.cpp

    # Core
    Core/Present.cpp

    # GUI
    GUI/GUI.cpp
    GUI/DX11/ImGuiRenderer.cpp

    # Hook
    Hook/Hooking.cpp

    # ImGui core & backends
    ImGui/imgui.cpp
    ImGui/imgui_demo.cpp
    ImGui/imgui_draw.cpp
    ImGui/imgui_tables.cpp
    ImGui/imgui_widgets.cpp
    ImGui/backend/imgui_impl_dx11.cpp
    ImGui/backend/imgui_impl_win32.cpp

    # Input
    Input/Input.cpp

    # MinHook
    MinHook/buffer.c
    MinHook/hde32.c
    MinHook/hde64.c
    MinHook/hook.c
    MinHook/trampoline.c

    # Modules
    Modules/Globals.cpp
    Modules/ModuleManager.cpp
    Modules/Alloc/AllocateNear.cpp
    Modules/Combat/Hitbox/Hitbox.cpp
    Modules/Combat/Reach/Reach.cpp
    Modules/Combat/RapidHit/RapidHit.cpp
    Modules/Info/Info.cpp
    Modules/Misc/AntiAFK/AntiAFK.cpp
    Modules/Misc/AutoClicker/AutoClicker.cpp
    Modules/Misc/Screenshot/Screenshot.cpp
    Modules/Misc/UnlockFPS/UnlockFPS.cpp
    Modules/Movement/AutoSprint/AutoSprint.cpp
    Modules/Movement/Fly/Fly.cpp
    Modules/Movement/Glide/Glide.cpp
    Modules/Movement/HighJump/HighJump.cpp
    Modules/Movement/Timer/Timer.cpp
    Modules/PatternScan/PatternScan.cpp
    Modules/Splash/Splash.cpp
    Modules/Terminal/Terminal.cpp
    Modules/Visuals/ClickGUI/ClickGUI.cpp
    Modules/Visuals/CPSCounter/CPSCounter.cpp
    Modules/Visuals/FPSOverlay/FPSOverlay.cpp
    Modules/Visuals/FullBright/FullBright.cpp
    Modules/Visuals/Keystrokes/Keystrokes.cpp
    Modules/Visuals/MotionBlur/MotionBlur.cpp
    Modules/Visuals/PingCounter/PingCounter.cpp
    Modules/Visuals/PlayerInfo/PlayerInfo.cpp
    Modules/Visuals/MouseStrokes/MouseStrokes.cpp
    Modules/Visuals/RenderInfo/RenderInfo.cpp
    Modules/Visuals/Watermark/Watermark.cpp
    Modules/Visuals/NoHurtCam/NoHurtCam.cpp

    # Networking
    Networking/IRChat.cpp
    Networking/Client/IRCClient.cpp

    # Utils
    Utils/WinRTTitle.cpp
)

# Header files (for IDE project generators)
set(HEADERS
    Animations/Animations.hpp
    ArrayList/ArrayList.hpp
    Assets/resource.h
    Assets/stb/stb_image.h
    Assets/stb/stb_image_write.h
    Config/ConfigManager.hpp
    Core/Present.hpp
    GUI/GUI.hpp
    GUI/DX11/ImGuiRenderer.hpp
    Hook/Hook.hpp
    ImGui/imconfig.h
    ImGui/imgui.h
    ImGui/imgui_internal.h
    ImGui/imstb_rectpack.h
    ImGui/imstb_textedit.h
    ImGui/imstb_truetype.h
    ImGui/backend/imgui_impl_dx11.h
    ImGui/backend/imgui_impl_win32.h
    ImGui/imgui-markdown/imgui-markdown.h
    Input/Input.hpp
    MinHook/buffer.h
    MinHook/hde32.h
    MinHook/hde64.h
    MinHook/MinHook.h
    MinHook/pstdint.h
    MinHook/table32.h
    MinHook/table64.h
    MinHook/trampoline.h
    miniaudio/miniaudio.h
    Modules/Alloc/AllocateNear.hpp
    Modules/Combat/Hitbox/Hitbox.hpp
    Modules/Combat/Reach/Reach.hpp
    Modules/Combat/RapidHit/RapidHit.hpp
    Modules/Globals.hpp
    Modules/Info/Info.hpp
    Modules/Misc/AntiAFK/AntiAFK.hpp
    Modules/Misc/AutoClicker/AutoClicker.hpp
    Modules/Misc/Screenshot/Screenshot.hpp
    Modules/Misc/UnlockFPS/UnlockFPS.hpp
    Modules/ModuleHeader.hpp
    Modules/ModuleManager.hpp
    Modules/Movement/AutoSprint/AutoSprint.hpp
    Modules/Movement/Glide/Glide.hpp
    Modules/Movement/HighJump/HighJump.hpp
    Modules/Movement/Fly/Fly.hpp
    Modules/Movement/Timer/Timer.hpp
    Modules/PatternScan/PatternScan.hpp
    Modules/Splash/Splash.hpp
    Modules/Terminal/Terminal.hpp
    Modules/Visuals/ClickGUI/ClickGUI.hpp
    Modules/Visuals/CPSCounter/CPSCounter.hpp
    Modules/Visuals/FPSOverlay/FPSOverlay.hpp
    Modules/Visuals/FullBright/FullBright.hpp
    Modules/Visuals/Keystrokes/Helper/HelperFunctions.hpp
    Modules/Visuals/Keystrokes/Keystrokes.hpp
    Modules/Visuals/MotionBlur/MotionBlur.hpp
    Modules/Visuals/PingCounter/PingCounter.hpp
    Modules/Visuals/PlayerInfo/PlayerInfo.hpp
    Modules/Visuals/MouseStrokes/MouseStrokes.hpp
    Modules/Visuals/RenderInfo/RenderInfo.hpp
    Modules/Visuals/Watermark/Watermark.hpp
    Modules/Visuals/NoHurtCam/NoHurtCam.hpp
    Networking/Client/IRCClient.hpp
    Networking/IRChat.hpp
    nlohmann/json.hpp
    Utils/HudElement.hpp
    Utils/WinRTTitle.hpp
)

add_library(${PROJECT_NAME} SHARED
    ${SOURCES}
    ${HEADERS}
)

# Output name
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "Azyre"
    PREFIX ""
)

# Include Directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Animations
    ${CMAKE_CURRENT_SOURCE_DIR}/ArrayList
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/GUI
    ${CMAKE_CURRENT_SOURCE_DIR}/GUI/DX11
    ${CMAKE_CURRENT_SOURCE_DIR}/Hook
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGui
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/backend
    ${CMAKE_CURRENT_SOURCE_DIR}/Input
    ${CMAKE_CURRENT_SOURCE_DIR}/MinHook
    ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules
    ${CMAKE_CURRENT_SOURCE_DIR}/Networking
    ${CMAKE_CURRENT_SOURCE_DIR}/Networking/Client
    ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann
    ${CMAKE_CURRENT_SOURCE_DIR}/Utils
)

# Preprocessor Definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    _CONSOLE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

# MSVC specific compiler options
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )

    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded"
    )
endif()

# Resource compiler include path so rc.exe finds Assets files
set_source_files_properties(Assets/resources.rc PROPERTIES
    COMPILE_FLAGS "/I \"${CMAKE_CURRENT_SOURCE_DIR}/Assets\" /I \"${CMAKE_CURRENT_SOURCE_DIR}\""
)

# Link Libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
    d3dcompiler
    dxgi
    ws2_32
    winmm
    shell32
    ole32
    winhttp
    WindowsApp
)
