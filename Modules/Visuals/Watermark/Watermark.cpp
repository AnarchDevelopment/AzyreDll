/*
Under an4rch Development Public Source License 1.0
*/

#include "Watermark.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../Utils/GradientText.hpp"
#include "../../../Utils/HudElement.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../ImGui/backend/imgui_impl_dx11.h"
#include "../../../GUI/GUI.hpp"
#include "../../../Assets/stb/stb_image.h"
#include "../../../Assets/resource.h"
#include <d3d11.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// External globals
extern ID3D11Device* pDevice;
extern ID3D11DeviceContext* pContext;
extern HMODULE g_hModule;

// Static member initialization
bool Watermark::g_showWatermark = true;
ULONGLONG Watermark::g_watermarkEnableTime = 0;
ULONGLONG Watermark::g_watermarkDisableTime = 0;
float Watermark::g_watermarkAnim = 1.0f;
HudElement* Watermark::g_watermarkHud = nullptr;

bool Watermark::g_useImage = false;
std::string Watermark::g_fontName = "Default";
char Watermark::g_customText[128] = "Azyre";
bool Watermark::g_showGlow = true;
bool Watermark::g_chromaText = true;
ImVec4 Watermark::g_staticColor = ImVec4(1.0f, 0.4f, 0.8f, 1.0f);
float Watermark::g_fontSize = 32.0f;
float Watermark::g_bgOpacity = 0.5f;
bool Watermark::g_showBackground = false;
bool Watermark::g_showShimmer = false;
float Watermark::g_chromaSpeed = 1.0f;
bool Watermark::g_chromaDirection = true;
bool Watermark::g_mirroredGradient = true;
bool Watermark::g_edgeFade = false;

std::vector<ImVec4> Watermark::g_chromaColors = {
    ImVec4(1.0f, 0.4f, 0.8f, 1.0f), // Pink
    ImVec4(0.6f, 0.5f, 1.0f, 1.0f), // Purple-ish
    ImVec4(0.4f, 0.8f, 1.0f, 1.0f)  // Sky Blue
};
float Watermark::g_imageOpacity = 1.0f;
float Watermark::g_imageSize = 50.0f;

int Watermark::g_chromaPreset = 1; // Default to Rainbow
ImVec4 Watermark::g_customColors[4] = {
    ImVec4(1.0f, 0.4f, 0.8f, 1.0f),
    ImVec4(0.6f, 0.5f, 1.0f, 1.0f),
    ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
    ImVec4(1.0f, 0.6f, 0.3f, 1.0f)
};
int Watermark::g_customColorCount = 4;
float Watermark::g_chromaAngle = 0.0f;
float Watermark::g_chromaSaturation = 1.0f;
bool Watermark::g_chromaLinear = false;

int Watermark::g_animStyle = 0;
float Watermark::g_slideOffset = 40.0f;

int Watermark::g_snapCorner = 0;
float Watermark::g_snapPadding = 10.0f;

bool Watermark::g_showOutline = false;
ImVec4 Watermark::g_outlineColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
float Watermark::g_outlineWidth = 1.5f;

ImVec4 Watermark::g_bgColor = ImVec4(0.02f, 0.02f, 0.04f, 1.0f);
float Watermark::g_bgRadius = 5.0f;
float Watermark::g_bgPadX = 10.0f;
float Watermark::g_bgPadY = 5.0f;

void* Watermark::g_watermarkTexture = nullptr;
int Watermark::g_texWidth = 0;
int Watermark::g_texHeight = 0;

struct WatermarkPreset {
    const char* name;
    std::vector<ImVec4> colors;
};

static const WatermarkPreset g_wmPresets[] = {
    { "Custom",     {} },
    { "Rainbow",    { ImVec4(1,0,0,1), ImVec4(1,0.5f,0,1), ImVec4(1,1,0,1),
                      ImVec4(0,1,0,1), ImVec4(0,0,1,1), ImVec4(0.5f,0,1,1) } },
    { "Poison",     { ImVec4(0,0.9f,0.3f,1), ImVec4(0.2f,0.8f,0.1f,1),
                      ImVec4(0.6f,0,0.8f,1), ImVec4(0.1f,0.9f,0.4f,1) } },
    { "Bubblegum",  { ImVec4(1,0.4f,0.7f,1), ImVec4(0.9f,0.3f,0.9f,1),
                      ImVec4(0.5f,0.2f,1,1), ImVec4(1,0.6f,0.8f,1) } },
    { "Cute",       { ImVec4(1,0.5f,0.7f,1), ImVec4(1,0.7f,0.8f,1),
                      ImVec4(0.8f,0.5f,1,1), ImVec4(0.6f,0.8f,1,1) } },
    { "Sunset",     { ImVec4(1,0.2f,0,1), ImVec4(1,0.6f,0,1),
                      ImVec4(1,0.9f,0,1), ImVec4(0.8f,0,0.5f,1) } },
    { "Ocean",      { ImVec4(0,0.4f,0.8f,1), ImVec4(0,0.7f,0.9f,1),
                      ImVec4(0,1,1,1), ImVec4(0.2f,0.6f,0.9f,1) } },
    { "Fire",       { ImVec4(1,0,0,1), ImVec4(1,0.4f,0,1),
                      ImVec4(1,0.8f,0,1), ImVec4(1,0.2f,0,1) } },
    { "Frost",      { ImVec4(0.7f,0.9f,1,1), ImVec4(0.4f,0.7f,1,1),
                      ImVec4(0.8f,0.95f,1,1), ImVec4(0.5f,0.8f,1,1) } },
    { "Neon",       { ImVec4(1,0,0.5f,1), ImVec4(0,1,0.5f,1),
                      ImVec4(0.5f,0,1,1), ImVec4(1,1,0,1) } },
    { "Pastel",     { ImVec4(1,0.7f,0.7f,1), ImVec4(0.7f,1,0.7f,1),
                      ImVec4(0.7f,0.7f,1,1), ImVec4(1,1,0.7f,1) } },
    { "Lavender",   { ImVec4(0.7f,0.5f,1,1), ImVec4(0.9f,0.6f,1,1),
                      ImVec4(0.5f,0.3f,0.9f,1), ImVec4(0.8f,0.7f,1,1) } },
};
static const int kWmPresetCount = sizeof(g_wmPresets) / sizeof(g_wmPresets[0]);

std::vector<ImVec4> Watermark::GetChromaColors() {
    if (g_chromaPreset == 0) {
        std::vector<ImVec4> cols;
        for (int i = 0; i < g_customColorCount && i < 4; i++)
            cols.push_back(g_customColors[i]);
        if (cols.empty()) cols.push_back(ImVec4(1, 1, 1, 1));
        return cols;
    }
    if (g_chromaPreset >= 0 && g_chromaPreset < kWmPresetCount)
        return g_wmPresets[g_chromaPreset].colors;
    return g_wmPresets[1].colors;
}

void Watermark::Initialize(HudElement* hud) {
    g_watermarkHud = hud;
}

bool Watermark::InitializeTextures() {
    if (g_watermarkTexture) return true;
    
    HRSRC hRes = FindResource(g_hModule, MAKEINTRESOURCE(IDR_WATERMARK_IMAGE), RT_RCDATA);
    if (!hRes) return false;
    
    HGLOBAL hGlobal = LoadResource(g_hModule, hRes);
    if (!hGlobal) return false;
    
    void* pData = LockResource(hGlobal);
    DWORD size = SizeofResource(g_hModule, hRes);
    
    int width, height, channels;
    unsigned char* pixels = stbi_load_from_memory((unsigned char*)pData, size, &width, &height, &channels, 4);
    if (!pixels) return false;
    
    g_texWidth = width;
    g_texHeight = height;
    
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    
    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = pDevice->CreateTexture2D(&desc, nullptr, &pTexture);
    
    if (SUCCEEDED(hr) && pTexture) {
        pContext->UpdateSubresource(pTexture, 0, nullptr, pixels, width * 4, 0);
        stbi_image_free(pixels);
        ID3D11ShaderResourceView* pSRV = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = UINT(-1);
        hr = pDevice->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
        pTexture->Release();
        if (SUCCEEDED(hr)) {
            pContext->GenerateMips(pSRV);
            g_watermarkTexture = (void*)pSRV;
            return true;
        }
    } else {
        stbi_image_free(pixels);
    }
    return false;
}

void Watermark::Shutdown() {
    if (g_watermarkTexture) {
        ((ID3D11ShaderResourceView*)g_watermarkTexture)->Release();
        g_watermarkTexture = nullptr;
    }
}

void Watermark::UpdateAnimation(ULONGLONG now) {
    if (g_showWatermark && g_watermarkEnableTime == 0) {
        g_watermarkEnableTime = now;
        g_watermarkDisableTime = 0;
    }
    if (!g_showWatermark && g_watermarkDisableTime == 0 && g_watermarkEnableTime > 0) {
        g_watermarkDisableTime = now;
        g_watermarkEnableTime = 0;
    }
    
    if (g_watermarkEnableTime > 0) {
        float enableElapsed = (float)(now - g_watermarkEnableTime) / 1000.0f;
        g_watermarkAnim = fminf(1.0f, enableElapsed / 0.4f);
    }
    else if (g_watermarkDisableTime > 0) {
        float disableElapsed = (float)(now - g_watermarkDisableTime) / 1000.0f;
        float disableAnim = fminf(1.0f, disableElapsed / 0.3f);
        g_watermarkAnim = 1.0f - disableAnim;
        if (disableAnim >= 1.0f) {
            g_watermarkEnableTime = 0;
            g_watermarkDisableTime = 0;
        }
    }
}

void Watermark::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    if (g_showWatermark || (g_watermarkDisableTime > 0 && g_watermarkAnim > 0.01f)) {
        float watermarkAlpha = g_watermarkAnim * 255.0f;
        float slideOffset = -60.0f + (Animations::SmoothInertia(g_watermarkAnim) * 60.0f);
        
        if (watermarkAlpha > 1.0f) {
            float xPosW = arrayListStart.x + 290.0f - ImGui::CalcTextSize("Watermark").x - 10;
            draw->AddText(ImVec2(xPosW + slideOffset, yPos), IM_COL32(100, 255, 200, (int)watermarkAlpha), "Watermark");
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void Watermark::RenderDisplay() {
    if (g_showWatermark || g_watermarkAnim > 0.01f) {
        if (!g_watermarkHud) return;
        
        extern bool g_showMenu;
        g_watermarkHud->HandleDrag(GUI::IsHudEditable());
        g_watermarkHud->ClampToScreen();

        float animT = g_watermarkAnim;
        float easedAnim = Animations::EaseOutExpo(animT);
        float slideX = 0.0f;
        float popScale = 1.0f;
        if (g_animStyle == 1) { // Slide
            easedAnim = Animations::EaseOutExpo(animT);
            float dir = (g_watermarkHud->pos.x > ImGui::GetIO().DisplaySize.x * 0.5f) ? 1.0f : -1.0f;
            slideX = dir * (1.0f - easedAnim) * g_slideOffset;
        } else if (g_animStyle == 2) { // Pop
            easedAnim = Animations::SmoothInertia(animT);
            popScale = 0.5f + 0.5f * Animations::EaseOutBack(animT);
        }

        ImDrawList* draw = ImGui::GetForegroundDrawList();
        ImVec2 pos = ImVec2(g_watermarkHud->pos.x + slideX, g_watermarkHud->pos.y);

        if (g_useImage && g_watermarkTexture) {
            float aspect = (float)g_texWidth / (float)g_texHeight;
            float h = g_imageSize;
            float w = h * aspect;
            
            draw->AddImage((ImTextureID)g_watermarkTexture, pos, ImVec2(pos.x + w, pos.y + h), ImVec2(0,0), ImVec2(1,1), ImColor(1.0f, 1.0f, 1.0f, easedAnim * g_imageOpacity));
            g_watermarkHud->size = ImVec2(w + 10, h + 10);
        } else {
            ImVec4 col = g_staticColor;
            col.w = easedAnim;
            
            ImFont* font = GUI::GetFontByName(g_fontName);
            float size = g_fontSize;
            float renderSize = size * popScale;
            
            if (g_showBackground) {
                ImVec2 tSize = ImGui::CalcTextSize(g_customText);
                tSize.x *= (renderSize / 16.0f); tSize.y *= (renderSize / 16.0f);
                draw->AddRectFilled(
                    ImVec2(pos.x - g_bgPadX, pos.y - g_bgPadY),
                    ImVec2(pos.x + tSize.x + g_bgPadX, pos.y + tSize.y + g_bgPadY),
                    ImGui::GetColorU32(ImVec4(g_bgColor.x, g_bgColor.y, g_bgColor.z, easedAnim * g_bgOpacity)),
                    g_bgRadius);
            }

            if (g_showGlow) {
                // Improved 4-way glow for symmetric "fade" appearance
                std::vector<ImVec4> cols = g_chromaText ? Watermark::GetChromaColors() : std::vector<ImVec4>{Watermark::g_staticColor};
                for (int i = 2; i >= 1; --i) {
                    float glowAlpha = easedAnim * (0.12f / i);
                    GradientText::DrawGradientText(draw, font, renderSize, ImVec2(pos.x + i, pos.y), g_customText, cols, glowAlpha, g_chromaSpeed, g_chromaAngle, g_chromaSaturation, g_chromaLinear);
                    GradientText::DrawGradientText(draw, font, renderSize, ImVec2(pos.x - i, pos.y), g_customText, cols, glowAlpha, g_chromaSpeed, g_chromaAngle, g_chromaSaturation, g_chromaLinear);
                    GradientText::DrawGradientText(draw, font, renderSize, ImVec2(pos.x, pos.y + i), g_customText, cols, glowAlpha, g_chromaSpeed, g_chromaAngle, g_chromaSaturation, g_chromaLinear);
                    GradientText::DrawGradientText(draw, font, renderSize, ImVec2(pos.x, pos.y - i), g_customText, cols, glowAlpha, g_chromaSpeed, g_chromaAngle, g_chromaSaturation, g_chromaLinear);
                }
            }

            if (g_showOutline) {
                const std::vector<ImVec4> outlineCols{ g_outlineColor };
                const float ow = g_outlineWidth;
                const ImVec2 offs[] = {
                    { -ow, 0.0f }, { ow, 0.0f }, { 0.0f, -ow }, { 0.0f, ow },
                    { -ow * 0.7f, -ow * 0.7f }, { ow * 0.7f, -ow * 0.7f },
                    { -ow * 0.7f, ow * 0.7f }, { ow * 0.7f, ow * 0.7f }
                };
                for (const auto& off : offs) {
                    GradientText::DrawGradientText(draw, font, renderSize, ImVec2(pos.x + off.x, pos.y + off.y), g_customText, outlineCols, easedAnim, 0.0f, 0.0f, 1.0f);
                }
            }
            
            GradientText::DrawGradientText(draw, font, renderSize, pos, g_customText, g_chromaText ? Watermark::GetChromaColors() : std::vector<ImVec4>{Watermark::g_staticColor}, easedAnim, g_chromaSpeed, g_chromaAngle, g_chromaSaturation, g_chromaLinear);
            
            // Shimmer effect (Light streak)
            if (g_showShimmer) {
                float time = (float)GetTickCount64() / 1000.0f;
                float shimmerPos = fmodf(time * 0.8f, 2.0f) - 0.5f; 
                
                ImVec2 tSize = ImGui::CalcTextSize(g_customText);
                tSize.x *= (renderSize / 16.0f); tSize.y *= (renderSize / 16.0f);
                
                float startX = pos.x + tSize.x * shimmerPos;
                float width = 30.0f;
                
                draw->PushClipRect(pos, ImVec2(pos.x + tSize.x, pos.y + tSize.y), true);
                draw->AddRectFilledMultiColor(
                    ImVec2(startX, pos.y), ImVec2(startX + width, pos.y + tSize.y),
                    IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, (int)(easedAnim * 100)),
                    IM_COL32(255, 255, 255, (int)(easedAnim * 100)), IM_COL32(255, 255, 255, 0)
                );
                draw->PopClipRect();
            }

            ImVec2 tSize = ImGui::CalcTextSize(g_customText);
            g_watermarkHud->size = ImVec2(tSize.x * (size / 16.0f) + 20, tSize.y * (size / 16.0f) + 10);
        }

        // Corner snapping (skipped while the user is dragging)
        if (g_snapCorner > 0 && !g_watermarkHud->dragging) {
            ImVec2 screen = ImGui::GetIO().DisplaySize;
            ImVec2 pad(g_snapPadding, g_snapPadding);
            switch (g_snapCorner) {
                case 1: g_watermarkHud->pos = ImVec2(pad.x, pad.y); break;
                case 2: g_watermarkHud->pos = ImVec2(screen.x - g_watermarkHud->size.x - pad.x, pad.y); break;
                case 3: g_watermarkHud->pos = ImVec2(pad.x, screen.y - g_watermarkHud->size.y - pad.y); break;
                case 4: g_watermarkHud->pos = ImVec2(screen.x - g_watermarkHud->size.x - pad.x, screen.y - g_watermarkHud->size.y - pad.y); break;
            }
        }

        if (GUI::IsHudEditable()) {
            draw->AddRect(g_watermarkHud->pos, ImVec2(pos.x + g_watermarkHud->size.x, pos.y + g_watermarkHud->size.y), IM_COL32(255, 255, 255, 80));
        }
    }
}

void Watermark::RenderMenu() {
    GUI::RenderCustomSwitch("Watermark", &g_showWatermark);
    
    if (GUI::BeginModuleSettings("Watermark", &g_showWatermark)) {
        const char* modes[] = { "Text", "Image" };
        int currentMode = g_useImage ? 1 : 0;
        if (GUI::RenderCombo("Display Mode", &currentMode, modes, IM_ARRAYSIZE(modes))) {
            g_useImage = (currentMode == 1);
        }

        if (!g_useImage) {
            ImGui::Separator();
            ImGui::Text("Text Settings");
            GUI::RenderFontSelect("Font", g_fontName);
            ImGui::InputText("Content", g_customText, 128);
            GUI::RenderSlider("Size", &g_fontSize, 12.0f, 64.0f, "%.0f px");

            const char* animStyles[] = { "Fade", "Slide", "Pop" };
            ImGui::SetNextItemWidth(-1.0f);
            GUI::RenderCombo("Animation", &g_animStyle, animStyles, IM_ARRAYSIZE(animStyles));
            if (g_animStyle == 1) {
                GUI::RenderSlider("Slide Distance", &g_slideOffset, 5.0f, 120.0f, "%.0f px");
            }
            
            ImGui::Separator();
            ImGui::Text("Color Settings");
            GUI::RenderCustomSwitch("Gradient Animation", &g_chromaText);
            if (g_chromaText) {
                const char* presetNames[32];
                for (int i = 0; i < kWmPresetCount && i < 32; i++) presetNames[i] = g_wmPresets[i].name;
                ImGui::SetNextItemWidth(-1.0f);
                GUI::RenderCombo("Preset##WM", &g_chromaPreset, presetNames, kWmPresetCount);

                if (g_chromaPreset == 0) {
                    float colorCountF = (float)g_customColorCount;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (GUI::RenderSlider("Colors##WM", &colorCountF, 2.0f, 4.0f, "%.0f"))
                        g_customColorCount = (int)colorCountF;
                    for (int i = 0; i < g_customColorCount && i < 4; i++) {
                        char label[32];
                        sprintf_s(label, "Color %d##WM", i);
                        ImGui::ColorEdit4(label, (float*)&g_customColors[i], ImGuiColorEditFlags_NoInputs);
                    }
                }

                GUI::RenderSlider("Speed", &g_chromaSpeed, 0.1f, 5.0f, "%.1fx");
                GUI::RenderSlider("Gradient Angle", &g_chromaAngle, 0.0f, 360.0f, "%.0f°");
                GUI::RenderSlider("Saturation##WM", &g_chromaSaturation, 0.0f, 2.0f, "%.2f");
                GUI::RenderCustomSwitch("Linear Animation##WM", &g_chromaLinear);
                GUI::RenderCustomSwitch("Forward Direction", &g_chromaDirection);
                GUI::RenderCustomSwitch("Mirrored Gradient", &g_mirroredGradient);
                GUI::RenderCustomSwitch("Side Alpha Fade", &g_edgeFade);
            } else {
                ImGui::ColorEdit4("Static Color", (float*)&Watermark::g_staticColor, ImGuiColorEditFlags_NoInputs);
            }
            
            ImGui::Separator();
            ImGui::Text("Position");
            const char* snapModes[] = { "Off", "Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right" };
            ImGui::SetNextItemWidth(-1.0f);
            GUI::RenderCombo("Snap Corner", &g_snapCorner, snapModes, IM_ARRAYSIZE(snapModes));
            if (g_snapCorner > 0) {
                GUI::RenderSlider("Snap Padding", &g_snapPadding, 0.0f, 60.0f, "%.0f px");
            }
            ImGui::TextDisabled("Drag the watermark in-game to reposition it.");

            ImGui::Separator();
            ImGui::Text("Effects");
            GUI::RenderCustomSwitch("Glow Effect", &g_showGlow);
            GUI::RenderCustomSwitch("Shimmer Effect", &g_showShimmer);
            GUI::RenderCustomSwitch("Outline", &g_showOutline);
            if (g_showOutline) {
                ImGui::ColorEdit4("Outline Color", (float*)&g_outlineColor, ImGuiColorEditFlags_NoInputs);
                GUI::RenderSlider("Outline Width", &g_outlineWidth, 0.5f, 4.0f, "%.1f px");
            }
            GUI::RenderCustomSwitch("Background", &g_showBackground);
            if (g_showBackground) {
                GUI::RenderSlider("BG Opacity", &g_bgOpacity, 0.0f, 1.0f, "%.2f");
                ImGui::ColorEdit4("BG Color", (float*)&g_bgColor, ImGuiColorEditFlags_NoInputs);
                GUI::RenderSlider("BG Radius", &g_bgRadius, 0.0f, 20.0f, "%.0f px");
                GUI::RenderSlider("BG Padding X", &g_bgPadX, 0.0f, 30.0f, "%.0f px");
                GUI::RenderSlider("BG Padding Y", &g_bgPadY, 0.0f, 30.0f, "%.0f px");
            }
        } else {
            ImGui::Separator();
            ImGui::Text("Image Settings");
            GUI::RenderSlider("Height", &g_imageSize, 10.0f, 200.0f, "%.0f px");
            GUI::RenderSlider("Opacity", &g_imageOpacity, 0.0f, 1.0f, "%.2f");
            
            if (!g_watermarkTexture) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Image failed to load!");
                if (GUI::RenderButton("Retry Load")) InitializeTextures();
            } else {
                ImGui::Text("Preview:");
                ImGui::Image((ImTextureID)g_watermarkTexture, ImVec2(100, 100 / ((float)g_texWidth / g_texHeight)), ImVec2(0,0), ImVec2(1,1), ImVec4(1,1,1,g_imageOpacity), ImVec4(0,0,0,0));
            }
        }
        
        GUI::EndModuleSettings();
    }
}
