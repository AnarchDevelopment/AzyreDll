#include "TargetHUD.hpp"

#include "Features/Friends.hpp"
#include "Features/HudRegistry.hpp"
#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Combat/KillAura/KillAura.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/DX11.hpp"
#include "Render/ResourceSprites.hpp"
#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"
#include "resource.h"

#include <imgui.h>
#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace mc {

TargetHUD::TargetHUD()
    : Module("TargetHUD", "Solstice-style target information card", Category::Visuals, 0)
{
    markHasSettings();
}

TargetHUD::~TargetHUD()
{
    releaseSkinTexture();
}

void TargetHUD::releaseSkinTexture()
{
    if (skinSrv_)
    {
        static_cast<ID3D11ShaderResourceView*>(skinSrv_)->Release();
        skinSrv_ = nullptr;
    }
    skinHash_ = 0;
}

void TargetHUD::onShutdown()
{
    releaseSkinTexture();
    skinOwner_ = 0;
}

void TargetHUD::updateSkinTexture(const Actor& actor)
{
    if (skinOwner_ != actor.address())
    {
        releaseSkinTexture();
        skinOwner_ = actor.address();
    }

    std::array<uint8_t, off::skin::PixelBytes> pixels{};
    if (!actor.skinRgba(pixels))
        return;

    uint64_t hash = 1469598103934665603ull;
    for (uint8_t byte : pixels)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    if (skinSrv_ && hash == skinHash_)
        return;

    ID3D11Device* device = static_cast<ID3D11Device*>(dx11::d3dDevice());
    if (!device)
        return;

    // The front face is only 8x8 texels in the skin atlas. Composite the hat
    // layer, then expand each source texel 8x with nearest-neighbor sampling
    // so ImGui's linear sampler does not blur the small face crop.
    std::array<uint8_t, off::skin::PixelBytes> facePixels{};
    std::array<std::array<uint8_t, 4>, 64> faceTexels{};
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const size_t baseOffset = (static_cast<size_t>(y + 8) * 64 + (x + 8)) * 4;
            const size_t overlayOffset = (static_cast<size_t>(y + 8) * 64 + (x + 40)) * 4;
            auto& out = faceTexels[static_cast<size_t>(y) * 8 + x];
            const unsigned alpha = pixels[overlayOffset + 3];
            const unsigned inverseAlpha = 255u - alpha;
            for (size_t channel = 0; channel < 3; ++channel)
            {
                out[channel] = static_cast<uint8_t>(
                    (static_cast<unsigned>(pixels[overlayOffset + channel]) * alpha +
                     static_cast<unsigned>(pixels[baseOffset + channel]) * inverseAlpha + 127u) /
                    255u);
            }
            out[3] = std::max(pixels[baseOffset + 3], pixels[overlayOffset + 3]);
        }
    }
    for (int y = 0; y < 64; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            const auto& texel = faceTexels[static_cast<size_t>(y / 8) * 8 + (x / 8)];
            const size_t offset = (static_cast<size_t>(y) * 64 + x) * 4;
            std::copy(texel.begin(), texel.end(), facePixels.begin() + offset);
        }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64;
    desc.Height = 64;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = facePixels.data();
    init.SysMemPitch = 64 * 4;

    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &init, &texture)) && texture)
    {
        if (SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &view)) && view)
        {
            if (skinSrv_)
                static_cast<ID3D11ShaderResourceView*>(skinSrv_)->Release();
            skinSrv_ = view;
            skinHash_ = hash;
        }
        texture->Release();
    }
}

void TargetHUD::findTarget(const LocalPlayer& local)
{
    targetAddr_ = 0;
    Module* auraModule = ModuleManager::get().find("KillAura");
    if (!auraModule || !auraModule->enabled())
        return;

    auto* aura = static_cast<KillAura*>(auraModule);
    const uintptr_t candidate = aura->targetAddress();
    if (!candidate || candidate == local.address())
        return;

    Actor actor(candidate);
    const float currentHealth = actor.currentHealth();
    if (!actor.valid() || currentHealth == 0.0f ||
        (currentHealth < 0.0f && actor.maxHealth() <= 0.0f))
        return;
    targetAddr_ = candidate;
}

void TargetHUD::onRender()
{
    Game& game = Game::get();
    if (!game.localFound() || menu::hudEditing())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.isLocal())
        return;
    findTarget(local);

    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
    const bool showing = targetAddr_ != 0;
    const float targetAnim = showing ? 1.0f : 0.0f;
    anim_ += (targetAnim - anim_) * std::min(1.0f, dt * 10.0f);
    if (anim_ < 0.01f)
        return;

    if (!showing)
        return;

    Actor actor(targetAddr_);
    if (!actor.valid())
    {
        targetAddr_ = 0;
        return;
    }

    targetName_ = actor.name();
    if (targetName_.empty())
        targetName_ = "Player";

    maxHealth_ = actor.maxHealth();
    if (maxHealth_ < 1.0f || maxHealth_ > 2048.0f)
        maxHealth_ = 20.0f;
    const float currentHealth = actor.currentHealth();
    if (displayedTarget_ != actor.address())
    {
        health_ = currentHealth >= 0.0f
            ? std::clamp(currentHealth, 0.0f, maxHealth_)
            : maxHealth_;
        displayedTarget_ = actor.address();
    }
    else if (currentHealth >= 0.0f && std::isfinite(currentHealth))
    {
        const float clampedHealth = std::clamp(currentHealth, 0.0f, maxHealth_);
        health_ += (clampedHealth - health_) * std::min(1.0f, dt * 10.0f);
        if (std::fabs(clampedHealth - health_) < 0.05f)
            health_ = clampedHealth;
    }

    updateSkinTexture(actor);
    drawCard(local, actor, dt);
}

void TargetHUD::drawCard(const LocalPlayer& local, const Actor& actor, float dt)
{
    (void)dt;
    const float s = scale_;
    const float a = anim_;
    const float fullW = 240.0f * s;
    const float fullH = 74.0f * s;
    const float w = fullW * a;
    const float h = fullH * a;
    if (w < 1.0f || h < 1.0f)
        return;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 defaultPos(display.x * 0.5f - fullW * 0.5f,
                            display.y * 0.5f - fullH * 0.5f);
    const ImVec2 panelPos = hud::Registry::get().report(
        "TargetHUD", defaultPos, ImVec2(fullW, fullH));
    const ImVec2 boxMin(panelPos.x + (fullW - w) * 0.5f,
                        panelPos.y + (fullH - h) * 0.5f);
    const ImVec2 boxMax(boxMin.x + w, boxMin.y + h);
    const float radius = 12.0f * s * a;
    const float pad = 12.5f * s * a;
    const bool hasBlur = blur::wanted() && blur::ready() && blur::srv();

    widgets::SoftShadow(draw, boxMin, boxMax, radius, 24.0f * a, 8.0f * s);
    if (hasBlur && display.x > 0.0f && display.y > 0.0f)
    {
        const ImVec2 uv0(boxMin.x / display.x, boxMin.y / display.y);
        const ImVec2 uv1(boxMax.x / display.x, boxMax.y / display.y);
        draw->AddImageRounded(ImTextureRef(blur::srv()), boxMin, boxMax, uv0, uv1,
                              IM_COL32(255, 255, 255, static_cast<int>(255.0f * a)), radius);
        draw->AddRectFilled(boxMin, boxMax,
                            IM_COL32(8, 10, 16, static_cast<int>(95.0f * a)), radius);
    }
    else
        draw->AddRectFilled(boxMin, boxMax,
                            IM_COL32(10, 12, 18, static_cast<int>(232.0f * a)), radius);

    const ImU32 accent = friends::isFriend(targetName_)
        ? friends::colorU32()
        : theme::AccentU32(a);
    draw->AddRectFilled(boxMin, ImVec2(boxMin.x + 3.0f * s * a, boxMax.y), accent, radius);
    draw->AddRect(boxMin, boxMax,
                  IM_COL32(255, 255, 255, static_cast<int>(28.0f * a)), radius, 0, 1.0f * s);

    const float headSize = 55.0f * s * a;
    const ImVec2 headMin(boxMin.x + pad, boxMin.y + (h - headSize) * 0.5f);
    const ImVec2 headMax(headMin.x + headSize, headMin.y + headSize);
    if (skinSrv_)
    {
        const ImTextureRef texture(skinSrv_);
        draw->AddImageRounded(texture, headMin, headMax,
                              ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                              IM_COL32(255, 255, 255, static_cast<int>(255.0f * a)),
                              10.0f * s * a);
    }
    else if (void* head = resources::srv(IDR_STEVE_HEAD))
    {
        draw->AddImageRounded(ImTextureRef(head), headMin, headMax,
                              ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                              IM_COL32(255, 255, 255, static_cast<int>(255.0f * a)),
                              10.0f * s * a);
    }

    const float contentX = headMax.x + 12.0f * s * a;
    const float rightPad = 12.0f * s * a;
    const float contentRight = boxMax.x - rightPad;
    const float textSize = fontSize_ * s * a;
    const float titleY = boxMin.y + 9.0f * s * a;
    const ImU32 textWhite = IM_COL32(245, 247, 250, static_cast<int>(255.0f * a));
    draw->AddText(nullptr, textSize, ImVec2(contentX, titleY), textWhite,
                  targetName_.c_str());

    const float distance = distance3D(local.pos(), actor.pos());
    const float localHealth = local.currentHealth();
    const bool winning = localHealth >= 0.0f && health_ >= 0.0f && localHealth >= health_;
    const char* status = localHealth < 0.0f ? "Status unavailable"
        : (winning ? "Winning" : "Losing");
    const float statusY = titleY + 23.0f * s * a;
    if (showStatus_)
    {
        const ImU32 statusColor = localHealth < 0.0f
            ? IM_COL32(185, 190, 202, static_cast<int>(220.0f * a))
            : winning
            ? IM_COL32(140, 225, 170, static_cast<int>(245.0f * a))
            : IM_COL32(240, 142, 142, static_cast<int>(245.0f * a));
        draw->AddText(nullptr, 13.0f * s * a, ImVec2(contentX, statusY), statusColor, status);
    }
    if (showDistance_)
    {
        char distanceText[24]{};
        std::snprintf(distanceText, sizeof(distanceText), "%.1fm", distance);
        const ImVec2 distanceSize = ImGui::GetFont()->CalcTextSizeA(
            12.0f * s * a, FLT_MAX, 0.0f, distanceText);
        draw->AddText(nullptr, 12.0f * s * a,
                      ImVec2(contentRight - distanceSize.x, statusY),
                      IM_COL32(185, 190, 202, static_cast<int>(220.0f * a)), distanceText);
    }

    if (showHealth_)
    {
        const float barH = 13.0f * s * a;
        const ImVec2 barMin(contentX, boxMax.y - 12.0f * s * a - barH);
        const ImVec2 barMax(contentRight, barMin.y + barH);
        draw->AddRectFilled(barMin, barMax,
                            IM_COL32(100, 100, 110, static_cast<int>(170.0f * a)),
                            8.0f * s * a);

        const float healthPct = std::clamp(health_ / maxHealth_, 0.0f, 1.0f);
        const ImVec2 healthMax(barMin.x + (barMax.x - barMin.x) * healthPct, barMax.y);
        ImVec4 start = theme::Accent;
        ImVec4 end = theme::AccentTri ? theme::Accent3 : theme::Accent2;
        start.w *= a;
        end.w *= a;
        if (healthPct > 0.01f)
        {
            draw->PushClipRect(barMin, healthMax, true);
            draw->AddRectFilledMultiColor(barMin, barMax,
                ImGui::ColorConvertFloat4ToU32(start), ImGui::ColorConvertFloat4ToU32(end),
                ImGui::ColorConvertFloat4ToU32(end), ImGui::ColorConvertFloat4ToU32(start));
            draw->PopClipRect();
        }
        char healthText[32]{};
        std::snprintf(healthText, sizeof(healthText), "%.0f / %.0f", health_, maxHealth_);
        draw->AddText(nullptr, 10.0f * s * a,
                      ImVec2(barMin.x + 5.0f * s * a, barMin.y + 1.0f * s * a),
                      IM_COL32(255, 255, 255, static_cast<int>(240.0f * a)), healthText);
    }
}

void TargetHUD::drawSettings()
{
    widgets::CSlider("Font Size##th", &fontSize_, 12.0f, 32.0f, "%.0f px");
    widgets::CSlider("Scale##th", &scale_, 0.5f, 2.0f, "%.1f");
    widgets::Checkbox("Health##th", &showHealth_);
    widgets::Checkbox("Status##th", &showStatus_);
    widgets::Checkbox("Distance##th", &showDistance_);
}

MC_REGISTER_MODULE(TargetHUD);

} // namespace mc
