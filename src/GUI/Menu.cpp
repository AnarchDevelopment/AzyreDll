#include "Menu.hpp"

#include "Config/Config.hpp"
#include "Features/HudRegistry.hpp"
#include "Features/PlaceStats.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Input/WheelHook.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Interface/Interface.hpp"
#include "Modules/impl/Interface/Style/sAzyre/sAzyre.hpp"
#include "Modules/impl/Interface/Style/sSolstice/sSolstice.hpp"
#include "Render/DX11.hpp"
#include "Render/D2D/D2D.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Scripting/Scripting.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <windows.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace mc::menu {

static bool g_visible = false;
static bool g_debugRaw = false;
static bool g_debugBlur = false;
static bool g_debugCompRaw = false;
static bool g_hudEditing = false;
static int g_tab = 0;
static float g_anim = 0.0f;
static ImVec2 g_menuMin = ImVec2(0, 0);
static ImVec2 g_menuMax = ImVec2(0, 0);
static float g_glassRounding = 8.0f;

static bool g_debugOpen = false;
static char g_search[64] = "";

static std::map<const Module*, bool> g_expanded;
static std::map<const Module*, float> g_expandAnim;
static std::map<const Module*, float> g_switchAnim;
static std::map<const Module*, float> g_rowHoverAnim;

bool visible()
{
    return g_visible;
}

float animationAlpha()
{
    return g_anim;
}

void windowRect(ImVec2& outMin, ImVec2& outMax)
{
    outMin = g_menuMin;
    outMax = g_menuMax;
}

float glassRadius()
{
    return g_glassRounding;
}

bool hudEditing()
{
    return g_hudEditing;
}

void setHudEditing(bool v)
{
    g_hudEditing = v;
}

void handleInput()
{
    static bool wasDown = false;
    bool isDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (isDown && !wasDown)
    {
        g_visible = !g_visible;
        if (!g_visible)
            config::saveAll();
    }
    wasDown = isDown;

    // Debug: F9 muestra la captura RAW fullscreen (sin blur ni scrim).
    static bool dbgWasDown = false;
    bool dbgDown = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (dbgDown && !dbgWasDown)
        g_debugRaw = !g_debugRaw;
    dbgWasDown = dbgDown;

    // Debug: F10 muestra la textura BLUREADA fullscreen (sin scrim ni alpha
    // parcial) — verificacion directa del contenido del pipeline de blur.
    static bool dbg2WasDown = false;
    bool dbg2Down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (dbg2Down && !dbg2WasDown)
        g_debugBlur = !g_debugBlur;
    dbg2WasDown = dbg2Down;

    // Debug: F11 hace que el composite Mica pinte la captura RAW (sin blur)
    // — aisla "el composite dibuja" de "la cadena de blur produce contenido".
    static bool dbg3WasDown = false;
    bool dbg3Down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (dbg3Down && !dbg3WasDown)
        blur::setDebugCompositeRaw(!g_debugCompRaw);
    dbg3WasDown = dbg3Down;
}

static const char* keyName(int key)
{
    static char buf[16];
    if (key <= 0)
        return "NONE";
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
    {
        buf[0] = (char)key;
        buf[1] = 0;
        return buf;
    }
    snprintf(buf, sizeof(buf), "VK_%02X", key);
    return buf;
}

static bool containsCI(const std::string& hay, const char* needle)
{
    size_t nl = strlen(needle);
    if (nl == 0)
        return true;
    if (hay.size() < nl)
        return false;
    std::string h;
    h.reserve(hay.size());
    for (char c : hay)
        h.push_back((char)std::tolower((unsigned char)c));
    std::string n;
    n.reserve(nl);
    for (size_t i = 0; i < nl; ++i)
        n.push_back((char)std::tolower((unsigned char)needle[i]));
    return h.find(n) != std::string::npos;
}

// Sombra suave y panel acrilico viven en widgets:: (SoftShadow/AcrylicPanel).

static void drawStatusPill(ImDrawList* d, const ImVec2& linePos, float winRight)
{
    bool inWorld = Game::get().localFound();
    const char* st = inWorld ? "IN WORLD" : "MENU";
    ImVec2 ts = ImGui::CalcTextSize(st);
    float pillRight = winRight - 40.0f;
    ImVec2 pMin(pillRight - ts.x - 30.0f, linePos.y - 4.0f);
    ImVec2 pMax(pillRight, linePos.y + ts.y + 4.0f);
    float h = pMax.y - pMin.y;

    ImU32 bg = inWorld ? IM_COL32(107, 203, 98, 26) : IM_COL32(255, 255, 255, 16);
    ImU32 dot = inWorld ? IM_COL32(107, 203, 98, 235) : IM_COL32(150, 155, 162, 235);
    ImU32 txt = inWorld ? IM_COL32(138, 226, 132, 255) : IM_COL32(190, 193, 198, 255);

    d->AddRectFilled(pMin, pMax, bg, h * 0.5f);
    d->AddCircleFilled(ImVec2(pMin.x + 9.0f, (pMin.y + pMax.y) * 0.5f), 3.2f, dot, 10);
    d->AddText(ImVec2(pMin.x + 18.0f, pMin.y + 4.0f), txt, st);
}

namespace {

// easeExpoOut a 0.85x: exponencial con exponente suavizado (menos agresivo
// que el 10x original — se asienta sin sentirse cortado).
float easeExpoOut(float t)
{
    return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -8.5f * t);
}

} // namespace

// Boton center-bottom para entrar/salir del HUD editor
// Solo visible con el menu abierto
static void drawHudEditorButton()
{
    // El boton solo aparece con el menu abierto
    if (g_anim <= 0.01f)
        return;

    // Asegurar que el registry tenga el path del config (para que las
    // posiciones carguen correctamente incluso tras reinyectar)
    hud::Registry::get().setConfigPath(config::baseDir() + "\\hud.json");

    ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImVec2 mouse = ImGui::GetIO().MousePos;

    float bw = 90.0f;
    float bh = 24.0f;
    ImVec2 bMin(disp.x * 0.5f - bw * 0.5f, disp.y - bh - 8.0f);
    ImVec2 bMax(bMin.x + bw, bMin.y + bh);

    bool hovered = mouse.x >= bMin.x && mouse.x <= bMax.x && mouse.y >= bMin.y && mouse.y <= bMax.y;
    bool clicked = hovered && ImGui::IsMouseClicked(0);

    if (clicked)
    {
        g_hudEditing = !g_hudEditing;
        if (!g_hudEditing)
            hud::Registry::get().save();
    }

    ImU32 bg = g_hudEditing ? IM_COL32(60, 200, 120, 200) : IM_COL32(20, 22, 28, 180);
    ImU32 tx = g_hudEditing ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 185, 192, 220);
    if (hovered)
    {
        bg = g_hudEditing ? IM_COL32(80, 220, 140, 220) : IM_COL32(30, 32, 40, 200);
        tx = IM_COL32(255, 255, 255, 255);
    }

    d->AddRectFilled(bMin, bMax, bg, bh * 0.5f);
    const char* label = g_hudEditing ? "Exit Editor" : "Edit HUD";
    ImVec2 ts = ImGui::CalcTextSize(label);
    d->AddText(ImVec2(bMin.x + (bw - ts.x) * 0.5f, bMin.y + (bh - ts.y) * 0.5f), tx, label);
}

// HUD Editor: arrastrar elementos, botones Save / Reset en center-top
static int s_hudSnap = 0; // 0 = sin snap
static bool s_hudTabWasDown = false;

static void renderHudEditor()
{
    if (!g_hudEditing)
        return;

    auto& reg = hud::Registry::get();
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImVec2 mouse = ImGui::GetIO().MousePos;

    // TAB: ciclar snap distance
    bool tabDown = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
    if (tabDown && !s_hudTabWasDown)
    {
        s_hudSnap += 5;
        if (s_hudSnap > 100)
            s_hudSnap = 0;
    }
    s_hudTabWasDown = tabDown;

    // Grid de snap
    if (s_hudSnap > 0)
    {
        for (float x = 0; x < disp.x; x += s_hudSnap)
            d->AddLine(ImVec2(x, 0), ImVec2(x, disp.y), IM_COL32(255, 255, 255, 15));
        for (float y = 0; y < disp.y; y += s_hudSnap)
            d->AddLine(ImVec2(0, y), ImVec2(disp.x, y), IM_COL32(255, 255, 255, 15));
    }

    // Overlay oscurecido
    d->AddRectFilled(ImVec2(0, 0), disp, IM_COL32(0, 0, 0, 50));

    static bool dragging = false;
    static hud::Element* dragged = nullptr;
    static ImVec2 dragOffset(0, 0);

    // Iniciar drag
    if (ImGui::IsMouseClicked(0) && !dragging)
    {
        hud::Element* hit = reg.hitTest(mouse);
        if (hit)
        {
            dragging = true;
            dragged = hit;
            dragOffset = ImVec2(mouse.x - hit->pos.x, mouse.y - hit->pos.y);
        }
    }

    // Durante el drag
    if (dragging && dragged)
    {
        ImVec2 newPos(mouse.x - dragOffset.x, mouse.y - dragOffset.y);

        // Snap a grid
        if (s_hudSnap > 0)
        {
            newPos.x = std::round(newPos.x / s_hudSnap) * s_hudSnap;
            newPos.y = std::round(newPos.y / s_hudSnap) * s_hudSnap;
        }

        if (newPos.x < 0) newPos.x = 0;
        if (newPos.y < 0) newPos.y = 0;
        if (newPos.x + dragged->size.x > disp.x) newPos.x = disp.x - dragged->size.x;
        if (newPos.y + dragged->size.y > disp.y) newPos.y = disp.y - dragged->size.y;

        dragged->pos = newPos;

        if (!ImGui::IsMouseDown(0))
        {
            dragging = false;
            dragged = nullptr;
        }
    }

    // Outlines + labels de todos los elementos
    for (auto& e : reg.all())
    {
        if (!e.visible || e.size.x <= 0)
            continue;

        ImU32 outline = (&e == dragged) ? theme::AccentU32(0.9f) : IM_COL32(255, 255, 255, 100);
        d->AddRect(e.pos, ImVec2(e.pos.x + e.size.x, e.pos.y + e.size.y), outline, 4.0f, 0, 2.0f);

        ImVec2 ts = ImGui::CalcTextSize(e.id.c_str());
        d->AddRectFilled(ImVec2(e.pos.x, e.pos.y - ts.y - 4),
                        ImVec2(e.pos.x + ts.x + 8, e.pos.y), IM_COL32(0, 0, 0, 160), 3.0f);
        d->AddText(ImVec2(e.pos.x + 4, e.pos.y - ts.y - 2),
                   IM_COL32(255, 255, 255, 200), e.id.c_str());
    }

    // Botones Save / Reset en center-top
    float btnW = 90.0f;
    float btnH = 28.0f;
    float gap = 10.0f;
    float totalW = btnW * 2 + gap;
    ImVec2 sMin(disp.x * 0.5f - totalW * 0.5f, 12.0f);
    ImVec2 rMin(sMin.x + btnW + gap, sMin.y);

    auto drawBtn = [&](const ImVec2& bMin, const char* label, bool& clickedOut, ImU32 accent)
    {
        ImVec2 bMax(bMin.x + btnW, bMin.y + btnH);
        bool hov = mouse.x >= bMin.x && mouse.x <= bMax.x && mouse.y >= bMin.y && mouse.y <= bMax.y;
        clickedOut = hov && ImGui::IsMouseClicked(0);

        ImU32 bg = hov ? accent : IM_COL32(20, 22, 28, 200);
        d->AddRectFilled(bMin, bMax, bg, btnH * 0.35f);
        ImVec2 ts = ImGui::CalcTextSize(label);
        d->AddText(ImVec2(bMin.x + (btnW - ts.x) * 0.5f, bMin.y + (btnH - ts.y) * 0.5f),
                   IM_COL32(255, 255, 255, 240), label);
    };

    bool saveClicked = false;
    bool resetClicked = false;
    drawBtn(sMin, "Save", saveClicked, IM_COL32(60, 200, 120, 220));
    drawBtn(rMin, "Reset positions", resetClicked, IM_COL32(200, 80, 80, 220));

    if (saveClicked)
    {
        reg.save();
        g_hudEditing = false;
    }
    if (resetClicked)
    {
        reg.reset();
    }

    // Info de snap en la esquina inferior derecha
    if (s_hudSnap > 0)
    {
        char snapText[32];
        snprintf(snapText, sizeof(snapText), "Snap: %d px", s_hudSnap);
        ImVec2 ts = ImGui::CalcTextSize(snapText);
        d->AddText(ImVec2(disp.x - ts.x - 12, disp.y - ts.y - 12),
                   IM_COL32(255, 255, 255, 160), snapText);
    }

    // ESC: salir del editor
    static bool escWasDown = false;
    bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDown && !escWasDown)
    {
        reg.save();
        g_hudEditing = false;
    }
    escWasDown = escDown;
}

void render()
{
    theme::apply();

    // En modo Solstice el vidrio lo hace D2D: apagar el composite Glass nativo.
    blur::setMenuGlass(gui::blurMode() == 0);

    float dt = ImGui::GetIO().DeltaTime;
    float speed = dt * 9.0f;
    if (speed > 1.0f)
        speed = 1.0f;
    float target = g_visible ? 1.0f : 0.0f;
    g_anim += (target - g_anim) * speed;
    if (g_visible && g_anim > 0.995f)
        g_anim = 1.0f;
    if (!g_visible && g_anim < 0.005f)
        g_anim = 0.0f;

    if (!g_visible && g_anim <= 0.0f)
        return;

    // Dispatch por estilo de GUI (modulo Interface): el fade g_anim se
    // comparte; cada estilo maneja su propio backdrop y layout.
    if (gui::style() == 1)
        gui::sSolstice::render(g_anim);
    else
        gui::sAzyre::render(g_anim);
}

namespace {

// ---- Glass Fluent surface --------------------------------------------------
// El vidrio en si lo compone el BlurHelper en el backbuffer (rounded-rect SDF
// con refraccion + luz de borde, ver BlurShaders kGlassPS). Aqui solo:
//   - fallback solido translucido si el blur no pudo correr este frame,
//   - un scrim leve para fijar el contraste del contenido,
//   - strokes Fluent (hairline + luz superior).
void drawGlassSurface(ImDrawList* d, const ImVec2& mn, const ImVec2& mx,
                      float rounding, float alpha, const ImVec2& disp)
{
    if (alpha <= 0.01f || disp.x <= 0.0f || disp.y <= 0.0f)
        return;

    // Elevacion: sombra suave bajo el panel (Fluent "elevated surface").
    widgets::SoftShadow(ImGui::GetBackgroundDrawList(), mn, mx, rounding,
                        46.0f * alpha, 26.0f);

    // Modo Solstice: blur con Direct2D sobre el rect de la ventana.
#ifdef _MSC_VER
    if (gui::blurMode() == 1)
    {
        D2D::addBlur(d, 14.0f, ImVec4(mn.x, mn.y, mx.x, mx.y), rounding);
        d->AddRectFilled(mn, mx, IM_COL32(20, 24, 32, (int)(90.0f * alpha)), rounding);
        d->AddRect(mn, mx, IM_COL32(255, 255, 255, (int)(30.0f * alpha)), rounding, 0, 1.0f);
        float inset = rounding + 0.5f;
        d->AddLine(ImVec2(mn.x + inset, mn.y + 0.5f), ImVec2(mx.x - inset, mn.y + 0.5f),
                   IM_COL32(255, 255, 255, (int)(54.0f * alpha)), 1.0f);
        return;
    }
#endif

    const bool haveGlass = blur::ready() && blur::srv();
    if (haveGlass)
    {
        // Vidrio activo: scrim minimo para no duplicar el frost del shader.
        d->AddRectFilled(mn, mx, IM_COL32(24, 28, 36, (int)(40.0f * alpha)), rounding);
    }
    else
    {
        // Sin captura: tarjeta solida para no perder legibilidad.
        d->AddRectFilled(mn, mx, IM_COL32(30, 31, 35, (int)(238.0f * alpha)), rounding);
    }

    // Strokes: borde exterior sutil + highlight interior superior (luz Fluent).
    d->AddRect(mn, mx, IM_COL32(255, 255, 255, (int)(30.0f * alpha)),
               rounding, 0, 1.0f);
    {
        float inset = rounding + 0.5f;
        d->AddLine(ImVec2(mn.x + inset, mn.y + 0.5f),
                   ImVec2(mx.x - inset, mn.y + 0.5f),
                   IM_COL32(255, 255, 255, (int)(54.0f * alpha)), 1.0f);
    }
}

} // namespace

void renderAzyre(float t)
{
    ModuleManager& mgr = ModuleManager::get();
    float dt = ImGui::GetIO().DeltaTime;

    // EaseExpoOut 0.85x para todo: arranque fuerte, settle suave.
    float ease = easeExpoOut(t);
    float scale = 0.15f + 0.85f * ease;

    ImVec2 disp = ImGui::GetIO().DisplaySize;

    // Radio glass Fluent del panel (respeta el ajuste del usuario en Interface).
    // Se publica por menu::glassRadius() para que el composite Glass HLSL
    // (BlurHelper) pinte el frost con el mismo redondeo.
    g_glassRounding = theme::WindowRounding;
    float glassRounding = g_glassRounding;

    if (ease > 0.01f)
    {
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(0.0f, 0.0f), disp, IM_COL32(6, 8, 10, (int)(25.0f * ease)));

        // Debug: captura RAW (F9). Si se ve el frame del juego aqui, la
        // captura y el path de texturas funcionan; si se ve negro/basura,
        // el problema esta en la cadena de blur.
        if (g_debugRaw && blur::captureSrv())
        {
            ImGui::GetForegroundDrawList()->AddImage(
                ImTextureRef((void*)blur::captureSrv()),
                ImVec2(0.0f, 0.0f), disp, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
            ImGui::GetForegroundDrawList()->AddText(ImVec2(12.0f, 12.0f),
                                                   IM_COL32(255, 80, 80, 255),
                                                   "RAW CAPTURE - F9 para ocultar");
        }

        // Debug: textura BLUREADA (F10) a alpha completa, sin scrim.
        if (g_debugBlur && blur::srv())
        {
            ImGui::GetForegroundDrawList()->AddImage(
                ImTextureRef((void*)blur::srv()),
                ImVec2(0.0f, 0.0f), disp, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
            ImGui::GetForegroundDrawList()->AddText(ImVec2(12.0f, 12.0f),
                                                   IM_COL32(80, 255, 80, 255),
                                                   "BLURRED TEXTURE - F10 para ocultar");
        }
    }

    ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760.0f * scale, 540.0f * scale), ImGuiCond_Always);

    // Glass Fluent: la ventana no pinta su propio fondo ni borde; el panel
    // acrilico se dibuja a mano con esquinas redondeadas (drawGlassSurface)
    // para que el blur y el redondeo queden limpios.
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    float winAlpha = 0.10f + 0.90f * ease;
    if (winAlpha > 1.0f)
        winAlpha = 1.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, winAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 10.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f * scale, 5.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f * scale, 6.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, glassRounding);

    bool open = g_visible;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoTitleBar;
    bool isWindowOpen = ImGui::Begin("Azyre", g_visible ? &open : nullptr, flags);
    if (isWindowOpen)
    {
        theme::apply();
        ImVec2 winPos = ImGui::GetWindowPos();
        float winW = ImGui::GetWindowWidth();
        g_menuMin = winPos;
        g_menuMax = ImVec2(winPos.x + winW, winPos.y + ImGui::GetWindowSize().y);

        // Panel Glass Fluent: backdrop acrilico + scrim + strokes Fluent.
        drawGlassSurface(ImGui::GetWindowDrawList(), winPos,
                         ImVec2(g_menuMax.x, g_menuMax.y), glassRounding, ease, disp);

        // ================= Header =================
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Accent);
        ImGui::Text("AZYRE");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("1.1.0  Bedrock Win10 DX11");

        ImGui::SameLine(winW - 36.0f);
        if (ImGui::SmallButton("X##close_menu"))
            open = false;

        ImGui::PushStyleColor(ImGuiCol_Separator,
                              ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.40f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        // ================= Layout =================
        static int lastTabShown = -1;
        static float tabAnim = 1.0f;
        if (g_tab != lastTabShown)
        {
            lastTabShown = g_tab;
            tabAnim = 0.0f;
        }
        tabAnim = clampf(tabAnim + dt * 6.0f, 0.0f, 1.0f);
        float tabEase = widgets::EaseOutExpo(tabAnim);

        const float sideW = 152.0f;
        const float tabH = 30.0f;
        const float tabGap = 5.0f;

        ImVec2 areaBase = ImGui::GetCursorPos();
        float totalW = ImGui::GetContentRegionAvail().x;
        float contentW = totalW - sideW - 6.0f;

        bool searching = (g_tab != 6) && g_search[0] != '\0';
        float searchRowH = (g_tab != 6) ? 32.0f : 0.0f;

        float debugH = g_debugOpen ? 92.0f : 0.0f;
        float footerH = 74.0f + debugH;
        float availH = ImGui::GetContentRegionAvail().y;
        float colsH = availH - footerH - searchRowH;
        if (colsH < 150.0f)
            colsH = 150.0f;

        static float indY = -1.0f;
        if (indY < 0.0f)
            indY = areaBase.y;
        float targetY = areaBase.y + g_tab * (tabH + tabGap);
        indY += (targetY - indY) * clampf(dt * 14.0f, 0.0f, 1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, winAlpha * (0.40f + 0.60f * tabEase));

        // ================= Search bar =================
        if (g_tab != 6)
        {
            ImGui::SetNextItemWidth(totalW * 0.45f);
            ImGui::InputTextWithHint("##search", "Search module...", g_search, sizeof(g_search));
            if (g_search[0] != '\0')
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("x##clearsearch"))
                    g_search[0] = '\0';
            }
        }

        ImGui::SetCursorPos(ImVec2(areaBase.x, areaBase.y + searchRowH));

        // ================= Sidebar (tabs, izquierda) =================
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 8.0f));
        if (ImGui::BeginChild("##side", ImVec2(sideW, colsH), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            const char* tabs[] = {"Combat", "Movement", "Visuals", "Misc", "Scripting", "Interface", "Configs"};
            ImGuiWindow* sideWnd = ImGui::GetCurrentWindow();
            float bbW = ImGui::GetContentRegionAvail().x;

            for (int i = 0; i < 7; ++i)
            {
                ImGui::SetCursorPos(ImVec2(0.0f, i * (tabH + tabGap)));
                if (sideWnd->SkipItems)
                    continue;

                ImVec2 bbMin = ImGui::GetCursorScreenPos();
                ImRect bb(bbMin, ImVec2(bbMin.x + bbW, bbMin.y + tabH));
                ImGui::ItemSize(ImVec2(bbW, tabH), 0.0f);

                bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max) &&
                               ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                if (pressed)
                {
                    g_tab = i;
                    g_search[0] = '\0';
                }

                bool act = g_tab == i && !searching;
                ImDrawList* d = sideWnd->DrawList;

                // Fade de hover estilo Fluent.
                static float tabHoverA[7] = {};
                tabHoverA[i] += (((hovered && !act) ? 1.0f : 0.0f) - tabHoverA[i]) *
                                clampf(dt * 12.0f, 0.0f, 1.0f);

                if (act)
                {
                    d->AddRectFilled(bb.Min, bb.Max, theme::AccentU32(0.14f), 6.0f);
                    d->AddRect(bb.Min, bb.Max, theme::AccentU32(0.28f), 6.0f, 0, 1.0f);
                }
                else if (tabHoverA[i] > 0.01f)
                {
                    d->AddRectFilled(bb.Min, bb.Max,
                                    IM_COL32(255, 255, 255, (int)(18.0f * tabHoverA[i])), 6.0f);
                    d->AddRect(bb.Min, bb.Max,
                               IM_COL32(255, 255, 255, (int)(26.0f * tabHoverA[i])), 6.0f, 0, 1.0f);
                }

                const char* text = tabs[i];
                ImVec2 ts = ImGui::CalcTextSize(text);
                ImVec2 textPos = ImVec2(bb.Min.x + 12.0f,
                                        bb.Min.y + (tabH - ts.y) * 0.5f);
                ImU32 textCol = act ? theme::AccentU32()
                                    : ImGui::GetColorU32(ImGuiCol_Text);
                d->AddText(textPos, textCol, text);

                if (i < 6)
                {
                    int n = (int)mgr.byCategory((Category)i).size();
                    if (n > 0)
                    {
                        char cnt[16];
                        snprintf(cnt, sizeof(cnt), "%d", n);
                        ImVec2 cs = ImGui::CalcTextSize(cnt);
                        d->AddText(ImVec2(bb.Max.x - cs.x - 10.0f,
                                          bb.Min.y + (tabH - cs.y) * 0.5f),
                                   act ? theme::AccentU32(0.75f) : IM_COL32(115, 122, 133, 220),
                                   cnt);
                    }
                }
            }

            ImVec2 sPos = ImGui::GetWindowPos();
            float localInd = indY - areaBase.y;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(sPos.x + 2.0f, sPos.y + localInd + 5.0f),
                ImVec2(sPos.x + 5.0f, sPos.y + localInd + tabH - 5.0f),
                theme::AccentU32(), 1.5f);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 6.0f);

        // ================= Contenido =================
        if (g_tab == 6)
        {
            if (ImGui::BeginChild("##configs", ImVec2(contentW, colsH), false))
            {
                static char newName[64] = "";
                static std::string selectedName;
                static std::vector<std::string> presets;
                static ULONGLONG lastRefresh = 0;
                static ULONGLONG lastToast = 0;
                static char toast[128] = "";

                auto refresh = [&]()
                {
                    presets = config::listPresets();
                    lastRefresh = GetTickCount64();
                    if (!selectedName.empty() &&
                        std::find(presets.begin(), presets.end(), selectedName) == presets.end())
                        selectedName.clear();
                };

                if (presets.empty() || GetTickCount64() - lastRefresh > 1000)
                    refresh();

                float cfgW = ImGui::GetContentRegionAvail().x;

                // ---- Crear ----
                ImGui::TextDisabled("New config from current state:");
                ImGui::SetNextItemWidth(cfgW - 130.0f);
                ImGui::InputTextWithHint("##cfgname", "Name...", newName, sizeof(newName));
                ImGui::SameLine();
                if (ImGui::Button("Create##cfg_create") && newName[0] != '\0')
                {
                    if (config::savePreset(newName))
                    {
                        snprintf(toast, sizeof(toast), "Config created: %s", newName);
                        lastToast = GetTickCount64();
                        selectedName = newName;
                        newName[0] = '\0';
                        refresh();
                    }
                }

                ImGui::Separator();

                // ---- Listado ----
                float listH = ImGui::GetContentRegionAvail().y - 62.0f;
                if (ImGui::BeginChild("##cfglist", ImVec2(cfgW, listH), true))
                {
                    if (presets.empty())
                        ImGui::TextDisabled("No configs saved yet");

                    int idx = 0;
                    for (const std::string& p : presets)
                    {
                        ImGui::PushID(idx++);
                        bool isSel = (p == selectedName);
                        if (isSel)
                            ImGui::PushStyleColor(ImGuiCol_Text, theme::Accent);
                        if (ImGui::Selectable(p.c_str(), isSel))
                            selectedName = p;
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            config::loadPreset(p);
                            snprintf(toast, sizeof(toast), "Config loaded: %s", p.c_str());
                            lastToast = GetTickCount64();
                        }
                        if (isSel)
                            ImGui::PopStyleColor();
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();

                // ---- Acciones sobre la seleccion ----
                bool hasSel = !selectedName.empty();
                if (!hasSel)
                    ImGui::BeginDisabled();

                if (ImGui::Button("Load##cfg_load"))
                {
                    if (config::loadPreset(selectedName))
                    {
                        snprintf(toast, sizeof(toast), "Config loaded: %s", selectedName.c_str());
                        lastToast = GetTickCount64();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Save##cfg_save"))
                {
                    if (config::savePreset(selectedName))
                    {
                        snprintf(toast, sizeof(toast), "Config saved: %s", selectedName.c_str());
                        lastToast = GetTickCount64();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete##cfg_del"))
                {
                    if (config::deletePreset(selectedName))
                    {
                        snprintf(toast, sizeof(toast), "Config deleted: %s", selectedName.c_str());
                        lastToast = GetTickCount64();
                        selectedName.clear();
                        refresh();
                    }
                }

                if (!hasSel)
                    ImGui::EndDisabled();

                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 210.0f);
                if (toast[0] && GetTickCount64() - lastToast < 2500)
                    ImGui::TextDisabled("%s", toast);
                else if (!hasSel)
                    ImGui::TextDisabled("Select a config");
                else
                    ImGui::TextDisabled("Selected: %s", selectedName.c_str());

                // ---- Scripts ----
                ImGui::Separator();
                if (ImGui::Button("Reload scripts##cfg_scr"))
                {
                    scripts::reload();
                    snprintf(toast, sizeof(toast), "Scripts reloaded (%d)", scripts::count());
                    lastToast = GetTickCount64();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Scripts loaded: %d", scripts::count());
                if (scripts::errorCount() > 0)
                    ImGui::TextDisabled("Script error: %s", scripts::lastError());
            }
            ImGui::EndChild();
        }
        else
        {
            if (ImGui::BeginChild("##modlist", ImVec2(contentW, colsH), false))
            {
                float wheelY = ImGui::GetIO().MouseWheel;
                ImVec2 mousePos = ImGui::GetIO().MousePos;
                bool mouseInMenu = mousePos.x >= g_menuMin.x && mousePos.x <= g_menuMax.x &&
                                   mousePos.y >= g_menuMin.y && mousePos.y <= g_menuMax.y;
                if (wheelY != 0.0f && mouseInMenu && ImGui::GetScrollMaxY() > 0.0f &&
                    GImGui->WheelingWindowScrolledFrame != ImGui::GetFrameCount())
                {
                    float step = ImTrunc(ImMin(5.0f * ImGui::GetFontSize(),
                                               ImGui::GetWindowHeight() * 0.67f));
                    ImGui::SetScrollY(ImGui::GetScrollY() - wheelY * step);
                }

                ImDrawList* dl = ImGui::GetWindowDrawList();
                Module* binding = mgr.binding();
                std::vector<Module*> modules;
                if (searching)
                {
                    for (auto& up : mgr.all())
                    {
                        Module* m = up.get();
                        if (m && containsCI(m->name(), g_search))
                            modules.push_back(m);
                    }
                }
                else
                {
                    modules = mgr.byCategory((Category)g_tab);
                }

                if (searching)
                {
                    char lbl[48];
                    snprintf(lbl, sizeof(lbl), "Results (%d)", (int)modules.size());
                    ImGui::TextDisabled("%s", lbl);
                }

                const float rowH = 34.0f;
                const float chipZone = 168.0f;

                for (Module* m : modules)
                {
                    if (!m)
                        continue;

                    float avail = ImGui::GetContentRegionAvail().x;
                    ImVec2 rowMin = ImGui::GetCursorPos();
                    // Posicion en pantalla YA con el scroll del child aplicado:
                    // GetCursorPos() es relativo al contenido y no incluye el desplazamiento.
                    ImVec2 sMin = ImGui::GetCursorScreenPos();
                    ImVec2 sMax(sMin.x + avail, sMin.y + rowH);
                    ImGui::PushID(m);

                    // Fila clicable: activar/desactivar
                    ImGui::SetCursorPos(rowMin);
                    if (ImGui::InvisibleButton("##row", ImVec2(avail - chipZone, rowH)))
                    {
                        bool turningOn = !m->enabled();
                        m->setEnabled(turningOn);
                        if (turningOn && m->hasSettings())
                            g_expanded[m] = true;
                    }
                    bool rowHov = ImGui::IsItemHovered();
                    if (rowHov && !m->description().empty())
                        ImGui::SetTooltip("%s", m->description().c_str());

                    // Hover animado
                    float ha = g_rowHoverAnim[m];
                    ha += ((rowHov ? 1.0f : 0.0f) - ha) * clampf(dt * 14.0f, 0.0f, 1.0f);
                    g_rowHoverAnim[m] = ha;
                    if (ha > 0.01f)
                    {
                        dl->AddRectFilled(sMin, sMax,
                                          IM_COL32(255, 255, 255, (int)(12.0f * ha)), 6.0f);
                        dl->AddRect(sMin, sMax,
                                    IM_COL32(255, 255, 255, (int)(26.0f * ha)), 6.0f, 0, 1.0f);
                    }

                    bool on = m->enabled();

                    // Fila activa: fill acrilico tenue + barra de acento (Fluent).
                    if (on)
                    {
                        dl->AddRectFilled(sMin, sMax, theme::AccentU32(0.07f), 6.0f);
                        dl->AddRectFilled(sMin, ImVec2(sMin.x + 3.0f, sMax.y),
                                          theme::AccentU32(0.85f), 1.5f);
                    }

                    // Toggle estilo WinUI dark: track azul claro con perilla negra.
                    float& sw = g_switchAnim[m];
                    sw += ((on ? 1.0f : 0.0f) - sw) * clampf(dt * 14.0f, 0.0f, 1.0f);
                    float swW = 34.0f, swH = 18.0f;
                    ImVec2 swMin(sMin.x + 9.0f, sMin.y + (rowH - swH) * 0.5f);
                    ImVec2 swMax(swMin.x + swW, swMin.y + swH);
                    dl->AddRectFilled(swMin, swMax,
                                      on ? theme::AccentU32(0.95f) : IM_COL32(255, 255, 255, 20),
                                      swH * 0.5f);
                    if (!on)
                        dl->AddRect(swMin, swMax, IM_COL32(255, 255, 255, 26), swH * 0.5f, 0, 1.0f);
                    float kx = swMin.x + 7.0f + sw * (swW - 14.0f);
                    dl->AddCircleFilled(ImVec2(kx, (swMin.y + swMax.y) * 0.5f), 5.5f,
                                        on ? IM_COL32(17, 18, 21, 255) : IM_COL32(235, 238, 242, 255), 12);

                    // Nombre
                    const char* nm = m->name().c_str();
                    ImVec2 nts = ImGui::CalcTextSize(nm);
                    float nameX = sMin.x + 9.0f + swW + 10.0f;
                    dl->AddText(ImVec2(nameX, sMin.y + (rowH - nts.y) * 0.5f),
                                on ? theme::AccentU32() : ImGui::GetColorU32(ImGuiCol_Text), nm);

                    if (searching)
                    {
                        const char* cat = categoryName(m->category());
                        ImVec2 cts = ImGui::CalcTextSize(cat);
                        dl->AddText(ImVec2(nameX + nts.x + 8.0f, sMin.y + (rowH - cts.y) * 0.5f),
                                    IM_COL32(140, 148, 160, 220), cat);
                    }

                    // Chip de keybind
                    char label[64];
                    if (binding == m)
                        snprintf(label, sizeof(label), "[...]##bind");
                    else
                        snprintf(label, sizeof(label), "[%s]##bind", keyName(m->keybind()));
                    ImGui::SetCursorPos(ImVec2(avail - chipZone + 4.0f, rowMin.y + 6.0f));
                    if (ImGui::SmallButton(label))
                    {
                        if (mgr.binding() == m)
                            mgr.setBinding(nullptr);
                        else
                            mgr.setBinding(m);
                    }

                    // Flecha de settings
                    if (m->hasSettings())
                    {
                        bool exp = g_expanded[m];
                        float& ea = g_expandAnim[m];
                        ea += ((exp ? 1.0f : 0.0f) - ea) * clampf(dt * 12.0f, 0.0f, 1.0f);

                        ImGui::SetCursorPos(ImVec2(avail - 36.0f, rowMin.y + 7.0f));
                        if (ImGui::InvisibleButton("##exp", ImVec2(20.0f, 20.0f)))
                            g_expanded[m] = !exp;

                        ImVec2 bMin = ImGui::GetItemRectMin();
                        ImVec2 ctr(bMin.x + 10.0f, bMin.y + 10.0f);
                        float ang = ea * 1.5707963f;
                        float ca = cosf(ang), sa = sinf(ang);
                        auto rot = [&](float x, float y) {
                            return ImVec2(ctr.x + x * ca - y * sa, ctr.y + x * sa + y * ca);
                        };
                        ImU32 arCol = (ea > 0.01f)
                                          ? theme::AccentU32()
                                          : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                        dl->AddTriangleFilled(rot(-3.0f, -4.5f), rot(-3.0f, 4.5f),
                                              rot(4.5f, 0.0f), arCol);
                    }

                    // Panel de settings
                    if (m->hasSettings() && g_expanded[m])
                    {
                        ImGui::SetCursorPos(ImVec2(0.0f, rowMin.y + rowH + 2.0f));
                        ImVec2 setTopScreen = ImGui::GetCursorScreenPos();
                        ImGui::Indent(52.0f);
                        m->drawSettings();
                        ImGui::Unindent(52.0f);
                        float setBotY = ImGui::GetCursorPos().y;
                        ImVec2 setBotScreen = ImGui::GetCursorScreenPos();

                        dl->AddRectFilled(ImVec2(setTopScreen.x + 40.0f, setTopScreen.y + 2.0f),
                                          ImVec2(setTopScreen.x + 42.5f, setBotScreen.y - 2.0f),
                                          theme::AccentU32(0.30f), 1.0f);

                        ImGui::SetCursorPos(ImVec2(0.0f, setBotY + 8.0f));
                    }
                    else
                    {
                        ImGui::SetCursorPos(ImVec2(0.0f, rowMin.y + rowH + 4.0f));
                    }

                    ImGui::PopID();
                }

                // ImGui 1.92: neutraliza el SetCursorPos final de la ultima fila
                // (exigir un item despues evita el assert de boundaries en End()).
                ImGui::Dummy(ImVec2(0.0f, 0.0f));

                if (modules.empty() && !searching)
                    ImGui::TextDisabled("No modules in this category");
            }
            ImGui::EndChild();
        }

        ImGui::PopStyleVar();

        // Divisor vertical entre sidebar y contenido
        ImVec2 dividerTop(winPos.x + areaBase.x + sideW + 3.0f,
                           winPos.y + areaBase.y + searchRowH - 2.0f);
        ImVec2 dividerBot(winPos.x + areaBase.x + sideW + 3.0f,
                          winPos.y + areaBase.y + searchRowH + colsH + 4.0f);
        ImGui::GetWindowDrawList()->AddLine(dividerTop, dividerBot, theme::AccentU32(0.30f), 1.0f);

        // ================= Footer =================
        ImGui::SetCursorPos(ImVec2(areaBase.x, areaBase.y + searchRowH + colsH + 10.0f));
        ImGui::Separator();
        {
            Game& g = Game::get();
            bool world = g.localFound();
            bool gmOk = false;
            bool inputOk = false;
            if (world)
            {
                gmOk = g.gameMode().valid();
                inputOk = g.moveInput().valid();
            }

            ImGui::TextDisabled("LP: %s   Remotes: %d/%d   GameMode: %s   Input: %s",
                                world ? "OK" : "--",
                                (int)g.shownRemotesCount(), (int)g.rawRemotesCount(),
                                gmOk ? "OK" : "FAIL", inputOk ? "OK" : "FAIL");

            ImGui::SameLine(winW - 76.0f);
            if (ImGui::SmallButton(g_debugOpen ? "Debug -##foot" : "Debug +##foot"))
                g_debugOpen = !g_debugOpen;

            if (g_debugOpen)
            {
                if (world)
                {
                    ItemInstance held = g.localPlayer().heldItem();
                    ImGui::TextDisabled("Held: %s  count=%d  block=%s",
                                        held.valid() ? "yes" : "NO",
                                        held.valid() ? held.count() : 0,
                                        held.isBlockItem() ? "yes" : "no");

                    LocalPlayer lp = g.localPlayer();
                    ImGui::TextDisabled("Ground: %d  LMB: %d  vy: %.2f  airH: %.2f",
                                        lp.onGround() ? 1 : 0,
                                        lp.leftClickFlag() ? 1 : 0,
                                        lp.velocity().y,
                                        lp.height());
                }

                ImGui::TextDisabled("Place: %llu tries  last=%s  @ %d,%d,%d   Wheel: %.2f",
                                    placestats::attempts,
                                    placestats::lastOk ? "OK" : "fail",
                                    placestats::lastX, placestats::lastY, placestats::lastZ,
                                    wheel::lastValue());
            }
        }
        ImGui::Separator();

        if (mgr.binding())
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f),
                               "Binding: press a key | ESC clears | click [...] to cancel");
        else
            ImGui::TextDisabled("INSERT: show/hide   ·   click row: toggle   ·   [key]: bind   ·   arrow: settings");
    }

    ImGui::End();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor();

    if (!open)
    {
        g_visible = false;
        config::saveAll();
    }
}

void renderHudChrome()
{
    drawHudEditorButton();
    renderHudEditor();
}

namespace {

// Glow coloreado: mismo falloff cubico por capas que widgets::SoftShadow,
// pero con el color pasado (el acento de la notificacion).
void toastGlow(ImDrawList* d, const ImVec2& min, const ImVec2& max,
               ImU32 col, float strength, float spread, float rounding)
{
    const int layers = 9;
    for (int i = 0; i < layers; ++i)
    {
        float t = (float)i / (float)(layers - 1);
        float pad = spread * t;
        float f = 1.0f - t;
        f = f * f * f;
        int a = (int)(strength * f);
        if (a <= 0)
            continue;
        d->AddRectFilled(ImVec2(min.x - pad, min.y - pad),
                         ImVec2(max.x + pad, max.y + pad),
                         (col & 0x00FFFFFFu) | ((unsigned)a << 24), rounding + pad);
    }
}

}

// Estilo Solstice: la notificacion entra en diagonal desde la esquina
// inferior derecha. La caja se divide en dos sectores: uno de color (accent)
// que crece de izquierda a derecha con el progreso del tiempo y otro negro
// con el tiempo restante, con un glow suave alrededor del sector de color.
void renderToasts()
{
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    ModuleManager& mgr = ModuleManager::get();
    mgr.tickNotifications(io.DeltaTime);
    const auto& notifs = mgr.notifications();

    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = 20.0f;
    float y = sh - 10.0f;

    // Orden natural: la mas vieja queda en el slot inferior, la mas nueva
    // entra por encima. El slot "sube" a medida que la notificacion se asienta,
    // y las demas bajan en cascada cuando una sale.
    for (const auto& n : notifs)
    {
        char buf[128];
        if (n.hasState)
            snprintf(buf, sizeof(buf), "%s  [%s]", n.text.c_str(), n.enabled ? "ON" : "OFF");
        else
            snprintf(buf, sizeof(buf), "%s", n.text.c_str());

        ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, buf);
        float w = ts.x + 50.0f;
        if (w < 200.0f)
            w = 200.0f;
        // La altura incluye 10px de separacion con la notificacion de arriba.
        float h = ts.y + 30.0f;

        float beginX = sw - w - 10.0f;
        float endX = sw + w;
        float x = endX + (beginX - endX) * n.slide;
        y -= h * n.slide;
        if (x > sw + w && y > sh + h)
            continue;

        float percentDone = n.timeShown / n.duration;
        if (percentDone < 0.0f)
            percentDone = 0.0f;
        if (percentDone > 1.0f)
            percentDone = 1.0f;

        // Accent para ON y avisos sin estado, gris para OFF.
        ImVec4 themeCol = theme::Accent;
        if (n.hasState && !n.enabled)
            themeCol = ImVec4(150.0f / 255.0f, 155.0f / 255.0f, 160.0f / 255.0f, 1.0f);
        ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(
            ImVec4(themeCol.x, themeCol.y, themeCol.z, 1.0f));

        float max = x + w;
        ImVec2 progMax(x + w * percentDone + 6.0f, y + h - 10.0f);
        if (progMax.x > max)
            progMax.x = max;
        if (progMax.x < x)
            progMax.x = x;
        ImVec2 bgMin(x + w * percentDone, y);
        ImVec2 bgMax(x + w, y + h - 10.0f);

        // Glow alrededor del sector de color, bajo los rellenos.
        toastGlow(d, ImVec2(x, y), progMax, accentCol, 50.0f, 22.0f, 5.0f);

        // Sector de color: crece con el progreso (clip evita redondear el borde
        // derecho mientras avanza).
        d->PushClipRect(ImVec2(x, y), ImVec2(x + w * percentDone, y + h - 10.0f), true);
        d->AddRectFilled(ImVec2(x, y), progMax,
                         ImGui::ColorConvertFloat4ToU32(
                             ImVec4(themeCol.x, themeCol.y, themeCol.z, 0.7f)),
                         5.0f);
        d->PopClipRect();

        // Sector negro: tiempo restante (solapa 6px sobre el color para
        // transicion suave).
        d->PushClipRect(bgMin, bgMax, true);
        d->AddRectFilled(ImVec2(x + w * percentDone - 6.0f, y), bgMax,
                         IM_COL32(0, 0, 0, 179), 5.0f);
        d->PopClipRect();

        d->AddText(font, fontSize, ImVec2(x + 10.0f, y + 10.0f),
                   IM_COL32(255, 255, 255, 255), buf);
    }
}

}
