/*
Under an4rch Development Public Source License 1.0
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include "PlayerInfo.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../Utils/HudElement.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../ImGui/backend/imgui_impl_dx11.h"
#include "../../../GUI/GUI.hpp"
#include "../../../Assets/stb/stb_image.h"
#include <d3d11.h>
#include <windows.h>
#include <appmodel.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

// External globals
extern ID3D11Device* pDevice;

// Draw-list callbacks that switch the pixel shader sampler so the baked head
// texture is drawn with nearest-neighbor filtering (crisp pixel-art head).
static void ImGuiBindNearestSampler(const ImDrawList*, const ImDrawCmd*) {
    ImGui_ImplDX11_RenderState* rs = (ImGui_ImplDX11_RenderState*)ImGui::GetPlatformIO().Renderer_RenderState;
    if (rs && rs->DeviceContext && rs->SamplerNearest)
        rs->DeviceContext->PSSetSamplers(0, 1, &rs->SamplerNearest);
}

static void ImGuiBindLinearSampler(const ImDrawList*, const ImDrawCmd*) {
    ImGui_ImplDX11_RenderState* rs = (ImGui_ImplDX11_RenderState*)ImGui::GetPlatformIO().Renderer_RenderState;
    if (rs && rs->DeviceContext && rs->SamplerLinear)
        rs->DeviceContext->PSSetSamplers(0, 1, &rs->SamplerLinear);
}

// Upscales the 8x8 head region of the skin with nearest-neighbor sampling into
// a crisp 128x128 texture. `withHat` composites the hat overlay region over the
// face (for skins where the hat region is opaque, this saturates the preview).
static ID3D11ShaderResourceView* BakeHeadView(const unsigned char* pixels, int sw, int sh, bool withHat) {
    const int UPSCALE = 16;
    const int SIZE = 8 * UPSCALE;

    std::vector<unsigned char> out((size_t)SIZE * SIZE * 4);

    for (int oy = 0; oy < SIZE; ++oy) {
        int sy = 8 + oy / UPSCALE;
        if (sy >= sh) sy = sh - 1;
        const unsigned char* faceRow = pixels + (sy * sw + 8) * 4;
        const unsigned char* hatRow  = pixels + (sy * sw + 40) * 4;
        for (int ox = 0; ox < SIZE; ++ox) {
            int sx = ox / UPSCALE;
            const unsigned char* fpx = faceRow + sx * 4;
            float r = fpx[0], g = fpx[1], b = fpx[2], a = fpx[3] / 255.0f;

            if (withHat) {
                const unsigned char* hpx = hatRow + sx * 4;
                float ha = hpx[3] / 255.0f;
                if (ha > 0.0f) {
                    float inv = 1.0f - ha;
                    r = hpx[0] * ha + r * inv;
                    g = hpx[1] * ha + g * inv;
                    b = hpx[2] * ha + b * inv;
                    a = ha + a * inv;
                }
            }

            unsigned char* dst = &out[((size_t)oy * SIZE + ox) * 4];
            dst[0] = (unsigned char)(r + 0.5f);
            dst[1] = (unsigned char)(g + 0.5f);
            dst[2] = (unsigned char)(b + 0.5f);
            dst[3] = (unsigned char)(a * 255.0f + 0.5f);
        }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = SIZE;
    desc.Height = SIZE;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = out.data();
    subResource.SysMemPitch = SIZE * 4;

    ID3D11Texture2D* pTexture = nullptr;
    if (FAILED(pDevice->CreateTexture2D(&desc, &subResource, &pTexture))) return nullptr;

    ID3D11ShaderResourceView* pSRV = nullptr;
    HRESULT hr = pDevice->CreateShaderResourceView(pTexture, nullptr, &pSRV);
    pTexture->Release();
    if (FAILED(hr)) return nullptr;

    return pSRV;
}

// Static member initialization
bool PlayerInfo::g_showPlayerInfo = false;
ULONGLONG PlayerInfo::g_enableTime = 0;
ULONGLONG PlayerInfo::g_disableTime = 0;
float PlayerInfo::g_anim = 0.0f;
HudElement* PlayerInfo::g_playerInfoHud = nullptr;

std::string PlayerInfo::g_playerName = "";
void* PlayerInfo::g_skinTexture = nullptr;
void* PlayerInfo::g_skinFaceTex = nullptr;
void* PlayerInfo::g_skinHatTex = nullptr;
int PlayerInfo::g_texWidth = 0;
int PlayerInfo::g_texHeight = 0;

// Settings defaults
bool PlayerInfo::g_showHatLayer = false;
float PlayerInfo::g_headSize = 34.0f;
float PlayerInfo::g_textScale = 1.0f;
bool PlayerInfo::g_showBackground = true;
float PlayerInfo::g_bgOpacity = 0.9f;
float PlayerInfo::g_bgRadius = 6.0f;
bool PlayerInfo::g_showBorder = true;
ImVec4 PlayerInfo::g_borderColor = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);
ImVec4 PlayerInfo::g_nameColor = ImVec4(0.92f, 0.92f, 0.96f, 1.0f);
bool PlayerInfo::g_headRounded = true;
float PlayerInfo::g_headRadius = 10.0f;

std::string PlayerInfo::GetMinecraftDataDir() {
    wchar_t family[128] = { 0 };
    UINT32 famLen = 128;
    if (GetCurrentPackageFamilyName(&famLen, family) != ERROR_SUCCESS) {
        // Not running as a packaged app - fall back to the known Minecraft family name
        wcscpy_s(family, L"Microsoft.MinecraftUWP_8wekyb3d8bbwe");
    }

    char famNarrow[128] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, family, -1, famNarrow, 128, NULL, NULL);

    // NOTE: LOCALAPPDATA is redirected inside packaged (UWP) apps to the package
    // data folder (e.g. ...\Packages\<family>\AC), so it cannot be used to locate
    // the real user profile. USERPROFILE is NOT redirected, so resolve it first.
    wchar_t profile[MAX_PATH] = { 0 };
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";

    char profNarrow[MAX_PATH] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, profile, -1, profNarrow, MAX_PATH, NULL, NULL);

    return std::string(profNarrow) + "\\AppData\\Local\\Packages\\" + famNarrow +
           "\\LocalState\\games\\com.mojang\\minecraftpe";
}

std::string PlayerInfo::ReadPlayerName() {
    std::string dir = GetMinecraftDataDir();
    if (dir.empty()) return "";

    FILE* f = fopen((dir + "\\options.txt").c_str(), "r");
    if (!f) return "";

    char line[1024];
    std::string name;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "mp_username:", 12) == 0) {
            char* n = line + 12;
            char* nl = strchr(n, '\n'); if (nl) *nl = 0;
            char* cr = strchr(n, '\r'); if (cr) *cr = 0;
            name = n;
            break;
        }
    }
    fclose(f);
    return name;
}

bool PlayerInfo::LoadSkinTexture() {
    if (g_skinTexture) return true;

    std::string dir = GetMinecraftDataDir();
    if (dir.empty()) return false;

    int w, h, ch;
    unsigned char* pixels = stbi_load((dir + "\\custom.png").c_str(), &w, &h, &ch, 4);
    if (!pixels) return false;

    g_texWidth = w;
    g_texHeight = h;

    // Bake crisp nearest-neighbor upscaled head views (face only, and face + hat)
    g_skinFaceTex = (void*)BakeHeadView(pixels, w, h, false);
    g_skinHatTex = (void*)BakeHeadView(pixels, w, h, true);

    // Keep the full skin texture too (used as the "skin loaded" indicator)
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = pixels;
    subResource.SysMemPitch = w * 4;

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = pDevice->CreateTexture2D(&desc, &subResource, &pTexture);
    stbi_image_free(pixels);
    if (FAILED(hr)) return false;

    ID3D11ShaderResourceView* pSRV = nullptr;
    hr = pDevice->CreateShaderResourceView(pTexture, nullptr, &pSRV);
    pTexture->Release();
    if (FAILED(hr)) return false;

    g_skinTexture = (void*)pSRV;
    return true;
}

void PlayerInfo::RefreshPlayerData() {
    g_playerName = ReadPlayerName();
    if (!g_skinTexture) LoadSkinTexture();
}

void PlayerInfo::Initialize(HudElement* hud) {
    g_playerInfoHud = hud;
    RefreshPlayerData();
}

void PlayerInfo::Shutdown() {
    if (g_skinTexture) {
        ((ID3D11ShaderResourceView*)g_skinTexture)->Release();
        g_skinTexture = nullptr;
    }
    if (g_skinFaceTex) {
        ((ID3D11ShaderResourceView*)g_skinFaceTex)->Release();
        g_skinFaceTex = nullptr;
    }
    if (g_skinHatTex) {
        ((ID3D11ShaderResourceView*)g_skinHatTex)->Release();
        g_skinHatTex = nullptr;
    }
}

void PlayerInfo::UpdateAnimation(ULONGLONG now) {
    if (g_showPlayerInfo && g_enableTime == 0) {
        g_enableTime = now;
        g_disableTime = 0;
        RefreshPlayerData();
    }
    if (!g_showPlayerInfo && g_disableTime == 0 && g_enableTime > 0) {
        g_disableTime = now;
        g_enableTime = 0;
    }

    if (g_enableTime > 0) {
        float enableElapsed = (float)(now - g_enableTime) / 1000.0f;
        g_anim = fminf(1.0f, enableElapsed / 0.25f);
    } else if (g_disableTime > 0) {
        float disableElapsed = (float)(now - g_disableTime) / 1000.0f;
        g_anim = 1.0f - fminf(1.0f, disableElapsed / 0.2f);
        if (g_anim <= 0.01f) {
            g_disableTime = 0;
            g_anim = 0.0f;
        }
    }
}

void PlayerInfo::RenderDisplay() {
    if (!(g_showPlayerInfo || g_anim > 0.01f)) return;
    if (!g_playerInfoHud) return;

    extern bool g_showMenu;
    g_playerInfoHud->HandleDrag(GUI::IsHudEditable());
    g_playerInfoHud->ClampToScreen();

    float eased = Animations::EaseOutExpo(g_anim);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImVec2 pos = g_playerInfoHud->pos;

    // Content metrics
    const float headSize = g_headSize;
    const float fontSize = 16.0f * g_textScale;
    ImFont* font = GUI::g_fontDefault ? GUI::g_fontDefault : ImGui::GetFont();
    std::string displayName = g_playerName.empty() ? "Unknown" : g_playerName;
    ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, displayName.c_str());

    const float padX = 10.0f, padY = 8.0f;
    const float gap = 8.0f;
    float boxW = padX * 2.0f + headSize + gap + ts.x;
    float boxH = padY * 2.0f + fmaxf(headSize, ts.y);

    g_playerInfoHud->size = ImVec2(boxW, boxH);

    // Background
    if (g_showBackground) {
        draw->AddRectFilled(pos, pos + ImVec2(boxW, boxH), IM_COL32(20, 20, 26, (int)(255 * g_bgOpacity * eased)), g_bgRadius);
    }

    // Border
    if (g_showBorder) {
        ImU32 borderCol = ImGui::GetColorU32(ImVec4(g_borderColor.x, g_borderColor.y, g_borderColor.z, g_borderColor.w * eased));
        draw->AddRect(pos, pos + ImVec2(boxW, boxH), borderCol, g_bgRadius, 0, 1.0f);
    }

    // Skin head (crisp baked texture). The face region of the raw skin is only
    // 8x8 pixels, so the raw texture was upscaled with nearest-neighbor sampling
    // and is drawn here with the nearest sampler to keep the pixel-art look.
    ImVec2 headMin = pos + ImVec2(padX, padY);
    if (g_skinTexture) {
        void* headView = g_showHatLayer ? g_skinHatTex : g_skinFaceTex;
        ImU32 col = IM_COL32(255, 255, 255, (int)(255 * eased));
        if (headView) {
            draw->AddCallback(ImGuiBindNearestSampler, nullptr);
            if (g_headRounded) {
                draw->AddImageRounded((ImTextureID)headView, headMin, headMin + ImVec2(headSize, headSize), ImVec2(0, 0), ImVec2(1, 1), col, g_headRadius, ImDrawFlags_RoundCornersAll);
            } else {
                draw->AddImage((ImTextureID)headView, headMin, headMin + ImVec2(headSize, headSize), ImVec2(0, 0), ImVec2(1, 1), col);
            }
            draw->AddCallback(ImGuiBindLinearSampler, nullptr);
        } else {
            float w = (float)g_texWidth, h = (float)g_texHeight;
            ImVec2 faceUV0(8.0f / w, 8.0f / h), faceUV1(16.0f / w, 16.0f / h);
            draw->AddImage((ImTextureID)g_skinTexture, headMin, headMin + ImVec2(headSize, headSize), faceUV0, faceUV1, col);
        }
    } else {
        draw->AddRectFilled(headMin, headMin + ImVec2(headSize, headSize), IM_COL32(70, 70, 80, (int)(255 * eased)), 4.0f);
    }

    // Player name
    ImVec2 textPos = headMin + ImVec2(headSize + gap, (headSize - ts.y) * 0.5f);
    draw->AddText(font, fontSize, textPos, ImGui::GetColorU32(ImVec4(g_nameColor.x, g_nameColor.y, g_nameColor.z, g_nameColor.w * eased)), displayName.c_str());

    if (GUI::IsHudEditable()) {
        draw->AddRect(g_playerInfoHud->pos, pos + ImVec2(boxW, boxH), IM_COL32(255, 255, 255, 80));
    }
}

void PlayerInfo::RenderMenu() {
    bool prev = g_showPlayerInfo;
    GUI::RenderCustomSwitch("Player Info", &g_showPlayerInfo);

    if (GUI::BeginModuleSettings("Player Info", &g_showPlayerInfo)) {
        ImGui::Text("Name: %s", g_playerName.empty() ? "Not found" : g_playerName.c_str());
        if (g_skinTexture) {
            ImGui::Text("Skin: loaded (%dx%d)", g_texWidth, g_texHeight);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Skin not found (custom.png)");
        }
        if (ImGui::Button("Reload Data")) {
            RefreshPlayerData();
        }

        ImGui::Separator();
        ImGui::SliderFloat("Head Size##PI", &g_headSize, 18.0f, 72.0f, "%.0f");
        ImGui::SliderFloat("Text Scale##PI", &g_textScale, 0.5f, 3.0f, "%.2f");
        GUI::RenderCustomSwitch("Show Hat Layer##PI", &g_showHatLayer);
        GUI::RenderCustomSwitch("Rounded Head##PI", &g_headRounded);
        if (g_headRounded) {
            ImGui::SliderFloat("Head Radius##PI", &g_headRadius, 0.0f, 32.0f, "%.0f");
        }
        GUI::RenderCustomSwitch("Show Background##PI", &g_showBackground);
        if (g_showBackground) {
            ImGui::SliderFloat("Bg Opacity##PI", &g_bgOpacity, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Bg Radius##PI", &g_bgRadius, 0.0f, 24.0f, "%.0f");
        }
        GUI::RenderCustomSwitch("Show Border##PI", &g_showBorder);
        if (g_showBorder) {
            ImGui::ColorEdit4("Border Color##PI", (float*)&g_borderColor, ImGuiColorEditFlags_NoInputs);
        }
        ImGui::ColorEdit4("Name Color##PI", (float*)&g_nameColor, ImGuiColorEditFlags_NoInputs);

        ImGui::Separator();
        ImGui::TextDisabled("Drag the box in-game to reposition it.");
        GUI::EndModuleSettings();
    }
}
