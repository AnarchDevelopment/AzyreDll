#include "Zoom.hpp"

#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Widgets.hpp"
#include "Input/KeyNames.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace mc {

namespace {
// Factor de FOV actual (lo lee worldToScreen para que los overlays sigan
// la proyeccion del zoom). Solo lo toca Zoom::onRender.
float g_fovFactor = 1.0f;
}

float zoomFovFactor()
{
    return g_fovFactor;
}

// Firma de CE (aobscanmodule INJECT, F3 0F 10 87 48 17 00 00):
//   movss xmm0, [rdi+0x1748]  <- carga el FOV de la camara.
// El cave re-ejecuta la instruccion original y multiplica xmm0 por un factor
// nuestro (1.0 = passthrough exacto, el FOV del juego queda intacto):
//   [0..8)   movss xmm0, [rdi+0x1748]     (original)
//   [8..16)  mulss xmm0, [rip+factorSlot] (F3 0F 59 05 <disp32>)
//   [16..21) jmp  site+8                  (E9 <rel32>)
//   [24]     factorSlot: float
static const unsigned char kFovPattern[] = {0xF3, 0x0F, 0x10, 0x87, 0x48, 0x17, 0x00, 0x00};
static constexpr size_t kFactorOffset = 24;

Zoom::Zoom()
    : Module("Zoom", "Smooth zoom with scroll adjustment", Category::Visuals, 0)
{
    markHasSettings();
}

bool Zoom::ensurePatch()
{
    if (!site_)
    {
        site_ = patch::patternScanImage(kFovPattern, sizeof(kFovPattern));
        if (site_)
            MC_LOG("[Zoom] FOV site found at 0x%p", (void*)site_);
        else
        {
            MC_LOG_ERROR("[Zoom] FOV pattern not found");
            return false;
        }
    }

    if (!patch_.init(site_, 8))
        return false;
    if (!patch_.allocCave())
        return false;

    unsigned char shell[32] = {};

    // movss xmm0, [rdi+0x1748] — instruccion original (preserva el
    // comportamiento del juego: sprint, spyglass, etc).
    memcpy(shell + 0, kFovPattern, 8);

    // mulss xmm0, [rip+disp32] -> factorSlot en cave+24.
    // RIP tras la instruccion = cave+16 -> disp = 24 - 16 = 8.
    shell[8] = 0xF3;
    shell[9] = 0x0F;
    shell[10] = 0x59;
    shell[11] = 0x05;
    int32_t disp = (int32_t)kFactorOffset - 16;
    memcpy(shell + 12, &disp, 4);

    // jmp site+8 (RIP tras el jmp = cave+21).
    shell[16] = 0xE9;
    int32_t relBack = (int32_t)((uintptr_t)(site_ + 8) - (patch_.caveAddr() + 21));
    memcpy(shell + 17, &relBack, 4);

    if (!patch_.writeCave(0, shell, 21))
        return false;

    float one = 1.0f;
    if (!patch_.writeCave(kFactorOffset, &one, 4))
        return false;

    if (!patch_.enableJmpToCave())
        return false;
    MC_LOG("[Zoom] FOV override active @ 0x%p (cave 0x%p)", (void*)site_,
           (void*)patch_.caveAddr());
    return true;
}

void Zoom::onEnable()
{
    zooming_ = false;
    factor_ = 1.0f;
    ensurePatch();
}

void Zoom::onDisable()
{
    patch_.disable();
    zooming_ = false;
    g_fovFactor = 1.0f;
}

void Zoom::onShutdown()
{
    patch_.disable();
    patch_.freeCave();
    g_fovFactor = 1.0f;
}

void Zoom::onRender()
{
    if (!patch_.enabled)
        return;

    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;

    bool menuOpen = menu::visible() || menu::hudEditing() || io.WantTextInput;

    // --- Tecla de zoom (Hold / Toggle) ---
    bool down = (key_ > 0 && key_ <= 255) && (GetAsyncKeyState(key_) & 0x8000) != 0;
    if (mode_ == 0)
    {
        // Hold: zoom mientras la tecla este presionada (y sin GUI abierta).
        zooming_ = down && !menuOpen;
    }
    else if (!menuOpen)
    {
        // Toggle: un toque activa, otro desactiva.
        if (down && !keyWasDown_)
            zooming_ = !zooming_;
    }
    keyWasDown_ = down;

    // --- Scroll ajusta el nivel (estilo OptiFine) ---
    if (scroll_ && zooming_ && !menuOpen && io.MouseWheel != 0.0f)
    {
        level_ *= powf(1.25f, io.MouseWheel);
        if (level_ < 1.0f) level_ = 1.0f;
        if (level_ > 30.0f) level_ = 30.0f;
    }

    // --- Factor suave: 1.0 = sin zoom, 1/level = zoom ---
    float target = zooming_ ? (1.0f / level_) : 1.0f;
    if (smooth_)
    {
        factor_ += (target - factor_) * clampf(dt * speed_, 0.0f, 1.0f);
        if (std::fabs(target - factor_) < 0.0005f)
            factor_ = target;
    }
    else
    {
        factor_ = target;
    }

    patch_.writeCave(kFactorOffset, &factor_, 4);
    g_fovFactor = factor_;

    // ========================================================================
    // Camara cinematica: puerto del MouseFilter de OptiFine 1.8.9. Mientras se
    // zoomea, la rotacion persigue la intencion del mouse con el filtro de
    // deltas de OptiFine (doble suavizado + clamp anti-spike). La velocidad
    // (filterAmount) escala con el nivel de zoom: mas zoom = seguimiento mas
    // rapido, zoom debil = lento y apenas perceptible.
    // ========================================================================
    if (!smoothCam_ || !zooming_)
    {
        // OptiFine recrea los filtros al soltar el zoom (new MouseFilter()).
        camSynced_ = false;
        filterX_.reset();
        filterY_.reset();
        return;
    }

    Game& game = Game::get();
    if (!game.localFound())
    {
        camSynced_ = false;
        filterX_.reset();
        filterY_.reset();
        return;
    }

    LocalPlayer lp = game.localPlayer();
    float curYaw = lp.yaw();
    float curPitch = lp.pitch();

    if (!camSynced_)
    {
        // Al iniciar el zoom partir de la rotacion actual (sin salto).
        smoothYaw_ = curYaw;
        smoothPitch_ = curPitch;
        filterX_.reset();
        filterY_.reset();
        camSynced_ = true;
        return;
    }

    // Delta que el juego aplico este frame (la intencion del mouse), con el
    // camino mas corto para el yaw (envuelve en ±180).
    float dYaw = fmodf(curYaw - smoothYaw_ + 540.0f, 360.0f) - 180.0f;
    float dPitch = curPitch - smoothPitch_;

    // OptiFine: filterAmount = 0.05 * f1 (f1 = (sens*0.6+0.2)^3 * 8 = 1 con la
    // sensibilidad por defecto). Aqui ademas escala con el nivel de zoom:
    // 4x = OptiFine exacto (0.05), 30x = rapido, 1x = muy lento. El "por
    // frame @60fps" se convierte a independiente del framerate.
    float amount = clampf(0.05f * (level_ * 0.25f) * camSpeed_, 0.005f, 0.6f);
    float k = 1.0f - powf(1.0f - amount, dt * 60.0f);
    float lerpK = 1.0f - powf(0.5f, dt * 60.0f);

    // El filtro acumula la intencion en `in` (nada se pierde: hay catch-up al
    // soltar el mouse) y emite deltas suavizados que aplicamos a la rotacion.
    float fYaw = filterX_.smooth(dYaw, k, lerpK);
    float fPitch = filterY_.smooth(dPitch, k, lerpK);

    smoothYaw_ += fYaw;
    smoothPitch_ += fPitch;

    if (smoothPitch_ < -90.0f)
        smoothPitch_ = -90.0f;
    if (smoothPitch_ > 90.0f)
        smoothPitch_ = 90.0f;

    mem::write<float>(lp.address() + off::player::Yaw, smoothYaw_);
    mem::write<float>(lp.address() + off::player::Pitch, smoothPitch_);
}

void Zoom::drawSettings()
{
    const char* modes[] = {"Hold", "Toggle"};
    widgets::CCombo("Mode##zoom", &mode_, modes, 2);

    // --- Picker de tecla ---
    if (listening_)
    {
        ImGui::TextDisabled("Press any key... (ESC cancels)");
        bool cancel = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        bool bound = false;
        for (int k = 1; k < 256 && !bound; ++k)
        {
            if (k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON ||
                k == VK_XBUTTON1 || k == VK_XBUTTON2 || k == VK_INSERT)
            {
                bindPrev_[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
                continue;
            }
            bool kd = (GetAsyncKeyState(k) & 0x8000) != 0;
            bool pressed = kd && !bindPrev_[k];
            bindPrev_[k] = kd;
            if (pressed)
            {
                key_ = k;
                bound = true;
            }
        }
        if (bound || cancel)
            listening_ = false;
    }
    else
    {
        char btn[96];
        snprintf(btn, sizeof(btn), "Key: %s##zoomkey",
                 key_ > 0 ? input::keyName(key_).c_str() : "None");
        if (ImGui::SmallButton(btn))
            listening_ = true;
    }

    widgets::CSlider("Zoom##zoom", &level_, 1.0f, 30.0f, "%.1fx");
    widgets::Checkbox("Scroll adjusts zoom##zoom", &scroll_);
    widgets::Checkbox("Smooth##zoom", &smooth_);
    if (smooth_)
        widgets::CSlider("Smooth Speed##zoom", &speed_, 1.0f, 20.0f, "%.1f");
    ImGui::Separator();
    widgets::Checkbox("Smooth Camera##zoom", &smoothCam_);
    if (smoothCam_)
        widgets::CSlider("Camera Speed##zoom", &camSpeed_, 0.25f, 4.0f, "%.2f");

    if (site_)
        ImGui::TextDisabled("Site: 0x%llX%s", (unsigned long long)site_,
                            patch_.enabled ? " (patched)" : "");
    else
        ImGui::TextDisabled("Site: searching pattern...");
}

nlohmann::json Zoom::saveSettings() const
{
    return nlohmann::json{
        {"mode", mode_},
        {"key", key_},
        {"level", level_},
        {"smooth", smooth_},
        {"speed", speed_},
        {"scroll", scroll_},
        {"smoothCam", smoothCam_},
        {"camSpeed", camSpeed_},
    };
}

void Zoom::loadSettings(const nlohmann::json& j)
{
    mode_ = j.value("mode", mode_);
    if (mode_ < 0 || mode_ > 1)
        mode_ = 0;
    key_ = j.value("key", key_);
    if (key_ < 0 || key_ > 255)
        key_ = 0x43;
    level_ = j.value("level", level_);
    if (level_ < 1.0f)
        level_ = 1.0f;
    if (level_ > 30.0f)
        level_ = 30.0f;
    smooth_ = j.value("smooth", smooth_);
    speed_ = j.value("speed", speed_);
    scroll_ = j.value("scroll", scroll_);
    smoothCam_ = j.value("smoothCam", smoothCam_);
    camSpeed_ = j.value("camSpeed", camSpeed_);
    if (camSpeed_ < 0.25f)
        camSpeed_ = 0.25f;
    if (camSpeed_ > 4.0f)
        camSpeed_ = 4.0f;
}

MC_REGISTER_MODULE(Zoom);

}
