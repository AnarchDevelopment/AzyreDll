/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include "ModuleHeader.hpp"
#include "ImGui/imgui.h"
#include <cstdint>

class Module {
public:
    static void Initialize(uintptr_t gameBase, size_t imageSize, HudElement* renderInfoHud, HudElement* watermarkHud, HudElement* keystrokesHud, HudElement* cpsHud, HudElement* fpsOverlayHud, HudElement* pingHud, HudElement* playerInfoHud);
    static void UpdateAnimation(unsigned long long now);
    static void RenderDisplay(float sw, float sh);
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);
};
