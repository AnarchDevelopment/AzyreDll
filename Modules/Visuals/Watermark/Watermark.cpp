/*
Under an4rch Development Public Source License 1.0
*/

#include "Watermark.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../Utils/HudElement.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include "../../../Assets/resource.h"
#include "../../../Assets/stb/stb_image.h"
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

// Local helper function for chroma color cycling
// Helper for multi-color interpolation
ImVec4 GetInterpolatedColor(const std::vector<ImVec4>& colors, float t) {
    if (colors.empty()) return ImVec4(1, 1, 1, 1);
    if (colors.size() == 1) return colors[0];
    
    t = fmodf(fmaxf(0.0f, t), 1.0f);
    float scaledT = t * (colors.size() - 1);
    int idx1 = (int)scaledT;
    int idx2 = (idx1 + 1) % colors.size();
    float blend = scaledT - idx1;
    
    const ImVec4& c1 = colors[idx1];
    const ImVec4& c2 = colors[idx2];
    
    return ImVec4(
        c1.x + (c2.x - c1.x) * blend,
        c1.y + (c2.y - c1.y) * blend,
        c1.z + (c2.z - c1.z) * blend,
        c1.w + (c2.w - c1.w) * blend
    );
}

void DrawGradientText(ImDrawList* draw, ImFont* font, float fontSize, ImVec2 pos, const char* text, const std::vector<ImVec4>& colors, float alpha, bool animate) {
    std::string sText = text;
    if (sText.empty()) return;
    
    float totalWidth = 0.0f;
    ImFontBaked* baked = font->GetFontBaked(fontSize);
    for (char c : sText) {
        totalWidth += baked->GetCharAdvance(c);
    }
    
    float startX = pos.x;
    float currentX = pos.x;
    float timeOffset = animate ? ((float)GetTickCount64() / 1000.0f * Watermark::g_chromaSpeed) : 0.0f;
    
    for (size_t i = 0; i < sText.length(); i++) {
        char c = sText[i];
        float charWidth = baked->GetCharAdvance(c);
        
        // Calculate t based on pixel position for smoother transition
        float t = (currentX - startX) / (totalWidth > 0 ? totalWidth : 1.0f);
        
        // Apply direction
        float finalT = Watermark::g_chromaDirection ? (t + timeOffset) : (1.0f - t + timeOffset);
        
        // Mirror the gradient if enabled
        if (Watermark::g_mirroredGradient) {
            finalT = fmodf(finalT * 2.0f, 2.0f);
            if (finalT > 1.0f) finalT = 2.0f - finalT;
        }
        
        ImVec4 col = GetInterpolatedColor(colors, fmodf(finalT, 1.0f));
        float charAlpha = alpha;
        
        // Apply edge fade (transparency at start/end)
        if (Watermark::g_edgeFade) {
            float edgeFade = 1.0f - powf(abs(t - 0.5f) * 2.0f, 4.0f);
            charAlpha *= fmaxf(0.0f, edgeFade);
        }
        
        col.w *= charAlpha;
        
        char buf[2] = { c, '\0' };
        draw->AddText(font, fontSize, ImVec2(currentX, pos.y), ImGui::GetColorU32(col), buf);
        currentX += charWidth;
    }
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
                for (int i = 2; i >= 1; --i) {
                    float glowAlpha = easedAnim * (0.12f / i);
                    DrawGradientText(draw, font, renderSize, ImVec2(pos.x + i, pos.y), g_customText, Watermark::g_chromaColors, glowAlpha, g_chromaText);
                    DrawGradientText(draw, font, renderSize, ImVec2(pos.x - i, pos.y), g_customText, Watermark::g_chromaColors, glowAlpha, g_chromaText);
                    DrawGradientText(draw, font, renderSize, ImVec2(pos.x, pos.y + i), g_customText, Watermark::g_chromaColors, glowAlpha, g_chromaText);
                    DrawGradientText(draw, font, renderSize, ImVec2(pos.x, pos.y - i), g_customText, Watermark::g_chromaColors, glowAlpha, g_chromaText);
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
                    DrawGradientText(draw, font, renderSize, ImVec2(pos.x + off.x, pos.y + off.y), g_customText, outlineCols, easedAnim, false);
                }
            }
            
            DrawGradientText(draw, font, renderSize, pos, g_customText, g_chromaText ? Watermark::g_chromaColors : std::vector<ImVec4>{Watermark::g_staticColor}, easedAnim, g_chromaText);
            
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
                for (size_t i = 0; i < Watermark::g_chromaColors.size(); i++) {
                    char label[32];
                    sprintf_s(label, "Color %d", (int)i + 1);
                    ImGui::ColorEdit4(label, (float*)&Watermark::g_chromaColors[i], ImGuiColorEditFlags_NoInputs);
                    if (Watermark::g_chromaColors.size() > 2) {
                        ImGui::SameLine();
                        char btnLabel[32];
                        sprintf_s(btnLabel, "X##%d", (int)i);
                        if (GUI::RenderButton(btnLabel, ImVec2(24, 0))) {
                            Watermark::g_chromaColors.erase(Watermark::g_chromaColors.begin() + i);
                        }
                    }
                }
                if (Watermark::g_chromaColors.size() < 6) {
                    if (GUI::RenderButton("Add Color")) {
                        Watermark::g_chromaColors.push_back(ImVec4(1, 1, 1, 1));
                    }
                }
                GUI::RenderSlider("Speed", &g_chromaSpeed, 0.1f, 5.0f, "%.1fx");
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
