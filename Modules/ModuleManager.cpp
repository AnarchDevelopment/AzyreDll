/*
Under an4rch Development Public Source License 1.0
*/

#include "ModuleManager.hpp"
#include "../ArrayList/ArrayList.hpp"

void Module::Initialize(uintptr_t gameBase, HudElement* renderInfoHud, HudElement* watermarkHud, HudElement* keystrokesHud, HudElement* cpsHud, HudElement* fpsOverlayHud) {
    Reach::Initialize(gameBase);
    Hitbox::Initialize(gameBase);
    Timer::Initialize(gameBase);
    FullBright::Initialize(gameBase);
    RenderInfo::Initialize(renderInfoHud);
    Watermark::Initialize(watermarkHud);
    Keystrokes::Initialize(keystrokesHud);
    CPSCounter::Initialize(cpsHud);
    FPSOverlay::Initialize(fpsOverlayHud);
    Terminal::Initialize();
    Info::Initialize();
    UnlockFPS::Initialize();
}

void Module::UpdateAnimation(unsigned long long now) {
    RenderInfo::UpdateFPS();
    RenderInfo::UpdateAnimation(now);
    MotionBlur::UpdateAnimation(now);
    Keystrokes::UpdateAnimation(now);
    Watermark::UpdateAnimation(now);
    FPSOverlay::UpdateAnimation(now);
}

void Module::RenderDisplay(float sw, float sh) {
    Watermark::RenderDisplay();
    Keystrokes::RenderDisplay(sw, sh);
    RenderInfo::RenderWindow();
    CPSCounter::RenderDisplay(sw, sh);
    FPSOverlay::RenderDisplay((int)sw, (int)sh);
    
    // Call new centralized ArrayList
    ArrayList::Render();
}

void Module::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    // Legacy function, now handled inside RenderDisplay -> ArrayList::Render()
}
