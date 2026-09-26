#pragma once

#include <imgui.h>

namespace mc::theme {

// WinUI 3 Dark: SystemAccentColorLight (#4CC2FF) y accent base (#0078D4).
inline ImVec4 Accent(0.298f, 0.761f, 1.000f, 1.0f);
inline ImVec4 AccentDeep(0.000f, 0.471f, 0.831f, 1.0f);

inline ImU32 AccentU32(float alpha = 1.0f)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(Accent.x, Accent.y, Accent.z, alpha));
}

inline ImU32 AccentDeepU32(float alpha = 1.0f)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(AccentDeep.x, AccentDeep.y, AccentDeep.z, alpha));
}

inline void apply()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 8.0f;
    s.ChildRounding = 4.0f;
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.ScrollbarRounding = 3.0f;
    s.PopupRounding = 8.0f;
    s.TabRounding = 4.0f;
    s.WindowPadding = ImVec2(12.0f, 10.0f);
    s.FramePadding = ImVec2(9.0f, 5.0f);
    s.ItemSpacing = ImVec2(9.0f, 6.0f);
    s.ItemInnerSpacing = ImVec2(7.0f, 4.0f);
    s.ScrollbarSize = 6.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 0.0f;
    s.FrameBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* c = s.Colors;

    // Texto: blanco con opacidades oficiales de WinUI (100% / 61.4% / 36%).
    c[ImGuiCol_Text] = ImVec4(0.953f, 0.953f, 0.961f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.614f, 0.614f, 0.627f, 1.00f);

    // Fondos: SolidBackgroundFillColorBase (#1F1F20) y Layer (#2B2B2D).
    // Alpha bajo en la ventana: el backdrop acrilico (blur) se ve a traves.
    c[ImGuiCol_WindowBg] = ImVec4(0.122f, 0.122f, 0.125f, 0.70f);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.169f, 0.169f, 0.176f, 0.98f);

    // Bordes: ControlElevationBorderBrush = blanco ~9%.
    c[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.09f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Controles: rellenos "subtle" de WinUI (blanco 6/10/14% sobre #1F1F20).
    c[ImGuiCol_FrameBg] = ImVec4(0.176f, 0.176f, 0.180f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.211f, 0.212f, 0.216f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.247f, 0.247f, 0.251f, 1.00f);

    c[ImGuiCol_TitleBg] = ImVec4(0.122f, 0.122f, 0.125f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.145f, 0.145f, 0.149f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.122f, 0.122f, 0.125f, 1.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.145f, 0.145f, 0.149f, 1.00f);

    // Scrollbar overlay: fino y translucido.
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.24f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.36f);
    c[ImGuiCol_ScrollbarGrabActive] = Accent;

    c[ImGuiCol_CheckMark] = Accent;
    c[ImGuiCol_SliderGrab] = Accent;
    c[ImGuiCol_SliderGrabActive] = Accent;

    // Botones: subtle por defecto, accent-deep al presionar (#005FB8-ish).
    c[ImGuiCol_Button] = ImVec4(0.176f, 0.176f, 0.180f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.216f, 0.216f, 0.220f, 1.00f);
    c[ImGuiCol_ButtonActive] = AccentDeep;

    c[ImGuiCol_Header] = ImVec4(0.169f, 0.169f, 0.173f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(Accent.x, Accent.y, Accent.z, 0.18f);
    c[ImGuiCol_HeaderActive] = ImVec4(Accent.x, Accent.y, Accent.z, 0.32f);

    c[ImGuiCol_Separator] = ImVec4(1.00f, 1.00f, 1.00f, 0.083f);
    c[ImGuiCol_SeparatorHovered] = Accent;
    c[ImGuiCol_SeparatorActive] = Accent;

    c[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(Accent.x, Accent.y, Accent.z, 0.40f);
    c[ImGuiCol_ResizeGripActive] = Accent;

    c[ImGuiCol_Tab] = ImVec4(0.176f, 0.176f, 0.180f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(Accent.x, Accent.y, Accent.z, 0.24f);
    c[ImGuiCol_TabActive] = ImVec4(Accent.x, Accent.y, Accent.z, 0.20f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.145f, 0.145f, 0.149f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.180f, 0.180f, 0.184f, 1.00f);

    c[ImGuiCol_PlotLines] = Accent;
    c[ImGuiCol_PlotHistogram] = Accent;
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.169f, 0.169f, 0.173f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
    c[ImGuiCol_TableBorderLight] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
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
