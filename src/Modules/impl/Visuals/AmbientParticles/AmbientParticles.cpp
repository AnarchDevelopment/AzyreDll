#include "AmbientParticles.hpp"
#include "GUI/Widgets.hpp"

#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"

#include <windows.h>
#include <algorithm>
#include <cmath>

namespace mc {

namespace {

struct ModeCfg
{
    float fall;     // + baja, - sube (bloques/s a speed 1)
    float drift;    // oscilacion horizontal
    float twinkle;  // 0 = alpha fijo, 1 = titileo completo
    float swirl;    // rotacion xz (rad/s a speed 1)
    float topY;     // wrap alto (relativo a los pies)
    float botY;     // wrap bajo
    float r, g, b;
};

const ModeCfg kModes[4] = {
    { 1.15f, 0.45f, 0.12f, 0.00f, 9.0f, -0.2f, 0.94f, 0.97f, 1.00f}, // Snow
    {-0.85f, 0.60f, 0.55f, 0.00f, 8.0f, -0.2f, 1.00f, 0.55f, 0.20f}, // Ember
    { 0.50f, 0.90f, 0.15f, 0.45f, 9.0f, -0.2f, 1.00f, 0.58f, 0.76f}, // Petal
    {-0.30f, 0.30f, 1.00f, 0.00f, 7.0f, -0.2f, 1.00f, 0.90f, 0.50f}, // Spark
};

}

AmbientParticles::AmbientParticles()
    : Module("AmbientParticles", "Ambient particles floating around you",
             Category::Visuals, 0)
{
    markHasSettings();
}

void AmbientParticles::onEnable()
{
    parts_.clear();
    rng_ = 0x9E3779B9u ^ (uint32_t)GetTickCount64();
}

float AmbientParticles::nextRand()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(rng_ & 0xFFFFFF) / (float)0x1000000;
}

void AmbientParticles::spawn(Part& p)
{
    p.off.x = (nextRand() * 2.0f - 1.0f) * radius_;
    p.off.z = (nextRand() * 2.0f - 1.0f) * radius_;
    p.off.y = -0.2f + nextRand() * 8.0f;
    p.seed = nextRand();
    p.phase = nextRand() * kPi * 2.0f;
}

void AmbientParticles::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
        return;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    if (!fovInited_)
    {
        fov_ = readGameFov();
        fovInited_ = true;
    }

    while ((int)parts_.size() < count_)
    {
        Part p;
        spawn(p);
        parts_.push_back(p);
    }
    if ((int)parts_.size() > count_)
        parts_.resize((size_t)count_);

    const ModeCfg& m = kModes[mode_];
    float dt = clampf(io.DeltaTime, 0.0f, 0.05f);
    float R = radius_;
    float spanY = m.topY - m.botY;
    float t = (float)ImGui::GetTime();

    for (auto& p : parts_)
    {
        p.off.y -= m.fall * speed_ * dt;
        p.off.x += std::sin(t * 0.8f + p.phase) * m.drift * speed_ * dt;
        p.off.z += std::cos(t * 0.7f + p.phase * 1.37f) * m.drift * speed_ * dt;

        if (m.swirl != 0.0f)
        {
            float ang = m.swirl * speed_ * dt;
            float ca = std::cos(ang), sa = std::sin(ang);
            float nx = p.off.x * ca - p.off.z * sa;
            float nz = p.off.x * sa + p.off.z * ca;
            p.off.x = nx;
            p.off.z = nz;
        }

        while (p.off.y > m.topY) p.off.y -= spanY;
        while (p.off.y < m.botY) p.off.y += spanY;
        while (p.off.x > R) p.off.x -= 2.0f * R;
        while (p.off.x < -R) p.off.x += 2.0f * R;
        while (p.off.z > R) p.off.z -= 2.0f * R;
        while (p.off.z < -R) p.off.z += 2.0f * R;
    }

    CameraView cam = getCameraView(local, game.client());
    ImDrawList* d = ImGui::GetBackgroundDrawList();

    // Centro tick-based (~20Hz) -> interp por-frame a la tasa de refresco
    // ("144Hz"): el campo de particulas no salta con los updates de red.
    Vec3 base{local.posExact().x, local.pos().y, local.posExact().z};
    Vec3 c = smooth::interp(smooth::channel::Ambient, base);

    int cr, cg, cb;
    if (customColor_)
    {
        cr = (int)(color_.x * 255.0f);
        cg = (int)(color_.y * 255.0f);
        cb = (int)(color_.z * 255.0f);
    }
    else
    {
        cr = (int)(m.r * 255.0f);
        cg = (int)(m.g * 255.0f);
        cb = (int)(m.b * 255.0f);
    }
    if (cr > 255) cr = 255;
    if (cg > 255) cg = 255;
    if (cb > 255) cb = 255;

    float maxRange = R + 8.0f;

    for (const auto& p : parts_)
    {
        Vec3 world{c.x + p.off.x, c.y + p.off.y, c.z + p.off.z};
        Vec3 s;
        if (!worldToScreen(world, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, s))
            continue;
        if (s.z > maxRange)
            continue;

        float px = size_ * (170.0f / s.z);
        if (px < 1.0f)
            continue;
        if (px > 26.0f)
            px = 26.0f;

        // Fade en el borde del area (evita el pop al entrar/salir del wrap).
        float mm = std::max(std::fabs(p.off.x), std::fabs(p.off.z)) / R;
        float edge = mm > 0.80f ? clampf((1.0f - mm) / 0.20f, 0.0f, 1.0f) : 1.0f;
        float nearF = clampf((s.z - 0.35f) / 1.10f, 0.0f, 1.0f);
        float farF = clampf((maxRange - s.z) / 5.0f, 0.0f, 1.0f);
        float tw = 1.0f - m.twinkle +
                   m.twinkle * (0.5f + 0.5f * std::sin(t * (1.6f + p.seed * 3.2f) + p.phase));

        float a = opacity_ * edge * nearF * farF * tw;
        if (a <= 0.02f)
            continue;
        int ai = (int)(a * 255.0f);
        if (ai > 255) ai = 255;

        ImVec2 pt(s.x, s.y);
        if (ai >= 4)
            d->AddCircleFilled(pt, px * 2.2f, IM_COL32(cr, cg, cb, ai / 4), 10);
        d->AddCircleFilled(pt, px, IM_COL32(cr, cg, cb, ai), 10);
    }
}

void AmbientParticles::drawSettings()
{
    const char* modes[] = {"Snow", "Ember", "Petal", "Spark"};
    widgets::CCombo("Mode##amb", &mode_, modes, 4);
    widgets::CSlider("Count##amb", &count_, 20, 500);
    widgets::CSlider("Radius##amb", &radius_, 6.0f, 40.0f, "%.0f");
    widgets::CSlider("Speed##amb", &speed_, 0.2f, 3.0f, "%.1f");
    widgets::CSlider("Size##amb", &size_, 0.5f, 4.0f, "%.1f");
    widgets::CSlider("Opacity##amb", &opacity_, 0.05f, 1.00f, "%.2f");
    widgets::Checkbox("Custom color##amb", &customColor_);
    if (customColor_)
    {
        ImGui::Indent();
        widgets::ColorEdit4("Color##amb", (float*)&color_);
        ImGui::Unindent();
    }
    widgets::CSlider("FOV##amb", &fov_, 30.0f, 130.0f, "%.0f");
    ImGui::TextDisabled("Snow: falls / Ember: rises / Petal: swirls / Spark: twinkles");
}

MC_REGISTER_MODULE(AmbientParticles);

}
