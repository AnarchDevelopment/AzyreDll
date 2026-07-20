#pragma once

#include "../ImGui/imgui.h"

/// @brief HUD Element for draggable UI components
struct HudElement {
    ImVec2 pos;
    ImVec2 size;

    bool dragging = false;
    ImVec2 dragOffset;

    void HandleDrag(bool menuOpen) {
        if (!menuOpen) return;

        ImVec2 mouse = ImGui::GetIO().MousePos;

        bool hover =
            mouse.x >= pos.x && mouse.x <= pos.x + size.x &&
            mouse.y >= pos.y && mouse.y <= pos.y + size.y;

        if (hover && ImGui::IsMouseClicked(0)) {
            dragging = true;
            dragOffset = ImVec2(mouse.x - pos.x, mouse.y - pos.y);
        }

        if (!ImGui::IsMouseDown(0))
            dragging = false;

        if (dragging) {
            pos = ImVec2(mouse.x - dragOffset.x, mouse.y - dragOffset.y);
        }
    }

    void ClampToScreen() {
        ImVec2 screen = ImGui::GetIO().DisplaySize;

        if (pos.x < 0) pos.x = 0;
        if (pos.y < 0) pos.y = 0;

        if (pos.x + size.x > screen.x)
            pos.x = screen.x - size.x;

        if (pos.y + size.y > screen.y)
            pos.y = screen.y - size.y;
    }
};
