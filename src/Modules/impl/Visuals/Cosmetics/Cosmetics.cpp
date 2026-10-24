#include "Cosmetics.hpp"
#include "GUI/Widgets.hpp"

#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"
#include "Features/Smoothing.hpp"

#include <cmath>

namespace mc {

namespace {

struct WingStyleCfg
{
    int feathers;
    float aMin;      // rad del plumon mas bajo
    float aMax;      // rad del plumon mas alto
    float length;    // escala en bloques (antes de size_)
    float widthRoot; // ancho en la base
    float widthTip;  // ancho en la punta
    float flapAmp;   // rad del batido
    float backTilt;  // componente hacia atras del plano del ala
    float flapMul;   // multiplicador de frecuencia
};

// Angel: plumas largas y aireadas, batido suave y continuo.
// Demon: membrana corta y ancha, batido lento y fuerte.
// Butterfly: lobulos muy anchos, aleteo rapido.
const WingStyleCfg kWingStyles[3] = {
    {5, -0.30f, 1.15f, 0.95f, 0.050f, 0.018f, 0.26f, 0.45f, 1.00f},
    {4, -0.55f, 1.00f, 0.78f, 0.110f, 0.050f, 0.32f, 0.55f, 0.75f},
    {3, -0.80f, 0.70f, 0.85f, 0.150f, 0.075f, 0.52f, 0.30f, 1.80f},
};

ImU32 withAlpha(const ImVec4& c, float k)
{
    int r = (int)(c.x * 255.0f);
    int g = (int)(c.y * 255.0f);
    int b = (int)(c.z * 255.0f);
    int a = (int)(k * 255.0f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    return IM_COL32(r, g, b, a);
}

void drawQuad(ImDrawList* d, const Vec3 q[4], const CameraView& cam, float fov,
              float sw, float sh, ImU32 col)
{
    Vec3 p[4];
    for (int i = 0; i < 4; ++i)
    {
        if (!worldToScreen(q[i], cam.pos, cam.yaw, cam.pitch, fov, sw, sh, p[i]))
            return;
    }
    d->AddQuadFilled(ImVec2(p[0].x, p[0].y), ImVec2(p[1].x, p[1].y),
                     ImVec2(p[2].x, p[2].y), ImVec2(p[3].x, p[3].y), col);
}

void drawWings(ImDrawList* d, const Vec3& anchor, float yawDeg, float t,
               const WingStyleCfg& st, float flapSpeed, float size,
               const CameraView& cam, float fov, float sw, float sh,
               const ImVec4& color, float opacity)
{
    float yR = deg2rad(yawDeg);
    Vec3 fwd{-std::sin(yR), 0.0f, std::cos(yR)};
    Vec3 right{-std::cos(yR), 0.0f, -std::sin(yR)};
    Vec3 back = fwd * -1.0f;
    Vec3 e2 = normalize(Vec3{0.0f, 1.0f, 0.0f} + back * st.backTilt);

    float basePhase = t * flapSpeed * st.flapMul * kPi * 2.0f;

    for (int side = -1; side <= 1; side += 2)
    {
        Vec3 e1 = right * (float)side;

        for (int i = 0; i < st.feathers; ++i)
        {
            float f = st.feathers > 1 ? (float)i / (float)(st.feathers - 1) : 0.5f;
            // El batido viaja de dentro hacia fuera (retraso por plumon).
            float flap = st.flapAmp * std::sin(basePhase - (float)i * 0.35f);
            float a = st.aMin + (st.aMax - st.aMin) * f + flap;
            float ca = std::cos(a), sa = std::sin(a);
            Vec3 dir = e1 * ca + e2 * sa;
            Vec3 perp = e1 * -sa + e2 * ca;

            float len = st.length * size * (0.55f + 0.45f * f);
            float wRoot = st.widthRoot * size;
            float wTip = st.widthTip * size;

            Vec3 tip = anchor + dir * len;
            Vec3 q[4] = {
                anchor + perp * wRoot,
                anchor - perp * wRoot,
                tip - perp * wTip,
                tip + perp * wTip,
            };

            float k = opacity * (0.62f + 0.38f * f);

            // Glow: copia mas ancha y tenue DETRAS del plumon.
            Vec3 tipG = anchor + dir * (len * 1.08f);
            Vec3 qg[4] = {
                anchor + perp * (wRoot * 2.2f),
                anchor - perp * (wRoot * 2.2f),
                tipG - perp * (wTip * 2.0f + wRoot * 1.6f),
                tipG + perp * (wTip * 2.0f + wRoot * 1.6f),
            };
            drawQuad(d, qg, cam, fov, sw, sh, withAlpha(color, k * 0.20f));
            drawQuad(d, q, cam, fov, sw, sh, withAlpha(color, k));
        }
    }
}

void drawHalo(ImDrawList* d, const Vec3& center, float radius, float t,
              const CameraView& cam, float fov, float sw, float sh,
              const ImVec4& color, float opacity)
{
    Vec3 c = center;
    c.y += std::sin(t * 1.7f) * 0.035f;

    Vec3 mid;
    if (!worldToScreen(c, cam.pos, cam.yaw, cam.pitch, fov, sw, sh, mid))
        return;
    float th = clampf(180.0f / mid.z, 0.45f, 2.40f);

    const int N = 36;
    ImVec2 pts[N];
    for (int i = 0; i < N; ++i)
    {
        float a = (float)i / (float)N * kPi * 2.0f;
        Vec3 p{c.x + std::cos(a) * radius, c.y, c.z + std::sin(a) * radius};
        Vec3 s;
        if (!worldToScreen(p, cam.pos, cam.yaw, cam.pitch, fov, sw, sh, s))
            return;
        pts[i] = ImVec2(s.x, s.y);
    }

    d->AddPolyline(pts, N, withAlpha(color, opacity * 0.14f), 7.0f * th, ImDrawFlags_Closed);
    d->AddPolyline(pts, N, withAlpha(color, opacity * 0.38f), 4.0f * th, ImDrawFlags_Closed);
    d->AddPolyline(pts, N, withAlpha(color, opacity), 1.8f * th, ImDrawFlags_Closed);
}

}

Cosmetics::Cosmetics()
    : Module("Cosmetics", "Draws wings and a halo on players", Category::Visuals, 0)
{
    markHasSettings();
}

void Cosmetics::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    if (!wings_ && !halo_)
        return;

    if (!fovInited_)
    {
        fov_ = readGameFov();
        fovInited_ = true;
    }

    CameraView cam = getCameraView(local, game.client());
    ImDrawList* d = ImGui::GetForegroundDrawList();
    float t = (float)ImGui::GetTime();

    if (target_ != 1 && local.valid())
    {
        Pose p;
        // Base tick-based (~20Hz) -> interp por-frame a la tasa de refresco
        // ("144Hz"), igual que la camara: el ala calza con el modelo del motor.
        Vec3 base{local.posExact().x, local.pos().y, local.posExact().z};
        p.feet = smooth::interp(smooth::channel::Cosmetics, base);
        p.yaw = local.yaw();
        p.height = local.height();
        drawActor(d, p, cam, sw, sh, t);
    }

    if (target_ != 0)
    {
        for (const Actor& other : game.remotePlayers())
        {
            if (!other.valid())
                continue;

            Pose p;
            Vec3 anchor{other.posExact().x, other.pos().y, other.posExact().z};
            if (smooth_)
                anchor = smooth::entityPos(other.address(), anchor);
            p.feet = anchor;
            p.yaw = other.yaw();
            p.height = other.height();
            drawActor(d, p, cam, sw, sh, t);
        }
    }
}

void Cosmetics::drawActor(ImDrawList* d, const Pose& p, const CameraView& cam,
                          float sw, float sh, float t)
{
    // Camara encima del actor (1ª persona): no dibujar, taparia la pantalla.
    float dist = distance3D(cam.pos, {p.feet.x, p.feet.y + p.height * 0.6f, p.feet.z});
    if (dist < 1.2f)
        return;

    if (wings_)
    {
        const WingStyleCfg& st = kWingStyles[wingStyle_];
        Vec3 anchor{p.feet.x, p.feet.y + p.height * 0.74f, p.feet.z};
        drawWings(d, anchor, p.yaw, t, st, flapSpeed_, size_, cam, fov_, sw, sh,
                  color_, opacity_);
    }

    if (halo_)
    {
        Vec3 hc{p.feet.x, p.feet.y + p.height + 0.26f * size_, p.feet.z};
        drawHalo(d, hc, 0.30f * size_, t, cam, fov_, sw, sh, color_, opacity_);
    }
}

void Cosmetics::drawSettings()
{
    const char* targets[] = {"Self", "Others", "Both"};
    widgets::CCombo("Target##cos", &target_, targets, 3);
    widgets::Checkbox("Wings##cos", &wings_);
    if (wings_)
    {
        ImGui::Indent();
        const char* styles[] = {"Angel", "Demon", "Butterfly"};
        widgets::CCombo("Style##cos", &wingStyle_, styles, 3);
        widgets::CSlider("Size##cos", &size_, 0.4f, 2.0f, "%.2f");
        widgets::CSlider("Flap speed##cos", &flapSpeed_, 0.3f, 4.0f, "%.1f");
        ImGui::Unindent();
    }
    widgets::Checkbox("Halo##cos", &halo_);
    widgets::CSlider("Opacity##cos", &opacity_, 0.10f, 1.00f, "%.2f");
    widgets::Checkbox("Smooth##cos", &smooth_);
    widgets::CSlider("FOV##cos", &fov_, 30.0f, 130.0f, "%.0f");
    widgets::ColorEdit4("Color##cos", (float*)&color_);
    ImGui::TextDisabled("Wings render in world space (need 2nd/3rd person)");
}

MC_REGISTER_MODULE(Cosmetics);

}
