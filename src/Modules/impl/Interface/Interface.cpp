#include "Interface.hpp"

#include "Framework/ColorUtils.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/DX11.hpp"
#include "Config/Config.hpp"
#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Engine/Memory/FunctionRvas.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace mc {

namespace {

static const char* kFontNames[] = {
    "Roboto (Default)",
    "Comfortaa",
    "Comfortaa Bold",
    "Mojangles",
    "Mojangles Bold",
    "Open Sans",
    "Open Sans Bold",
    "Product Sans",
    "Product Sans Bold",
    "SF Pro",
    "SF Pro Bold",
    "Sarabun"
};
static const int kFontCount = 12;

// --- Estado global del estilo de GUI ---
int g_style = 2;             // Rise default; 1 remains reserved for legacy config migration.
int g_solsticeAnim = 0;      // 0 = Zoom, 1 = Bounce
float g_solsticeEase = 18.0f;
float g_solsticeBlur = 7.0f;
int g_blurMode = 2;          // Rise backdrop blur default.
float g_riseBlurOpacity = 0.82f;
float g_riseScale = 1.0f;    // Escala de ventana de Rise (0.5 .. 2.0)
float g_glassOpacity = 0.78f;
int g_glassMotion = 0;
std::atomic_bool g_slotEasing{false};
void* g_slotEasingTarget = nullptr;
struct UiVec2
{
    float x;
    float y;
};

using DrawImageFn = uintptr_t(__fastcall*)(void*, uintptr_t, UiVec2*, UiVec2*,
                                           UiVec2*, UiVec2*);
DrawImageFn g_originalDrawImage = nullptr;

struct SlotEaseState
{
    bool initialized = false;
    float displayedX = 0.0f;
    LARGE_INTEGER lastTime{};
} g_slotEaseState;
unsigned int g_slotEaseProbeCount = 0;

bool closeEnough(float value, float expected, float tolerance = 0.0025f)
{
    return std::isfinite(value) && std::fabs(value - expected) <= tolerance;
}

bool isSelectedHotbarImage(uintptr_t textureHandle, const UiVec2* uv,
                           const UiVec2* uvSize, int& textureWidth,
                           int& textureHeight)
{
    if (!uv || !uvSize || !mem::isReadable(reinterpret_cast<uintptr_t>(uv), sizeof(UiVec2)) ||
        !mem::isReadable(reinterpret_cast<uintptr_t>(uvSize), sizeof(UiVec2)))
        return false;

    // Keep the UI JSON's logical coordinates as a direct match too: some
    // texture packs upscale the bitmap without scaling the UI UV properties.
    const bool logicalUvMatch = closeEnough(uv->x, 0.0f) &&
                                closeEnough(uv->y, 22.0f) &&
                                closeEnough(uvSize->x, 24.0f) &&
                                closeEnough(uvSize->y, 22.0f);

    // drawImage receives a TexturePtr. The queued renderer dereferences its
    // resource and reads the dimensions pointer at resource+8 (sub_14032B4A0).
    if (textureHandle && mem::isReadable(textureHandle, sizeof(uintptr_t)))
    {
        const uintptr_t resource = mem::read<uintptr_t>(textureHandle);
        if (resource && mem::isReadable(resource + 8, sizeof(uintptr_t)))
        {
            const uintptr_t dimensions = mem::read<uintptr_t>(resource + 8);
            if (dimensions && mem::isReadable(dimensions, sizeof(int) * 2))
            {
                textureWidth = mem::read<int>(dimensions);
                textureHeight = mem::read<int>(dimensions + sizeof(int));
                if (textureWidth <= 0 || textureHeight <= 0 ||
                    textureWidth > 16384 || textureHeight > 16384)
                    textureWidth = textureHeight = 0;
            }
        }
    }

    if (logicalUvMatch)
        return true;
    if (textureWidth == 0 || textureHeight == 0)
        return false;

    // hud_screen.json binds hotbar_slot_selected_image to gui/gui.png at
    // uv=(0,22), uv_size=(24,22) in the 256x256 logical atlas. Comparing the
    // normalized region supports packs that scale the atlas resolution.
    const float invW = 1.0f / static_cast<float>(textureWidth);
    const float invH = 1.0f / static_cast<float>(textureHeight);
    constexpr float kAtlasScale = 1.0f / 256.0f;
    return closeEnough(uv->x * invW, 0.0f) &&
           closeEnough(uv->y * invH, 22.0f * kAtlasScale) &&
           closeEnough(uvSize->x * invW, 24.0f * kAtlasScale) &&
           closeEnough(uvSize->y * invH, 22.0f * kAtlasScale);
}

uintptr_t __fastcall drawImageDetour(void* context, uintptr_t textureHandle,
                                      UiVec2* position, UiVec2* size,
                                      UiVec2* uv, UiVec2* uvSize)
{
    const bool enabled = g_slotEasing.load(std::memory_order_relaxed);
    if (enabled && position && size && uv && uvSize &&
        mem::isReadable(reinterpret_cast<uintptr_t>(position), sizeof(UiVec2)) &&
        mem::isReadable(reinterpret_cast<uintptr_t>(uv), sizeof(UiVec2)) &&
        mem::isReadable(reinterpret_cast<uintptr_t>(uvSize), sizeof(UiVec2)) &&
        mem::isReadable(reinterpret_cast<uintptr_t>(size), sizeof(UiVec2)))
    {
        int textureWidth = 0;
        int textureHeight = 0;
        const bool selectedUv = isSelectedHotbarImage(
            textureHandle, uv, uvSize, textureWidth, textureHeight);
        const bool selectedImage = selectedUv && std::isfinite(size->x) &&
                                   std::isfinite(size->y) && size->y > 0.0f &&
                                   closeEnough(size->x / size->y, 24.0f / 22.0f, 0.02f);
        if (g_slotEaseProbeCount < 8)
        {
            ++g_slotEaseProbeCount;
            MC_LOG("[SlotEase] drawImage probe#%u tex=%p dims=%dx%d pos=(%.2f,%.2f) size=(%.2f,%.2f) uv=(%.2f,%.2f)+(%.2f,%.2f) match=%d",
                   g_slotEaseProbeCount, reinterpret_cast<void*>(textureHandle),
                   textureWidth, textureHeight, position->x, position->y,
                   size->x, size->y, uv->x, uv->y, uvSize->x, uvSize->y,
                   selectedImage ? 1 : 0);
        }
        if (selectedImage)
        {
            LARGE_INTEGER now{};
            LARGE_INTEGER frequency{};
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&frequency);

            if (!g_slotEaseState.initialized || frequency.QuadPart <= 0)
            {
                g_slotEaseState.displayedX = position->x;
                g_slotEaseState.lastTime = now;
                g_slotEaseState.initialized = true;
                MC_LOG("[SlotEase] selected image matched; tex=%dx%d uv=(%.2f,%.2f)+(%.2f,%.2f) x=%.2f",
                       textureWidth, textureHeight, uv->x, uv->y, uvSize->x, uvSize->y,
                       position->x);
            }
            else
            {
                const float dt = std::clamp(
                    static_cast<float>(now.QuadPart - g_slotEaseState.lastTime.QuadPart) /
                        static_cast<float>(frequency.QuadPart),
                    0.0f, 0.05f);
                g_slotEaseState.lastTime = now;
                constexpr float kResponseSeconds = 0.075f;
                const float blend = 1.0f - std::exp(-dt / kResponseSeconds);
                g_slotEaseState.displayedX +=
                    (position->x - g_slotEaseState.displayedX) * blend;
                position->x = g_slotEaseState.displayedX;
            }
        }
    }

    return g_originalDrawImage
        ? g_originalDrawImage(context, textureHandle, position, size, uv, uvSize)
        : 0;
}

bool installSlotEasingHookImpl()
{
    if (g_slotEasingTarget)
        return true;
    g_slotEasingTarget = reinterpret_cast<void*>(mem::resolve(off::fn::MinecraftUIRenderContextDrawImage));
    if (!g_slotEasingTarget ||
        !hook::create(g_slotEasingTarget, reinterpret_cast<void*>(&drawImageDetour),
                      reinterpret_cast<void**>(&g_originalDrawImage)))
    {
        g_slotEasingTarget = nullptr;
        g_originalDrawImage = nullptr;
        return false;
    }
    MC_LOG("[SlotEase] hooked MinecraftUIRenderContext::drawImage at %p",
           g_slotEasingTarget);
    return true;
}

} // namespace

namespace gui {

int style() { return g_style; }
void setStyle(int s)
{
    // Style 1 used to be Solstice. Keep old configs readable, but migrate that
    // removed style to Rise. The public selector skips the reserved value.
    if (s == 1)
        s = 2;
    g_style = (s == 0 || (s >= 2 && s <= 4)) ? s : 2;
}
int solsticeAnim() { return g_solsticeAnim; }
void setSolsticeAnim(int mode) { g_solsticeAnim = (mode == 1) ? 1 : 0; }
float solsticeEase() { return g_solsticeEase; }
void setSolsticeEase(float v) { g_solsticeEase = v; }
float solsticeBlur() { return g_solsticeBlur; }
void setSolsticeBlur(float v) { g_solsticeBlur = v; }
int blurMode() { return g_blurMode; }
void setBlurMode(int mode) { g_blurMode = (mode < 0 || mode > 2) ? 0 : mode; }
float riseBlurOpacity() { return g_riseBlurOpacity; }
void setRiseBlurOpacity(float opacity) { g_riseBlurOpacity = (opacity < 0.0f) ? 0.0f : (opacity > 1.0f ? 1.0f : opacity); }
float riseScale() { return g_riseScale; }
void setRiseScale(float v) { g_riseScale = (v < 0.5f) ? 0.5f : (v > 2.0f ? 2.0f : v); }
float glassOpacity() { return g_glassOpacity; }
void setGlassOpacity(float v) { g_glassOpacity = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v); }
int glassMotion() { return g_glassMotion; }
void setGlassMotion(int mode) { g_glassMotion = (mode < 0 || mode > 2) ? 0 : mode; }
bool slotEasing() { return g_slotEasing.load(std::memory_order_relaxed); }
void setSlotEasing(bool enabled)
{
    const bool previous = g_slotEasing.exchange(enabled, std::memory_order_relaxed);
    if (previous != enabled)
    {
        g_slotEaseState = {};
        g_slotEaseProbeCount = 0;
        MC_LOG("[SlotEase] %s", enabled ? "enabled" : "disabled");
    }
}
bool installSlotEasingHook() { return installSlotEasingHookImpl(); }
void uninstallSlotEasingHook()
{
    if (!g_slotEasingTarget)
        return;
    hook::remove(g_slotEasingTarget);
    g_slotEasingTarget = nullptr;
    g_originalDrawImage = nullptr;
    g_slotEaseState = {};
}
} // namespace gui

InterfaceModule::InterfaceModule()
    : Module("Interface", "Customize the client appearance", Category::Interface, 0)
{
    markHasSettings();
    // Siempre visible, no es toggleable de la misma manera que otros módulos.
    setEnabled(true);
}

void InterfaceModule::drawSettings()
{
    // ---- Estilo de la GUI ----
    ImGui::SeparatorText("GUI Style");
    const char* styles[] = {"Azyre", "Rise", "Glass", "Packet 3.1"};
    int cur = gui::style() == 0 ? 0 : gui::style() - 1;
    if (widgets::CCombo("Style##iface", &cur, styles, 4))
    {
        gui::setStyle(cur == 0 ? 0 : cur + 1);
        config::saveAll();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(live switch)");

    if (gui::style() == 2)
    {
        ImGui::Indent();
        float rScale = gui::riseScale();
        if (widgets::CSlider("UI Scale##iface_rise", &rScale, 0.6f, 1.5f, "%.2f"))
        {
            gui::setRiseScale(rScale);
            config::saveAll();
        }
        ImGui::Unindent();
    }

    if (gui::style() == 3)
    {
        ImGui::Indent();
        float opacity = gui::glassOpacity();
        if (widgets::CSlider("Glass Opacity##iface_glass", &opacity, 0.25f, 0.95f, "%.2f"))
        {
            gui::setGlassOpacity(opacity);
            config::saveAll();
        }
        const char* motions[] = {"Fluid", "Spring", "Reduced"};
        int motion = gui::glassMotion();
        if (widgets::CCombo("Motion##iface_glass", &motion, motions, 3))
        {
            gui::setGlassMotion(motion);
            config::saveAll();
        }
        ImGui::TextDisabled("Backdrop blur uses the live DX11 frame capture.");
        ImGui::Unindent();
    }

    ImGui::SeparatorText("Gameplay UI");
    bool slotEasing = gui::slotEasing();
    if (widgets::Checkbox("Slot Easing##iface", &slotEasing))
    {
        gui::setSlotEasing(slotEasing);
        config::saveAll();
    }
    ImGui::TextDisabled("Smoothly moves the native hotbar selection");

    // ---- Color de acento ----
    ImGui::SeparatorText("Accent Color");
    float col3[3] = {theme::Accent.x, theme::Accent.y, theme::Accent.z};
    if (widgets::ColorEdit3("##accent_color", col3,
                          ImGuiColorEditFlags_NoInputs |
                          ImGuiColorEditFlags_PickerHueWheel |
                          ImGuiColorEditFlags_DisplayHex))
    {
        theme::Accent.x = col3[0];
        theme::Accent.y = col3[1];
        theme::Accent.z = col3[2];

        // Recalcular AccentDeep como una versión más oscura del mismo tono.
        theme::AccentDeep.x = col3[0] * 0.62f;
        theme::AccentDeep.y = col3[1] * 0.62f;
        theme::AccentDeep.z = col3[2] * 0.62f;

        // Aceno plano: el gradiente animado de Rise queda neutro.
        theme::Accent2 = ImVec4(col3[0], col3[1], col3[2], 1.0f);
        theme::Accent3 = ImVec4(col3[0], col3[1], col3[2], 1.0f);
        theme::AccentTri = false;
        theme::AccentRainbow = false;

        theme::apply();
        config::saveAll();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Primary accent (hacks glow, toggle, slider...)");

    // Presets de color rápidos
    ImGui::TextDisabled("Presets:");
    ImGui::SameLine();
    struct Preset { const char* name; float r, g, b; };
    static const Preset kPresets[] = {
        {"Azyre Blue",   0.298f, 0.761f, 1.000f},
        {"Purple",       0.678f, 0.384f, 1.000f},
        {"Green",        0.282f, 0.870f, 0.549f},
        {"Orange",       1.000f, 0.592f, 0.196f},
        {"Red",          1.000f, 0.298f, 0.298f},
        {"Pink",         1.000f, 0.400f, 0.706f},
        {"White",        0.950f, 0.950f, 0.960f},
    };
    for (int i = 0; i < (int)(sizeof(kPresets)/sizeof(kPresets[0])); ++i)
    {
        if (i > 0) ImGui::SameLine();
        ImGui::PushID(i);
        float pc[3] = {kPresets[i].r, kPresets[i].g, kPresets[i].b};
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(pc[0]*0.35f, pc[1]*0.35f, pc[2]*0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(pc[0]*0.55f, pc[1]*0.55f, pc[2]*0.55f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(pc[0],       pc[1],       pc[2],       1.0f));
        if (ImGui::SmallButton(kPresets[i].name))
        {
            theme::Accent.x    = pc[0];
            theme::Accent.y    = pc[1];
            theme::Accent.z    = pc[2];
            theme::AccentDeep.x = pc[0] * 0.62f;
            theme::AccentDeep.y = pc[1] * 0.62f;
            theme::AccentDeep.z = pc[2] * 0.62f;
            theme::Accent2 = ImVec4(pc[0], pc[1], pc[2], 1.0f);
            theme::Accent3 = ImVec4(pc[0], pc[1], pc[2], 1.0f);
            theme::AccentTri = false;
            theme::AccentRainbow = false;
            theme::apply();
            config::saveAll();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopID();
    }

    ImGui::Spacing();

    // ---- Blur (Backdrop Acrylic) ----
    ImGui::SeparatorText("Backdrop Blur");
    {
        const char* blurModes[] = {"Native", "Solstice", "Rise"};
        int bm = gui::blurMode();
        if (widgets::CCombo("Blur Mode##iface", &bm, blurModes, 3))
        {
            gui::setBlurMode(bm);
            config::saveAll();
        }
        if (gui::blurMode() == 2)
        {
            float opacity = gui::riseBlurOpacity();
            if (widgets::CSlider("Rise Blur Opacity##iface", &opacity, 0.0f, 1.0f, "%.2f"))
            {
                gui::setRiseBlurOpacity(opacity);
                config::saveAll();
            }
        }
    }
    ImGui::TextDisabled("State: %s", (blur::ready() ? "Active" : "Unavailable"));
    ImGui::SameLine();
    if (ImGui::SmallButton("Info##blur_info"))
        ImGui::OpenPopup("blur_info_popup");
    if (ImGui::BeginPopup("blur_info_popup"))
    {
        ImGui::TextDisabled("Blur is applied behind the menu and on acrylic widgets.");
        ImGui::TextDisabled("It uses a Gaussian two-pass pipeline captured from the");
        ImGui::TextDisabled("game backbuffer each frame.");
        ImGui::EndPopup();
    }

    ImGui::Spacing();

    // ---- UI Scale ----
    ImGui::SeparatorText("UI");
    ImGui::TextDisabled("Menu size is fixed at 760x540. Font and rounding below.");

    if (widgets::CSlider("Window Rounding##iface", &theme::WindowRounding, 0.0f, 18.0f, "%.0f px"))
        theme::apply();
    if (ImGui::IsItemDeactivatedAfterEdit())
        config::saveAll();

    if (widgets::CSlider("Item Spacing##iface", &theme::ItemSpacingY, 2.0f, 14.0f, "%.0f px"))
        theme::apply();
    if (ImGui::IsItemDeactivatedAfterEdit())
        config::saveAll();

    ImGui::Spacing();
    
    ImGui::SeparatorText("Typography");
    if (widgets::CCombo("Global Font", &theme::FontIndex, kFontNames, kFontCount))
    {
        ImGuiIO& io = ImGui::GetIO();
        if (theme::FontIndex >= 0 && theme::FontIndex < io.Fonts->Fonts.Size)
        {
            io.FontDefault = io.Fonts->Fonts[theme::FontIndex];
            config::saveAll();
        }
    }

    ImGui::Spacing();

    // ---- Información del sistema ----
    ImGui::SeparatorText("System");
    ImGui::TextDisabled("Renderer: DX11  |  Backend: ImGui %s", ImGui::GetVersion());
    ImGui::TextDisabled("Display: %.0f x %.0f", ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    ImGui::TextDisabled("FPS: %.1f  (%.2f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
}

nlohmann::json InterfaceModule::saveSettings() const
{
    nlohmann::json j;
    j["accent_r"]       = theme::Accent.x;
    j["accent_g"]       = theme::Accent.y;
    j["accent_b"]       = theme::Accent.z;
    j["accent2_r"]      = theme::Accent2.x;
    j["accent2_g"]      = theme::Accent2.y;
    j["accent2_b"]      = theme::Accent2.z;
    j["accent3_r"]      = theme::Accent3.x;
    j["accent3_g"]      = theme::Accent3.y;
    j["accent3_b"]      = theme::Accent3.z;
    j["accent_tri"]     = theme::AccentTri;
    j["accent_rainbow"]  = theme::AccentRainbow;
    j["deep_r"]         = theme::AccentDeep.x;
    j["deep_g"]         = theme::AccentDeep.y;
    j["deep_b"]         = theme::AccentDeep.z;
    j["win_rounding"]   = theme::WindowRounding;
    j["item_spacing_y"] = theme::ItemSpacingY;
    j["font_index"]     = theme::FontIndex;
    j["gui_style"]      = gui::style();
    j["blur_mode"]      = gui::blurMode();
    j["rise_blur_opacity"] = gui::riseBlurOpacity();
    j["sol_anim"]       = gui::solsticeAnim();
    j["sol_ease"]       = gui::solsticeEase();
    j["sol_blur"]       = gui::solsticeBlur();
    j["sol_theme"]      = colors::theme();
    j["sol_speed"]      = colors::colorSpeed();
    j["sol_sat"]        = colors::saturation();
    j["rise_scale"]     = gui::riseScale();
    j["glass_opacity"]  = gui::glassOpacity();
    j["glass_motion"]   = gui::glassMotion();
    j["slot_easing"]    = gui::slotEasing();
    return j;
}

void InterfaceModule::loadSettings(const nlohmann::json& j)
{
    if (j.contains("accent_r")) theme::Accent.x    = j["accent_r"].get<float>();
    if (j.contains("accent_g")) theme::Accent.y    = j["accent_g"].get<float>();
    if (j.contains("accent_b")) theme::Accent.z    = j["accent_b"].get<float>();
    if (j.contains("accent2_r")) theme::Accent2.x  = j["accent2_r"].get<float>();
    if (j.contains("accent2_g")) theme::Accent2.y  = j["accent2_g"].get<float>();
    if (j.contains("accent2_b")) theme::Accent2.z  = j["accent2_b"].get<float>();
    if (j.contains("accent3_r")) theme::Accent3.x  = j["accent3_r"].get<float>();
    if (j.contains("accent3_g")) theme::Accent3.y  = j["accent3_g"].get<float>();
    if (j.contains("accent3_b")) theme::Accent3.z  = j["accent3_b"].get<float>();
    if (j.contains("accent_tri")) theme::AccentTri = j["accent_tri"].get<bool>();
    if (j.contains("accent_rainbow")) theme::AccentRainbow = j["accent_rainbow"].get<bool>();
    if (j.contains("deep_r"))   theme::AccentDeep.x = j["deep_r"].get<float>();
    if (j.contains("deep_g"))   theme::AccentDeep.y = j["deep_g"].get<float>();
    if (j.contains("deep_b"))   theme::AccentDeep.z = j["deep_b"].get<float>();
    if (j.contains("win_rounding"))
        theme::WindowRounding = j["win_rounding"].get<float>();
    if (j.contains("item_spacing_y"))
        theme::ItemSpacingY   = j["item_spacing_y"].get<float>();
    if (j.contains("font_index"))
        theme::FontIndex = j["font_index"].get<int>();
    if (j.contains("gui_style"))
        gui::setStyle(j["gui_style"].get<int>());
    if (j.contains("blur_mode"))
        gui::setBlurMode(j["blur_mode"].get<int>());
    if (j.contains("rise_blur_opacity"))
        gui::setRiseBlurOpacity(j["rise_blur_opacity"].get<float>());
    if (j.contains("sol_anim"))
        gui::setSolsticeAnim(j["sol_anim"].get<int>());
    if (j.contains("sol_ease"))
        gui::setSolsticeEase(j["sol_ease"].get<float>());
    if (j.contains("sol_blur"))
        gui::setSolsticeBlur(j["sol_blur"].get<float>());
    if (j.contains("sol_theme"))
        colors::setTheme(j["sol_theme"].get<int>());
    if (j.contains("sol_speed"))
        colors::setColorSpeed(j["sol_speed"].get<float>());
    if (j.contains("sol_sat"))
        colors::setSaturation(j["sol_sat"].get<float>());
    if (j.contains("rise_scale"))
        gui::setRiseScale(j["rise_scale"].get<float>());
    if (j.contains("glass_opacity"))
        gui::setGlassOpacity(j["glass_opacity"].get<float>());
    if (j.contains("glass_motion"))
        gui::setGlassMotion(j["glass_motion"].get<int>());
    if (j.contains("slot_easing"))
        gui::setSlotEasing(j["slot_easing"].get<bool>());
    // NO tocar ImGui aqui: loadSettings corre en Bootstrap, ANTES del primer
    // Present donde ImGui::CreateContext() ocurre. ImGui::GetIO() con contexto
    // null = access violation. El primer Present ya aplica FontIndex:
    //   io.FontDefault = io.Fonts->Fonts[theme::FontIndex];
}

MC_REGISTER_MODULE(InterfaceModule);

}
