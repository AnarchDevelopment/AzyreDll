#pragma once

#include <imgui.h>

namespace mc::theme {

inline ImVec4 Accent(0.84f, 0.86f, 0.90f, 1.0f);

inline ImU32 AccentU32(float alpha = 1.0f)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(Accent.x, Accent.y, Accent.z, alpha));
}

inline void apply()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 3.0f;
    s.ChildRounding = 0.0f;
    s.FrameRounding = 2.0f;
    s.GrabRounding = 2.0f;
    s.ScrollbarRounding = 2.0f;
    s.PopupRounding = 4.0f;
    s.TabRounding = 2.0f;
    s.WindowPadding = ImVec2(12.0f, 10.0f);
    s.FramePadding = ImVec2(9.0f, 5.0f);
    s.ItemSpacing = ImVec2(9.0f, 6.0f);
    s.ItemInnerSpacing = ImVec2(7.0f, 4.0f);
    s.ScrollbarSize = 11.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.55f, 0.60f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.065f, 0.080f, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.070f, 0.080f, 0.095f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(Accent.x, Accent.y, Accent.z, 0.35f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.110f, 0.125f, 0.150f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.155f, 0.175f, 0.210f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.190f, 0.215f, 0.250f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.065f, 0.080f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.075f, 0.088f, 0.105f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.055f, 0.065f, 0.080f, 1.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.075f, 0.088f, 0.105f, 1.00f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.055f, 0.065f, 0.60f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.33f, 0.38f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = Accent;
    c[ImGuiCol_CheckMark] = Accent;
    c[ImGuiCol_SliderGrab] = Accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(Accent.x, Accent.y, Accent.z, 0.80f);
    c[ImGuiCol_Button] = ImVec4(0.130f, 0.148f, 0.175f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.170f, 0.200f, 0.230f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(Accent.x, Accent.y, Accent.z, 0.55f);
    c[ImGuiCol_Header] = ImVec4(0.145f, 0.155f, 0.175f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(Accent.x, Accent.y, Accent.z, 0.30f);
    c[ImGuiCol_HeaderActive] = ImVec4(Accent.x, Accent.y, Accent.z, 0.48f);
    c[ImGuiCol_Separator] = ImVec4(0.19f, 0.21f, 0.25f, 0.80f);
    c[ImGuiCol_SeparatorHovered] = Accent;
    c[ImGuiCol_SeparatorActive] = Accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.19f, 0.21f, 0.25f, 0.60f);
    c[ImGuiCol_ResizeGripHovered] = Accent;
    c[ImGuiCol_ResizeGripActive] = Accent;
    c[ImGuiCol_Tab] = ImVec4(0.110f, 0.125f, 0.150f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(Accent.x, Accent.y, Accent.z, 0.40f);
    c[ImGuiCol_TabActive] = ImVec4(Accent.x, Accent.y, Accent.z, 0.28f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.080f, 0.090f, 0.105f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.100f, 0.115f, 0.135f, 1.00f);
    c[ImGuiCol_PlotLines] = Accent;
    c[ImGuiCol_PlotHistogram] = Accent;
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.11f, 0.125f, 0.15f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.19f, 0.21f, 0.25f, 1.00f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.16f, 0.19f, 1.00f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(Accent.x, Accent.y, Accent.z, 0.35f);
    c[ImGuiCol_DragDropTarget] = Accent;
    c[ImGuiCol_NavHighlight] = Accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}

}
