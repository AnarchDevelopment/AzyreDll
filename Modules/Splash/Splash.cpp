/*
Under an4rch Development Public Source License 1.0
*/

#include "Splash.hpp"
#include "../Globals.hpp"
#include "../../GUI/GUI.hpp"
#include "../../ImGui/imgui.h"
#include "../../Animations/Animations.hpp"
#include "../../Assets/resource.h"
#include "../../Assets/stb/stb_image.h"

#include <d3d11.h>
#include <windows.h>
#include <cmath>

extern ID3D11Device* pDevice;
extern HMODULE g_hModule;

namespace {
    ID3D11ShaderResourceView* s_srv = nullptr;
    int   s_texW = 0;
    int   s_texH = 0;

    unsigned long long s_start = 0;
    bool  s_active = false;
    float s_alpha = 0.0f;

    const float kTotalDur = 4.20f;

    struct Particle {
        float x, y;
        float vx, vy;
        float size;
        float life;
        float maxLife;
    };
    Particle s_particles[48];
    bool s_particlesInit = false;

    float HashFloat(int seed) {
        unsigned int h = (unsigned int)(seed * 2654435761u);
        return (float)(h & 0xFFFF) / 65535.0f;
    }

    void InitParticles(float sw, float sh) {
        for (int i = 0; i < 48; i++) {
            Particle& p = s_particles[i];
            p.x = HashFloat(i * 3 + 1) * sw;
            p.y = sh + HashFloat(i * 3 + 2) * sh * 0.5f;
            p.vx = (HashFloat(i * 3 + 3) - 0.5f) * 18.0f;
            p.vy = -(12.0f + HashFloat(i * 7) * 28.0f);
            p.size = 1.2f + HashFloat(i * 5) * 2.5f;
            p.maxLife = 2.5f + HashFloat(i * 11) * 2.0f;
            p.life = HashFloat(i * 13) * p.maxLife;
        }
        s_particlesInit = true;
    }
}

void Splash::Initialize() {
    if (s_srv || !pDevice) return;

    HRSRC hRes = FindResource(g_hModule, MAKEINTRESOURCE(IDR_WATERMARK_IMAGE), RT_RCDATA);
    if (!hRes) return;

    HGLOBAL hGlobal = LoadResource(g_hModule, hRes);
    if (!hGlobal) return;

    void* pData = LockResource(hGlobal);
    DWORD size  = SizeofResource(g_hModule, hRes);

    int w = 0, h = 0, channels = 0;
    unsigned char* px = stbi_load_from_memory((unsigned char*)pData, (int)size, &w, &h, &channels, 4);
    if (!px) return;

    for (int i = 0; i < w * h; ++i) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 255;
        px[i * 4 + 2] = 255;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = px;
    sub.SysMemPitch = w * 4;

    ID3D11Texture2D* pTex = nullptr;
    if (SUCCEEDED(pDevice->CreateTexture2D(&desc, &sub, &pTex))) {
        if (SUCCEEDED(pDevice->CreateShaderResourceView(pTex, nullptr, &s_srv))) {
            s_texW = w;
            s_texH = h;
        }
        pTex->Release();
    }
    stbi_image_free(px);
}

void Splash::Begin() {
    Initialize();
    s_start = GetTickCount64();
    s_alpha = 0.0f;
    s_active = true;
    s_particlesInit = false;
}

bool Splash::IsActive() {
    return s_active;
}

void Splash::Update(unsigned long long now) {
    if (!s_active) return;
    float t = (float)(now - s_start) / 1000.0f;

    if (t < 0.45f)
        s_alpha = fminf(1.0f, t / 0.45f);
    else
        s_alpha = 1.0f;

    if (t >= kTotalDur) {
        s_active = false;
        s_alpha = 0.0f;
    }
}

void Splash::Render(float sw, float sh) {
    if (!s_active || s_alpha <= 0.0f) return;

    float t = (float)(GetTickCount64() - s_start) / 1000.0f;
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    const ImVec4& acc = GUI::g_colorAccent;
    ImU32 accCol = ImColor(acc.x, acc.y, acc.z, 1.0f);

    if (!s_particlesInit) InitParticles(sw, sh);

    // ── PHASE 1: Black background with radial vignette ──
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(sw, sh), IM_COL32(6, 6, 12, (int)(255 * s_alpha)));

    {
        float vr = sw * 0.75f;
        ImVec2 center(sw * 0.5f, sh * 0.5f);
        for (int ring = 0; ring < 5; ring++) {
            float r = vr * (0.45f + ring * 0.15f);
            int a = (int)(18 * s_alpha * (1.0f - ring * 0.18f));
            draw->AddCircleFilled(center, r, IM_COL32(
                (int)(acc.x * 12), (int)(acc.y * 12), (int)(acc.z * 18), a));
        }
    }

    // ── PHASE 2: Floating particles ──
    {
        float particleAlpha = s_alpha;
        if (t > kTotalDur - 0.8f)
            particleAlpha *= fmaxf(0.0f, 1.0f - (t - (kTotalDur - 0.8f)) / 0.8f);

        for (int i = 0; i < 48; i++) {
            Particle& p = s_particles[i];
            p.life += 0.016f;
            if (p.life >= p.maxLife) {
                p.x = HashFloat(i * 3 + 1 + (int)(t * 100)) * sw;
                p.y = sh + 10.0f;
                p.life = 0.0f;
            }
            float px_ = p.x + p.vx * t * 0.3f;
            float py_ = p.y + p.vy * t * 0.3f;
            float frac = p.life / p.maxLife;
            float pa = particleAlpha * (frac < 0.15f ? frac / 0.15f : (frac > 0.75f ? (1.0f - frac) / 0.25f : 1.0f));
            if (pa > 0.01f) {
                draw->AddCircleFilled(
                    ImVec2(px_, py_), p.size,
                    IM_COL32((int)(acc.x * 255), (int)(acc.y * 255), (int)(acc.z * 255), (int)(85 * pa)));
            }
        }
    }

    // ── PHASE 3: Scan line sweeping down ──
    if (t > 0.1f && t < 1.2f) {
        float scanProg = (t - 0.1f) / 1.1f;
        float scanY = sh * (-0.05f + scanProg * 1.10f);
        float scanFade = scanProg < 0.5f ? scanProg * 2.0f : (1.0f - scanProg) * 2.0f;
        draw->AddLine(
            ImVec2(0, scanY), ImVec2(sw, scanY),
            IM_COL32((int)(acc.x * 255), (int)(acc.y * 255), (int)(acc.z * 255), (int)(55 * scanFade * s_alpha)),
            2.0f);
        draw->AddRectFilled(
            ImVec2(0, scanY - 30.0f), ImVec2(sw, scanY + 30.0f),
            IM_COL32((int)(acc.x * 255), (int)(acc.y * 255), (int)(acc.z * 255), (int)(8 * scanFade * s_alpha)));
    }

    // ── PHASE 4: Logo fade / scale in ──
    float logoA = 0.0f;
    float logoScale = 0.88f;
    if (t < 0.60f) {
        float k = Animations::EaseOutBack(Animations::Clamp01(t / 0.60f));
        logoA = k;
        logoScale = 0.88f + 0.12f * k;
    } else if (t < kTotalDur - 0.60f) {
        logoA = 1.0f;
        logoScale = 1.0f;
    } else {
        float fadeOut = Animations::Clamp01((t - (kTotalDur - 0.60f)) / 0.50f);
        logoA = 1.0f - Animations::EaseInQuart(fadeOut);
        logoScale = 1.0f - 0.04f * fadeOut;
    }

    // Logo rise after initial display
    float rise = 0.0f;
    if (t >= 1.50f) {
        float k = Animations::EaseOutQuart(Animations::Clamp01((t - 1.50f) / 0.55f));
        rise = -65.0f * k;
    }

    // ── PHASE 5: Expanding pulse rings ──
    if (t >= 0.30f && t < 2.0f) {
        for (int ring = 0; ring < 3; ring++) {
            float ringT = t - 0.30f - ring * 0.22f;
            if (ringT < 0.0f || ringT > 1.2f) continue;
            float rp = ringT / 1.2f;
            float rr = 60.0f + rp * 220.0f;
            float ra = (1.0f - rp) * 0.35f;
            ImVec2 center(sw * 0.5f, sh * 0.48f + rise);
            draw->AddCircle(center, rr,
                IM_COL32((int)(acc.x * 255), (int)(acc.y * 255), (int)(acc.z * 255), (int)(ra * 255 * logoA)),
                0, 1.5f);
        }
    }

    // ── PHASE 6: Logo glow aura ──
    if (logoA > 0.05f && s_srv) {
        float logoW = 320.0f;
        float logoH = logoW * (float)s_texH / (float)s_texW;
        if (logoH > sh * 0.28f) {
            logoH = sh * 0.28f;
            logoW = logoH * (float)s_texW / (float)s_texH;
        }
        logoW *= logoScale;
        logoH *= logoScale;

        float lx = (sw - logoW) * 0.5f;
        float ly = sh * 0.45f - logoH * 0.5f + rise;

        // Soft glow layers
        float glowExpand = 18.0f + sinf(t * 3.5f) * 4.0f;
        ImVec2 glowMin(lx - glowExpand, ly - glowExpand);
        ImVec2 glowMax(lx + logoW + glowExpand, ly + logoH + glowExpand);
        draw->AddImage((ImTextureID)s_srv, glowMin, glowMax,
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32((int)(acc.x * 255), (int)(acc.y * 180), (int)(acc.z * 255), (int)(22 * logoA)));

        // Second glow pass
        float glow2 = 8.0f + sinf(t * 5.0f) * 2.0f;
        draw->AddImage((ImTextureID)s_srv,
            ImVec2(lx - glow2, ly - glow2), ImVec2(lx + logoW + glow2, ly + logoH + glow2),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, (int)(30 * logoA)));

        // Main logo
        draw->AddImage((ImTextureID)s_srv,
            ImVec2(lx, ly), ImVec2(lx + logoW, ly + logoH),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, (int)(255 * logoA)));

        // ── PHASE 7: "AZYRE" text below logo ──
        if (t >= 0.75f) {
            float textK = Animations::EaseOutQuart(Animations::Clamp01((t - 0.75f) / 0.45f));
            float textA = textK * logoA;
            if (textA > 0.01f) {
                const char* brandText = "AZYRE";
                int len = (int)strlen(brandText);

                ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
                float fontSize = ImGui::GetFontSize() * 1.35f * logoScale;
                float tracking = 2.0f * logoScale;

                // Measure each glyph width for proper spacing
                float charWidths[5];
                float totalW = 0.0f;
                for (int ci = 0; ci < len; ci++) {
                    char single[2] = { brandText[ci], '\0' };
                    charWidths[ci] = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, single).x;
                    totalW += charWidths[ci];
                }
                totalW += tracking * (len - 1);

                float tx = (sw - totalW) * 0.5f;
                float ty = ly + logoH + 18.0f * logoScale;

                // Precompute x positions
                float charX[5];
                float cursor = tx;
                for (int ci = 0; ci < len; ci++) {
                    charX[ci] = cursor;
                    cursor += charWidths[ci] + tracking;
                }

                for (int ci = 0; ci < len; ci++) {
                    float charDelay = ci * 0.07f;
                    float charT = Animations::Clamp01((t - 0.75f - charDelay) / 0.35f);
                    float eased = Animations::EaseOutQuart(charT);
                    float charA = eased * logoA;
                    float charY = ty + (1.0f - eased) * 10.0f;

                    ImVec2 charPos(charX[ci], charY);

                    if (charA > 0.02f) {
                        char single[2] = { brandText[ci], '\0' };

                        // Glow: manual offset renders (no AddTextGlow to avoid double-render bug)
                        ImU32 glowCol = IM_COL32((int)(acc.x * 255), (int)(acc.y * 255), (int)(acc.z * 255), (int)(60 * charA));
                        for (int g = 1; g <= 3; g++) {
                            float off = (float)g * 0.7f;
                            draw->AddText(font, fontSize, ImVec2(charPos.x - off, charPos.y), glowCol, single);
                            draw->AddText(font, fontSize, ImVec2(charPos.x + off, charPos.y), glowCol, single);
                            draw->AddText(font, fontSize, ImVec2(charPos.x, charPos.y - off), glowCol, single);
                            draw->AddText(font, fontSize, ImVec2(charPos.x, charPos.y + off), glowCol, single);
                        }

                        // White letter on top
                        draw->AddText(font, fontSize, charPos,
                            IM_COL32(255, 255, 255, (int)(240 * charA)), single);
                    }
                }

                // Thin accent line under text
                if (textK > 0.5f) {
                    float lineProg = Animations::Clamp01((textK - 0.5f) * 2.0f);
                    float lineWidth = totalW * lineProg;
                    float lineX = (sw - lineWidth) * 0.5f;
                    float lineY = ty + fontSize + 6.0f;
                    draw->AddLine(
                        ImVec2(lineX, lineY), ImVec2(lineX + lineWidth, lineY),
                        IM_COL32((int)(acc.x * 255), (int)(acc.y * 255), (int)(acc.z * 255), (int)(160 * logoA)),
                        1.0f);
                }
            }
        }
    }

    // ── PHASE 8: Loading bar ──
    if (t >= 1.55f) {
        float barIn = Animations::EaseOutQuart(Animations::Clamp01((t - 1.55f) / 0.40f));
        float outFade = (t >= kTotalDur - 0.60f) ? fmaxf(0.0f, 1.0f - (t - (kTotalDur - 0.60f)) / 0.50f) : 1.0f;
        float barA = barIn * outFade;

        auto InterpolateColor = [](ImU32 colA, ImU32 colB, float t) -> ImU32 {
            ImVec4 a = ImGui::ColorConvertU32ToFloat4(colA);
            ImVec4 b = ImGui::ColorConvertU32ToFloat4(colB);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(
                a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
        };

        float bw = 340.0f;
        float bh = 6.0f;
        float bx = (sw - bw) * 0.5f;
        float by = sh * 0.72f + rise;
        float radius = 3.0f;

        float p = Animations::Clamp01((t - 1.55f) / 1.80f);
        float fw = bw * p;

        // Track background
        draw->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh),
            IM_COL32(20, 20, 28, (int)(140 * barA)), radius);

        // Progress fill with gradient
        if (fw > 1.0f) {
            ImU32 col1 = IM_COL32((int)(acc.x * 255), (int)(acc.y * 180), (int)(acc.z * 255), (int)(245 * barA));
            ImU32 col2 = IM_COL32((int)(acc.x * 200), (int)(acc.y * 255), (int)(acc.z * 220), (int)(245 * barA));

            draw->AddRectFilledMultiColor(
                ImVec2(bx, by), ImVec2(bx + fw, by + bh),
                col1, col2, col2, col1);

            // Top shine
            draw->AddRectFilled(
                ImVec2(bx + 2.0f, by + 1.0f),
                ImVec2(bx + fw - 2.0f, by + bh * 0.40f),
                IM_COL32(255, 255, 255, (int)(30 * barA)));

            // Glow under bar
            draw->AddRectFilled(
                ImVec2(bx, by + bh), ImVec2(bx + fw, by + bh + 8.0f),
                IM_COL32((int)(acc.x * 255), (int)(acc.y * 255), (int)(acc.z * 255), (int)(18 * barA)));

            // Moving shimmer
            float shimmerX = bx + fmodf(t * 120.0f, fw + 40.0f) - 20.0f;
            draw->AddRectFilled(
                ImVec2(shimmerX, by),
                ImVec2(shimmerX + 30.0f, by + bh),
                IM_COL32(255, 255, 255, (int)(22 * barA)));
        }

        // Progress text
        ImFont* monoFont = ImGui::GetIO().Fonts->Fonts[0];
        float monoFontSize = ImGui::GetFontSize();

        const char* stage1 = "Initializing systems";
        const char* stage2 = "Loading offsets";
        const char* stage3 = "Preparing modules";
        const char* stage4 = "Almost ready";
        const char* label = stage1;
        if (p >= 0.25f) label = stage2;
        if (p >= 0.55f) label = stage3;
        if (p >= 0.85f) label = stage4;

        char fullLabel[80];
        if (p > 0.03f)
            snprintf(fullLabel, sizeof(fullLabel), "%s  %d%%", label, (int)(p * 100));
        else
            strcpy(fullLabel, label);

        ImVec2 ts = monoFont->CalcTextSizeA(monoFontSize, FLT_MAX, 0.0f, fullLabel);
        ImVec2 textPos = ImVec2((sw - ts.x) * 0.5f, by + bh + 10.0f);

        // Shadow
        draw->AddText(monoFont, monoFontSize, ImVec2(textPos.x + 1.0f, textPos.y + 1.0f),
            IM_COL32(0, 0, 0, (int)(90 * barA)), fullLabel);
        // Main text
        draw->AddText(monoFont, monoFontSize, textPos,
            IM_COL32(200, 200, 210, (int)(210 * barA)), fullLabel);
    }

    // ── PHASE 9: Final flash ──
    if (t >= kTotalDur - 0.55f) {
        float wf = Animations::EaseInQuart(Animations::Clamp01((t - (kTotalDur - 0.55f)) / 0.50f));
        ImU32 flashCol = IM_COL32(
            (int)(255 * acc.x + 255 * (1.0f - acc.x) * wf),
            (int)(255 * acc.y + 255 * (1.0f - acc.y) * wf),
            (int)(255 * acc.z + 255 * (1.0f - acc.z) * wf),
            (int)(255 * wf));
        draw->AddRectFilled(ImVec2(0, 0), ImVec2(sw, sh), flashCol);
    }
}

void Splash::Shutdown() {
    if (s_srv) {
        s_srv->Release();
        s_srv = nullptr;
    }
    s_active = false;
    s_alpha = 0.0f;
    s_particlesInit = false;
}
