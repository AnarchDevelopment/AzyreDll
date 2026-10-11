#include "TargetHUD.hpp"
#include "GUI/Widgets.hpp"

#include "Features/ECS/ECS.hpp"
#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "Features/HudRegistry.hpp"
#include "GUI/Theme.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <cstdio>

namespace mc {

TargetHUD::TargetHUD()
    : Module("TargetHUD", "Info card of your current target", Category::Visuals, 0)
{
    markHasSettings();
}

void TargetHUD::findTarget(Game& game, const LocalPlayer& local)
{
    auto& reg = ecs::get().registry;

    // Mantener target actual si sigue vivo y en rango
    if (targetAddr_ != 0)
    {
        Actor actor(targetAddr_);
        if (actor.valid())
        {
            Vec3 tEye = {actor.pos().x, actor.pos().y + actor.height() * 0.9f, actor.pos().z};
            float dist = distance3D(local.pos(), tEye);
            if (dist <= range_)
            {
                lastSeen_ = GetTickCount64();
                return;
            }
        }
        targetAddr_ = 0;
    }

    // Buscar nuevo target
    Vec3 eye = local.eyePos();
    float myYaw = local.yaw();
    float myPitch = local.pitch();

    float bestScore = 1e9f;
    uintptr_t best = 0;

    for (const Actor& actor : game.remotePlayers())
    {
        if (!actor.valid())
            continue;

        Vec3 tEye = {actor.pos().x, actor.pos().y + actor.height() * 0.9f, actor.pos().z};
        float dist = distance3D(eye, tEye);
        if (dist > range_ || dist < 0.1f)
            continue;

        AimAngles a = calcAngles(eye, tEye);
        float yawDiff = angleDiff(myYaw, a.yaw);
        float pitchDiff = a.pitch - myPitch;
        if (std::fabs(yawDiff) > fov_ * 0.5f || std::fabs(pitchDiff) > fov_ * 0.5f)
            continue;

        float score = std::fabs(yawDiff) + std::fabs(pitchDiff);
        if (score < bestScore)
        {
            bestScore = score;
            best = actor.address();
        }
    }

    if (best != 0)
    {
        targetAddr_ = best;
        lastSeen_ = GetTickCount64();
    }
    else if (targetAddr_ != 0)
    {
        if (GetTickCount64() - lastSeen_ > 2000)
            targetAddr_ = 0;
    }
}

void TargetHUD::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    if (menu::hudEditing())
        return;

    LocalPlayer local = game.localPlayer();
    findTarget(game, local);

    float dt = ImGui::GetIO().DeltaTime;
    bool showing = (targetAddr_ != 0);
    float target = showing ? 1.0f : 0.0f;
    anim_ += (target - anim_) * (dt * 8.0f);
    if (anim_ < 0.01f)
        return;
    if (anim_ > 0.99f)
        anim_ = 1.0f;

    if (!showing)
        return;

    Actor actor(targetAddr_);
    if (!actor.valid())
        return;

    // Read data directly from the Actor (real-time)
    std::string n = actor.name();
    if (n.empty())
        n = "Player";
    targetName_ = n;

    // Health: 0x220 es MAX health. El current health en Bedrock vive en
    // ActorAttributeComponent (pointer chain), no en un field directo.
    // Pendiente: offset o component pointer del health actual.
    int hp = 20; // fallback hasta tener el offset del current health
    (void)hp;
    float dist = distance3D(local.pos(), actor.pos());

    // Lerp health
    float healthFloat = (float)hp;
    lerpedHealth_ += (healthFloat - lerpedHealth_) * (dt * 10.0f);
    if (std::fabs(healthFloat - lerpedHealth_) < 0.1f)
        lerpedHealth_ = healthFloat;

    drawCard(local, actor, dt);
}

void TargetHUD::drawCard(const LocalPlayer& local, const Actor& actor, float dt)
{
    (void)dt;

    auto& reg = ecs::get().registry;
    entt::entity targetEntity = ecs::get().findByAddress(actor.address());
    bool fr = (targetEntity != entt::null) && reg.all_of<ecs::IsFriend>(targetEntity);
    (void)fr;

    float a = anim_;
    std::string n = targetName_;
    if (n.empty())
        n = "Player";

    int displayHealth = (int)std::round(lerpedHealth_);
    float dist = distance3D(local.pos(), actor.pos());

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 disp = io.DisplaySize;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    bool hasBlur = blur::wanted() && blur::ready() && blur::srv();

    // --- Layout ---
    float s = scale_;
    float w = 240.0f * s * a;
    float h = 68.0f * s * a;
    float barH = 6.0f * s;
    float padX = 16.0f * s;
    float padY = 10.0f * s;

    ImVec2 boxMin(disp.x * 0.5f - w * 0.5f, disp.y * 0.62f);
    ImVec2 boxMax(boxMin.x + w, boxMin.y + h);

    hud::Registry::get().report("TargetHUD", boxMin, ImVec2(w, h));

    // --- Background ---
    widgets::SoftShadow(d, boxMin, boxMax, 8.0f * s, 30.0f * a, 8.0f);

    if (hasBlur)
    {
        ImVec2 uv0(boxMin.x / disp.x, boxMin.y / disp.y);
        ImVec2 uv1(boxMax.x / disp.x, boxMax.y / disp.y);
        d->AddImageRounded(ImTextureRef((void*)blur::srv()),
                           boxMin, boxMax, uv0, uv1,
                           IM_COL32(255, 255, 255, (int)(255.0f * a)), 8.0f * s);
        d->AddRectFilled(boxMin, boxMax, IM_COL32(12, 14, 18, (int)(60.0f * a)), 8.0f * s);
    }
    else
        d->AddRectFilled(boxMin, boxMax, IM_COL32(16, 18, 22, (int)(230.0f * a)), 8.0f * s);

    // Friend check
    bool isFriend = friends::isFriend(n);
    ImU32 accent = isFriend ? friends::colorU32() : theme::AccentU32(a);

    // Accent bar
    d->AddRectFilled(boxMin, ImVec2(boxMin.x + 3.0f * s, boxMax.y), accent, 0.0f);

    // Border
    d->AddRect(boxMin, boxMax, IM_COL32(255, 255, 255, (int)(18.0f * a)), 8.0f * s, 0, 1.0f);

    // --- Name + Health ---
    char title[96];
    if (showHealth_)
        snprintf(title, sizeof(title), "%s  [%d]", n.c_str(), displayHealth);
    else
        snprintf(title, sizeof(title), "%s", n.c_str());

    d->AddText(ImVec2(boxMin.x + padX + 3.0f * s, boxMin.y + padY),
               IM_COL32(240, 242, 245, (int)(255.0f * a)), title);

    if (showDistance_)
    {
        char db[32];
        snprintf(db, sizeof(db), "%.1fm", dist);
        d->AddText(ImVec2(boxMin.x + padX + 3.0f * s, boxMin.y + padY + 18.0f * s),
                   IM_COL32(170, 175, 185, (int)(200.0f * a)), db);
    }

    // --- Health bar ---
    if (showHealth_)
    {
        ImVec2 barMin(boxMin.x + padX + 3.0f * s, boxMax.y - barH - padY * 0.5f);
        ImVec2 barMax(boxMax.x - padX, boxMax.y - padY * 0.5f);

        d->AddRectFilled(barMin, barMax, IM_COL32(0, 0, 0, (int)(180.0f * a)), 3.0f);

        float frac = clampf(lerpedHealth_ / 20.0f, 0.0f, 1.0f);
        if (frac > 0.001f)
        {
            ImVec2 fillMax(barMin.x + (barMax.x - barMin.x) * frac, barMax.y);
            ImU32 col = frac > 0.5f
                            ? IM_COL32(40, 220, 80, (int)(255 * a))
                            : IM_COL32(240, 80, 40, (int)(255 * a));
            d->AddRectFilled(barMin, fillMax, col, 3.0f);

            // Shimmer edge
            d->AddRectFilled(ImVec2(fillMax.x - 1, barMin.y),
                             ImVec2(fillMax.x + 1, barMax.y),
                             IM_COL32(255, 255, 255, (int)(80.0f * a)), 0.0f);
        }
    }
}

void TargetHUD::drawSettings()
{
    widgets::CSlider("Range##th", &range_, 2.0f, 16.0f, "%.1f");
    widgets::CSlider("FOV##th", &fov_, 5.0f, 120.0f, "%.0f");
    widgets::CSlider("Scale##th", &scale_, 0.5f, 2.0f, "%.1f");
    widgets::Checkbox("Health bar##th", &showHealth_);
    widgets::Checkbox("Distance##th", &showDistance_);
}

MC_REGISTER_MODULE(TargetHUD);

}
