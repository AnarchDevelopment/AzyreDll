/*
Under an4rch Development Public Source License 1.0
*/

#include "ModuleManager.hpp"
#include "../ArrayList/ArrayList.hpp"

void Module::Initialize(uintptr_t gameBase, size_t imageSize, HudElement* renderInfoHud, HudElement* watermarkHud, HudElement* keystrokesHud, HudElement* cpsHud, HudElement* fpsOverlayHud, HudElement* pingHud, HudElement* playerInfoHud, HudElement* mouseStrokesHud) {
    Reach::Initialize(gameBase);
    Hitbox::Initialize(gameBase);
    RapidHit::Initialize(gameBase);
    Timer::Initialize(gameBase);
    FullBright::Initialize(gameBase);
    RenderInfo::Initialize(renderInfoHud);
    Watermark::Initialize(watermarkHud);
    Keystrokes::Initialize(keystrokesHud);
    CPSCounter::Initialize(cpsHud);
    FPSOverlay::Initialize(fpsOverlayHud);
    PingCounter::Initialize(pingHud);
    PlayerInfo::Initialize(playerInfoHud);
    MouseStrokes::Initialize(mouseStrokesHud);
    Terminal::Initialize();
    Info::Initialize();
    UnlockFPS::Initialize();
    ClickGUI::Initialize();
    
    // Initialize new Misc modules
    AutoClicker::Initialize();
    AntiAFK::Initialize();
    Screenshot::Initialize();
    NoHurtCam::Initialize(gameBase);
    HighJump::Initialize(gameBase);

    // Resolve the patch targets once (game module memory)
    AutoSprint::ScanPattern(gameBase, imageSize);
    FullBright::ScanPattern(gameBase, imageSize);
    Glide::ScanPattern(gameBase, imageSize);
    Fly::ScanPattern(gameBase, imageSize);
    NoHurtCam::ScanPattern(gameBase, imageSize);
    RapidHit::ScanPattern(gameBase, imageSize);
    HighJump::ScanPattern(gameBase, imageSize);
}

void Module::UpdateAnimation(unsigned long long now) {
    RenderInfo::UpdateFPS();
    RenderInfo::UpdateAnimation(now);
    MotionBlur::UpdateAnimation(now);
    Keystrokes::UpdateAnimation(now);
    Watermark::UpdateAnimation(now);
    FPSOverlay::UpdateAnimation(now);
    PingCounter::UpdateAnimation(now);
    PingCounter::UpdatePing(now);
    PlayerInfo::UpdateAnimation(now);
    MouseStrokes::UpdateAnimation(now);
    
    // Tick background modules
    AutoClicker::Tick();
    AntiAFK::Tick();
}

void Module::RenderDisplay(float sw, float sh) {
    Watermark::RenderDisplay();
    Keystrokes::RenderDisplay(sw, sh);
    RenderInfo::RenderWindow();
    CPSCounter::RenderDisplay((int)sw, (int)sh);
    FPSOverlay::RenderDisplay((int)sw, (int)sh);
    PingCounter::RenderDisplay(sw, sh);
    PlayerInfo::RenderDisplay();
    MouseStrokes::RenderDisplay(sw, sh);
    
    // Call new centralized ArrayList
    ArrayList::Render();
}

void Module::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    // Legacy function, now handled inside RenderDisplay -> ArrayList::Render()
}
