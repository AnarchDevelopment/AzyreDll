#include "sRise.hpp"
#include "RiseWidgets.hpp"
#include "UIColors.hpp"

#include "Config/Config.hpp"
#include "Framework/ColorUtils.hpp"
#include "Framework/ImRenderUtils.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Input/KeyNames.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Interface/Interface.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/DX11.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace mc::gui::sRise {

namespace {

// Ease out exponencial oficial de Rise: Animation(Easing.EASE_OUT_EXPO)
float easeOutExpo(float t)
{
    return (t >= 1.0f) ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
}

// Suavizado exponencial independiente del framerate. Aproxima el tween de
// Rise Animation(EASE_OUT_QUINT, 500ms) sin gestionar timers por animacion.
float approach(float cur, float target, float rate, float dt)
{
    return cur + (target - cur) * clampf(dt * rate, 0.0f, 1.0f);
}

bool containsCI(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    size_t nl = needle.size();
    if (haystack.size() < nl)
        return false;
    for (size_t i = 0; i + nl <= haystack.size(); ++i)
    {
        size_t j = 0;
        while (j < nl && tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j]))
            ++j;
        if (j == nl)
            return true;
    }
    return false;
}

// Estructura de categorías de Rise
struct RiseCategoryDef
{
    const char* id;
    const char* name;
    const char* icon;
    int catEnum; // -1 = Search, >= 0 mapea a mc::Category
};

static const RiseCategoryDef kRiseCats[] = {
    {"search",    "Search",    "?", -1},
    {"combat",    "Combat",    "C", (int)Category::Combat},
    {"movement",  "Movement",  "M", (int)Category::Movement},
    {"visuals",   "Visuals",   "V", (int)Category::Visuals},
    {"misc",      "Player",    "P", (int)Category::Misc},
    {"scripting", "Scripting", "S", (int)Category::Scripting},
    {"interface", "Interface", "I", (int)Category::Interface},
    {"themes",    "Themes",    "T", -2}, // -2 = ThemeScreen
};
constexpr int kCatCount = (int)(sizeof(kRiseCats) / sizeof(kRiseCats[0]));

// KeyColors.java — las 10 muestras de color que filtran los temas en Rise.
enum : unsigned
{
    KEY_RED       = 1u << 0,
    KEY_ORANGE    = 1u << 1,
    KEY_YELLOW    = 1u << 2,
    KEY_LIME      = 1u << 3,
    KEY_DARKGREEN = 1u << 4,
    KEY_AQUA      = 1u << 5,
    KEY_DARKBLUE  = 1u << 6,
    KEY_PURPLE    = 1u << 7,
    KEY_PINK      = 1u << 8,
    KEY_GRAY      = 1u << 9,
};
static const unsigned char kKeyColors[10][3] = {
    {255, 50, 50},   {255, 128, 50}, {255, 255, 50},  {128, 255, 50}, {50, 128, 50},
    {50, 200, 255},  {50, 100, 200}, {128, 50, 255},  {255, 128, 255}, {100, 100, 110},
};

// Themes.java de Rise 6.9.5: gradiente rA (arriba) -> rB (abajo); rC es el
// tercer stop de los temas tri-color. keyMask indica los KeyColors del tema.
// fn = tema funcion (TEST "Rainbow"): los colores se calculan en runtime con
// ColorUtil.aB via theme::rainbowAt.
struct RiseTheme
{
    const char* name;
    unsigned char a[3];
    unsigned char b[3];
    unsigned char c[3];
    bool tri;
    unsigned keyMask;
    bool fn = false;
};

static const RiseTheme kRiseThemes[] = {
    {"Aubergine",       {170, 7, 107},   {97, 4, 95},      {0, 0, 0},       false, KEY_PURPLE | KEY_RED},
    {"Aqua",            {185, 250, 255}, {79, 199, 200},   {0, 0, 0},       false, KEY_AQUA},
    {"Banana",          {253, 236, 177}, {255, 255, 255},  {0, 0, 0},       false, KEY_YELLOW},
    {"Blend",           {71, 148, 253},  {71, 253, 160},   {0, 0, 0},       false, KEY_AQUA | KEY_LIME},
    {"Blossom",         {226, 208, 249}, {49, 119, 115},   {0, 0, 0},       false, KEY_PINK | KEY_GRAY},
    {"Bubblegum",       {243, 145, 216}, {152, 165, 243},  {0, 0, 0},       false, KEY_PINK | KEY_PURPLE},
    {"Candy Cane",      {255, 0, 0},     {255, 255, 255},  {0, 0, 0},       false, KEY_RED},
    {"Cherry",          {187, 55, 125},  {251, 211, 233},  {0, 0, 0},       false, KEY_RED | KEY_PURPLE | KEY_PINK},
    {"Christmas",       {255, 64, 64},   {255, 255, 255},  {64, 255, 64},   true,  KEY_RED | KEY_LIME},
    {"Coral",           {244, 168, 150}, {52, 133, 151},   {0, 0, 0},       false, KEY_PINK | KEY_ORANGE | KEY_DARKBLUE},
    {"Digital Horizon", {95, 195, 228},  {229, 93, 135},   {0, 0, 0},       false, KEY_AQUA | KEY_RED | KEY_PINK},
    {"Express",         {173, 83, 137},  {60, 16, 83},     {0, 0, 0},       false, KEY_PURPLE | KEY_PINK},
    {"Lime Water",      {18, 255, 247},  {179, 255, 171},  {0, 0, 0},       false, KEY_AQUA | KEY_LIME},
    {"Lush",            {168, 224, 99},  {86, 171, 47},    {0, 0, 0},       false, KEY_LIME | KEY_DARKGREEN},
    {"Halogen",         {255, 65, 108},  {255, 75, 43},    {0, 0, 0},       false, KEY_RED | KEY_ORANGE},
    {"Hyper",           {236, 110, 173}, {52, 148, 230},   {0, 0, 0},       false, KEY_PINK | KEY_DARKBLUE | KEY_AQUA},
    {"Magic",           {74, 0, 224},    {142, 45, 226},   {0, 0, 0},       false, KEY_DARKBLUE | KEY_PURPLE},
    {"May",             {238, 79, 238},  {253, 219, 245},  {0, 0, 0},       false, KEY_PINK | KEY_PURPLE},
    {"Orange Juice",    {252, 74, 26},   {247, 183, 51},   {0, 0, 0},       false, KEY_ORANGE | KEY_YELLOW},
    {"Pastel",          {243, 155, 178}, {207, 196, 243},  {0, 0, 0},       false, KEY_PINK},
    {"Pumpkin",         {241, 166, 98},  {255, 216, 169},  {227, 139, 42},  true,  KEY_ORANGE},
    {"Satin",           {215, 60, 67},   {140, 23, 39},    {0, 0, 0},       false, KEY_RED},
    {"Snowy Sky",       {1, 171, 179},   {234, 234, 234},  {18, 232, 232},  true,  KEY_AQUA | KEY_GRAY},
    {"Steel Fade",      {66, 134, 244},  {55, 59, 68},     {0, 0, 0},       false, KEY_DARKBLUE | KEY_GRAY},
    {"Sundae",          {206, 74, 126},  {122, 44, 77},    {0, 0, 0},       false, KEY_PINK | KEY_PURPLE | KEY_RED},
    {"Sunkist",         {242, 201, 76},  {242, 153, 74},   {0, 0, 0},       false, KEY_YELLOW | KEY_ORANGE},
    {"Water",           {12, 232, 199},  {12, 163, 232},   {0, 0, 0},       false, KEY_AQUA | KEY_DARKBLUE},
    {"Legacy",          {112, 206, 255}, {112, 206, 255},  {0, 0, 0},       false, KEY_AQUA | KEY_DARKBLUE},
    {"Winter",          {255, 255, 255}, {255, 255, 255},  {0, 0, 0},       false, KEY_GRAY},
    {"Peony",           {226, 208, 249}, {207, 171, 255},  {0, 0, 0},       false, KEY_PINK | KEY_GRAY},
    {"Shadow",          {97, 131, 255},  {206, 212, 255},  {0, 0, 0},       false, KEY_AQUA},
    {"Wood",            {79, 109, 81},   {170, 139, 87},   {240, 235, 206}, true,  KEY_DARKGREEN},
    {"Gothic",          {31, 30, 30},    {196, 190, 190},  {0, 0, 0},       false, KEY_GRAY},
    {"Rue",             {234, 118, 176}, {31, 30, 30},     {0, 0, 0},       false, KEY_PINK},
    // Themes.TEST: tema funcion "Rainbow" (ColorUtil.aB). triColor=true en el
    // constructor de funcion; sin KeyColors (solo aparece sin filtro).
    {"Rainbow",         {0, 0, 0},       {0, 0, 0},        {0, 0, 0},       true,  0, true},
    {"Nord",            {143, 188, 187}, {163, 190, 140},  {236, 239, 244}, true,  KEY_AQUA | KEY_GRAY},
};
constexpr int kThemeCount = (int)(sizeof(kRiseThemes) / sizeof(kRiseThemes[0]));

// Gradiente redondeado con eje arbitrario (g0 -> g1).
void roundedGradient(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImVec2 g0, ImVec2 g1,
                     ImU32 c0, ImU32 c1, float radius, ImDrawFlags flags)
{
    int start = dl->VtxBuffer.Size;
    dl->AddRectFilled(p0, p1, c0, radius, flags);
    int end = dl->VtxBuffer.Size;
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, start, end, g0, g1, c0, c1);
}

// Color del shader de gradiente de Rise (rgqtest.glsl / trirgq.glsl):
//   mix(c1, c2, 0.5 + 0.5*sin(3.0*(coord + tiempo)))
// y para tri-color una mezcla exterior con smoothstep sobre el mismo seno.
// `coord` es la coordenada normalizada 0..1 a lo largo del eje del gradiente.
ImU32 themeShaderColor(const RiseTheme& t, float coord, float alpha)
{
    float time = (float)ImGui::GetTime();
    float s = sinf(3.0f * (coord + time));
    float m = 0.5f + 0.5f * s;

    auto mix = [](float a, float b, float k) { return a + (b - a) * k; };

    float r, g, b;
    if (t.tri)
    {
        // blendFactor = smoothstep(0,1,sin(...))
        float c = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
        float bf = c * c * (3.0f - 2.0f * c);

        float c12r = mix((float)t.a[0], (float)t.b[0], m);
        float c12g = mix((float)t.a[1], (float)t.b[1], m);
        float c12b = mix((float)t.a[2], (float)t.b[2], m);
        float c23r = mix((float)t.b[0], (float)t.c[0], m);
        float c23g = mix((float)t.b[1], (float)t.c[1], m);
        float c23b = mix((float)t.b[2], (float)t.c[2], m);

        r = mix(c12r, c23r, bf);
        g = mix(c12g, c23g, bf);
        b = mix(c12b, c23b, bf);
    }
    else
    {
        r = mix((float)t.a[0], (float)t.b[0], m);
        g = mix((float)t.a[1], (float)t.b[1], m);
        b = mix((float)t.a[2], (float)t.b[2], m);
    }

    int a = (int)(alpha * 255.0f + 0.5f);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    return IM_COL32((int)(r + 0.5f), (int)(g + 0.5f), (int)(b + 0.5f), a);
}

// Estado persistente de la ventana de Rise
static ImVec2 g_winPos = ImVec2(-1.0f, -1.0f);
static bool g_dragging = false;
static ImVec2 g_dragOffset = ImVec2(0.0f, 0.0f);

static int g_selectedCat = 1; // Default: Combat
static float g_catIndicatorY = 0.0f;

static char g_searchQuery[64] = "";
static float g_scroll = 0.0f;
static float g_scrollTarget = 0.0f;

// ThemeScreen: filtro de KeyColors (-1 = todos) y scroll propio del grid.
static int g_themeFilter = -1;
static float g_themeScroll = 0.0f;
static float g_themeScrollTarget = 0.0f;

// Animaciones por tarjeta de tema (ThemeComponent.java: xAnimation,
// yAnimation, opacityAnimation, selectorAnimation — EASE_OUT_QUINT 500ms).
struct ThemeAnim
{
    float x = 0.0f;
    float y = 0.0f;
    float opacity = 0.0f;
    float selector = 0.0f;
    bool inited = false;
};
static ThemeAnim g_themeAnim[kThemeCount];

// Animaciones de los filtros KeyColors (dimAnimation + bloomAnimation).
static float g_keyDim[10] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                             1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
static float g_keyBloom[10] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
static bool g_themeScreenWasActive = false;

// Escala efectiva del chrome (util para que RiseWidgets escale su geometria).
static float g_uiScale = 2.0f;

static std::map<const Module*, bool> g_expanded;
static std::map<const Module*, float> g_expandAnim;
static std::map<const Module*, float> g_settingsH;
static std::map<const Module*, float> g_enableAnim;
static std::map<const Module*, float> g_hoverAnim;

// Ventana contenedora transparente para los widgets interactivos del chrome
// de Rise (los InputText de busqueda). Sin esto, ImGui envia el InputText a su
// ventana implicita "Debug##Default": aparece una ventana "Debug" y el
// buscador no funciona (queda en esa ventana suelta).
void riseSearchHost(const char* windowId, const char* widgetId, const char* hint,
                    ImVec2 pos, ImVec2 size, char* buf, int bufSize,
                    float padX, float padY, float textAlpha)
{
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin(windowId, nullptr, flags))
    {
        ImGui::SetCursorPos(ImVec2(padX, padY));
        ImGui::PushItemWidth(size.x - padX * 2.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, textAlpha));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        ImGui::InputTextWithHint(widgetId, hint, buf, bufSize);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::PopItemWidth();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace

void render(float anim)
{
    (void)anim; // Rise usa sus propios Animation timers (RiseClickGUI.java)
    float dt = ImGui::GetIO().DeltaTime;
    ImGuiIO& io = ImGui::GetIO();
    // Background draw list: se dibuja ENCIMA del juego pero DEBAJO de las
    // ventanas ImGui. Asi las settings (ventanas ImGui) se ven sobre el
    // chrome de Rise sin ser tapadas.
    ImDrawList* fg = ImGui::GetBackgroundDrawList();
    // Foreground solo para overlays (bind notice al final).
    ImDrawList* fgTop = ImGui::GetForegroundDrawList();
    // Rise usa Product Sans (font oficial). Fallback al font del tema.
    ImFont* font = dx11::riseFont();
    if (!font)
        font = ImGui::GetFont();
    ModuleManager& mgr = ModuleManager::get();

    // ========================================================================
    // Animacion de apertura/cierre de RiseClickGUI.java:
    //   ABRIR  -> scaleAnimation  0 -> 1 : EASE_OUT_EXPO, 300ms
    //             opacityAnimation 0 -> 1 : EASE_OUT_EXPO, 300ms
    //   CERRAR -> opacityAnimation 1 -> 0 : LINEAR, 100ms
    //             scaleAnimation  1 -> 0 : LINEAR, 300ms
    // Al abrir, initGui resetea la escala a 0 (zoom desde el centro); al
    // cerrar los tweens continuan desde el valor actual (Animation.Q). La
    // escala esta anclada al centro de la ventana:
    //   translate = center * (1 - s); screen = translate + p * s
    // ========================================================================
    bool visibleNow = menu::visible();
    static bool s_init = false;
    static bool s_opening = true;
    static double s_t0 = 0.0;
    static float s_scaleFrom = 0.0f;
    static float s_opacFrom = 0.0f;
    static float s_scale = 0.0f;
    static float s_opac = 0.0f;

    if (!s_init)
    {
        s_init = true;
        s_opening = visibleNow;
        s_scale = visibleNow ? 0.0f : 1.0f;
        s_opac = visibleNow ? 0.0f : 1.0f;
        s_scaleFrom = s_scale;
        s_opacFrom = s_opac;
        s_t0 = ImGui::GetTime();
    }
    else if (visibleNow != s_opening)
    {
        s_opening = visibleNow;
        s_opacFrom = s_opac;                          // Q(): desde el valor actual
        s_scaleFrom = visibleNow ? 0.0f : s_scale;    // initGui: reset a 0 al abrir
        s_t0 = ImGui::GetTime();
    }

    double tMs = (ImGui::GetTime() - s_t0) * 1000.0;
    if (s_opening)
    {
        float p = clampf((float)(tMs / 300.0), 0.0f, 1.0f);
        float e = easeOutExpo(p);
        s_scale = s_scaleFrom + (1.0f - s_scaleFrom) * e;
        s_opac = s_opacFrom + (1.0f - s_opacFrom) * e;
    }
    else
    {
        float pO = clampf((float)(tMs / 100.0), 0.0f, 1.0f);
        float pS = clampf((float)(tMs / 300.0), 0.0f, 1.0f);
        s_opac = s_opacFrom * (1.0f - pO);   // LINEAR, 100ms
        s_scale = s_scaleFrom * (1.0f - pS); // LINEAR, 300ms
    }

    // Rise: con axS == 0 el ClickGUI se desactiva (no se dibuja nada).
    float fade = s_opac;
    if (fade <= 0.001f || s_scale <= 0.001f)
        return;

    // Escala completa 0 -> 1 anclada al centro (GlStateManager.scale de Rise)
    float scaleAnim = s_scale;

    ImVec2 disp = io.DisplaySize;
    if (disp.x <= 0.0f || disp.y <= 0.0f)
        return;

    // Dim backdrop oscuro + blur de fondo
    fg->AddRectFilled(ImVec2(0.0f, 0.0f), disp, IM_COL32(0, 0, 0, (int)(110.0f * fade)));
    if (blur::wanted() && blur::ready() && blur::srv())
    {
        fg->AddImage(ImTextureRef((void*)blur::srv()), ImVec2(0.0f, 0.0f), disp,
                     ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                     IM_COL32(255, 255, 255, (int)(200.0f * fade)));
    }

    // ========================================================================
    // Dimensiones del ClickGUI de Rise. Base original 416 x 338 con sidebar
    // 100, escalada x2 (832 x 676) para que se vea comoda en resoluciones
    // modernas. El slider de UI Scale multiplica sobre esta base.
    const float kBaseScale = 2.0f;
    float userScale = gui::riseScale() * kBaseScale;
    if (userScale < 1.0f) userScale = 1.0f;
    if (userScale > 3.0f) userScale = 3.0f;
    g_uiScale = userScale;

    const float winW = ImMin(416.0f * userScale, disp.x - 16.0f);
    const float winH = ImMin(338.0f * userScale, disp.y - 16.0f);
    const float sideW = 100.0f * userScale;
    const float round = 12.0f * userScale;

    // Centrar la ventana la primera vez
    if (g_winPos.x < 0.0f || g_winPos.y < 0.0f ||
        g_winPos.x > disp.x || g_winPos.y > disp.y)
    {
        g_winPos = ImVec2((disp.x - winW) * 0.5f, (disp.y - winH) * 0.5f);
    }

    ImVec2 mouse = io.MousePos;
    ImVec2 center(g_winPos.x + winW * 0.5f, g_winPos.y + winH * 0.5f);

    ImVec2 winMin = g_winPos;
    ImVec2 winMax(winMin.x + winW, winMin.y + winH);

    // Transformación centrada de apertura/cierre
    ImVec2 scaledMin(center.x + (winMin.x - center.x) * scaleAnim,
                     center.y + (winMin.y - center.y) * scaleAnim);
    ImVec2 scaledMax(center.x + (winMax.x - center.x) * scaleAnim,
                     center.y + (winMax.y - center.y) * scaleAnim);

    // Arrastre por la cabecera superior (Rise drag area: altura 15-25px)
    float dragH = 26.0f * userScale;
    bool overDragArea = (mouse.x >= scaledMin.x && mouse.x <= scaledMax.x &&
                         mouse.y >= scaledMin.y && mouse.y <= scaledMin.y + dragH);

    if (io.MouseClicked[0] && overDragArea && !io.WantCaptureMouse)
    {
        g_dragging = true;
        g_dragOffset = ImVec2(mouse.x - g_winPos.x, mouse.y - g_winPos.y);
    }
    if (g_dragging)
    {
        if (io.MouseDown[0])
        {
            g_winPos.x = mouse.x - g_dragOffset.x;
            g_winPos.y = mouse.y - g_dragOffset.y;
            if (g_winPos.x < 8.0f) g_winPos.x = 8.0f;
            if (g_winPos.y < 8.0f) g_winPos.y = 8.0f;
            if (g_winPos.x + winW > disp.x - 8.0f) g_winPos.x = disp.x - winW - 8.0f;
            if (g_winPos.y + winH > disp.y - 8.0f) g_winPos.y = disp.y - winH - 8.0f;
        }
        else
        {
            g_dragging = false;
        }
    }

    ImU32 accent = theme::AccentU32(fade);
    ImVec4 accentV4 = theme::Accent;

    // --- Sombra suave de Rise (dropShadow 18, blur 30) ---
    widgets::SoftShadow(fg, scaledMin, scaledMax, round, 50.0f * fade, 24.0f * userScale);

    // --- Fondo principal de la ventana (UIColors.BACKGROUND: #171A21) ---
    fg->AddRectFilled(scaledMin, scaledMax, colors::background(fade), round);
    fg->AddRect(scaledMin, scaledMax, IM_COL32(255, 255, 255, (int)(18.0f * fade)), round, 0, 1.0f);

    // --- Sidebar Izquierda (UIColors.SECONDARY: #121419) ---
    ImVec2 sideMin = scaledMin;
    ImVec2 sideMax(scaledMin.x + sideW, scaledMax.y);
    fg->AddRectFilled(sideMin, sideMax, colors::secondary(fade), round, ImDrawFlags_RoundCornersLeft);

    // Sombra sutil / gradiente de la sidebar hacia el contenido
    fg->AddRectFilledMultiColor(ImVec2(sideMax.x, sideMin.y), ImVec2(sideMax.x + 14.0f, sideMax.y),
                                IM_COL32(0, 0, 0, (int)(32.0f * fade)),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, (int)(32.0f * fade)));

    // ========================================================================
    // Sidebar Header: Logo "Rise" + "6.9.5" (SidebarCategory.java)
    // ========================================================================
    {
        float titleX = sideMin.x + 14.0f * userScale;
        float titleY = sideMin.y + 12.0f * userScale;

        const char* title = "Azyre";
        float fontSz = 20.0f * userScale;
        fg->AddText(font, fontSz, ImVec2(titleX, titleY), colors::text(fade), title);

        ImVec2 titleSz = font->CalcTextSizeA(fontSz, FLT_MAX, 0.0f, title);

        // Versión "6.9.5" en color de acento justo al lado
        const char* ver = "1.1.0";
        float verSz = 12.0f * userScale;
        fg->AddText(font, verSz, ImVec2(titleX + titleSz.x + 4.0f, titleY + 2.0f),
                    accent, ver);
    }

    // ========================================================================
    // Sidebar Categories (CategoryComponent.java: d0 += 19.5)
    // ========================================================================
    float catStartY = sideMin.y + 44.0f * userScale;
    const float catItemH = 21.0f * userScale;
    const float catStepY = 23.5f * userScale;

    // Indicador animado de categoría seleccionada
    float targetIndY = catStartY + g_selectedCat * catStepY;
    if (g_catIndicatorY <= 0.0f)
        g_catIndicatorY = targetIndY;
    else
        g_catIndicatorY += (targetIndY - g_catIndicatorY) * clampf(dt * 16.0f, 0.0f, 1.0f);

    // Píldora activa de Rise (accentColor.darker() con bordes redondeados)
    {
        ImVec2 indMin(sideMin.x + 8.0f * userScale, g_catIndicatorY);
        ImVec2 indMax(sideMax.x - 8.0f * userScale, g_catIndicatorY + catItemH);

        ImU32 pillCol = IM_COL32((int)(accentV4.x * 255.0f * 0.38f),
                                 (int)(accentV4.y * 255.0f * 0.38f),
                                 (int)(accentV4.z * 255.0f * 0.38f),
                                 (int)(210.0f * fade));
        fg->AddRectFilled(indMin, indMax, pillCol, 5.0f);
        fg->AddRect(indMin, indMax, accent, 5.0f, 0, 1.0f);
    }

    for (int i = 0; i < kCatCount; ++i)
    {
        float itemY = catStartY + i * catStepY;
        ImVec2 itemMin(sideMin.x + 8.0f * userScale, itemY);
        ImVec2 itemMax(sideMax.x - 8.0f * userScale, itemY + catItemH);

        bool hovered = (mouse.x >= itemMin.x && mouse.x <= itemMax.x &&
                        mouse.y >= itemMin.y && mouse.y <= itemMax.y);
        bool selected = (g_selectedCat == i);

        if (hovered && io.MouseClicked[0] && !io.WantCaptureMouse)
        {
            g_selectedCat = i;
            g_scroll = g_scrollTarget = 0.0f;
            g_themeScroll = g_themeScrollTarget = 0.0f;
            g_themeFilter = -1;
        }

        if (hovered && !selected)
        {
            fg->AddRectFilled(itemMin, itemMax,
                              IM_COL32(255, 255, 255, (int)(12.0f * fade)), 5.0f);
        }

        // En Rise, el texto de la categoría seleccionada se desplaza ligeramente a la derecha
        float textOffsetX = selected ? 3.0f * userScale : 0.0f;
        float catTextSz = 13.0f * userScale;

        ImU32 textCol = selected ? colors::text(fade)
                                 : (hovered ? colors::secondaryText(fade) : colors::trinaryText(fade));

        ImVec2 textPos(itemMin.x + 8.0f * userScale + textOffsetX,
                       itemMin.y + (catItemH - catTextSz) * 0.5f);
        fg->AddText(font, catTextSz, textPos, textCol, kRiseCats[i].name);
    }

    // ========================================================================
    // Panel de Contenido Derecho
    // ========================================================================
    ImVec2 contentMin(sideMax.x + 1.0f, scaledMin.y);
    ImVec2 contentMax(scaledMax.x, scaledMax.y);
    float contentW = contentMax.x - contentMin.x;

    bool isSearchScreen = (kRiseCats[g_selectedCat].catEnum == -1);
    bool isThemeScreen = (kRiseCats[g_selectedCat].catEnum == -2);

    // Header del contenido
    float headerH = 34.0f * userScale;
    ImVec2 headerMin = contentMin;
    ImVec2 headerMax(contentMax.x, contentMin.y + headerH);

    // Título de la categoría o Search Box central
    if (!isSearchScreen && !isThemeScreen)
    {
        const char* currentCatName = kRiseCats[g_selectedCat].name;
        float catTitleSz = 16.0f * userScale;
        fg->AddText(font, catTitleSz,
                    ImVec2(headerMin.x + 14.0f * userScale,
                           headerMin.y + (headerH - catTitleSz) * 0.5f),
                    colors::text(fade), currentCatName);

        // Barra de búsqueda compacta a la derecha
        float searchW = 120.0f * userScale;
        float searchH = 22.0f * userScale;
        ImVec2 searchMin(headerMax.x - searchW - 14.0f * userScale,
                         headerMin.y + (headerH - searchH) * 0.5f);
        ImVec2 searchMax(searchMin.x + searchW, searchMin.y + searchH);

        fg->AddRectFilled(searchMin, searchMax, colors::secondary(fade), 11.0f);
        fg->AddRect(searchMin, searchMax, IM_COL32(255, 255, 255, (int)(22.0f * fade)), 11.0f, 0, 1.0f);

        riseSearchHost("##rise_search_host_quick", "##rise_quick_search", "Search...",
                       searchMin, ImVec2(searchW, searchH), g_searchQuery,
                       (int)sizeof(g_searchQuery), 8.0f, 2.0f, fade);
    }
    else if (isSearchScreen)
    {
        // SearchScreen.java: caja de búsqueda destacada
        float searchW = 200.0f * userScale;
        float searchH = 24.0f * userScale;
        ImVec2 searchMin(headerMin.x + (contentW - searchW) * 0.5f,
                         headerMin.y + (headerH - searchH) * 0.5f);
        ImVec2 searchMax(searchMin.x + searchW, searchMin.y + searchH);

        fg->AddRectFilled(searchMin, searchMax, colors::secondary(fade), 12.0f);
        fg->AddRect(searchMin, searchMax, accent, 12.0f, 0, 1.0f);

        riseSearchHost("##rise_search_host_main", "##rise_main_search", "Type to search...",
                       searchMin, ImVec2(searchW, searchH), g_searchQuery,
                       (int)sizeof(g_searchQuery), 10.0f, 3.0f, fade);
    }
    else if (isThemeScreen)
    {
        // ThemeScreen: título centrado como Rise (Color.WHITE alpha 128)
        const char* themeTitle = "Themes";
        float themeTitleSz = 16.0f * userScale;
        ImVec2 themeTs = font->CalcTextSizeA(themeTitleSz, FLT_MAX, 0.0f, themeTitle);
        fg->AddText(font, themeTitleSz,
                    ImVec2(headerMin.x + (contentW - themeTs.x) * 0.5f,
                           headerMin.y + (headerH - themeTitleSz) * 0.5f),
                    IM_COL32(255, 255, 255, (int)(128.0f * fade)), themeTitle);
    }

    // Línea divisoria muy sutil bajo el header
    fg->AddLine(ImVec2(contentMin.x + 10.0f, headerMax.y),
                ImVec2(contentMax.x - 10.0f, headerMax.y),
                IM_COL32(255, 255, 255, (int)(12.0f * fade)), 1.0f);

    // ========================================================================
    // ThemeScreen — fiel a ThemeScreen.java + ThemeComponent.java de Rise 6.9.5:
    //   * titulo "Themes" centrado (alpha 128, ya dibujado en el header)
    //   * fila de KeyColors (5 por fila, 2 filas) que filtra los temas
    //   * grid de 3 columnas, tarjetas de 50px con preview de gradiente
    // ========================================================================
    if (!isThemeScreen)
        g_themeScreenWasActive = false;

    if (isThemeScreen)
    {
        // Primer frame en ThemeScreen: colocar las tarjetas sin animar
        // (Rise: ThemeScreen.aT() pone todo en su sitio al abrir la pantalla).
        bool firstFrame = !g_themeScreenWasActive;
        g_themeScreenWasActive = true;

        float padX = 12.0f * userScale;

        // ---- Filtros de KeyColors (KeyColors.java) ----
        float filterTop = headerMax.y + 6.0f * userScale;
        float filterGap = 6.0f * userScale;
        float filterH = 17.0f * userScale;
        float filterW = (contentW - padX * 2.0f - filterGap * 4.0f) / 5.0f;

        for (int k = 0; k < 10; ++k)
        {
            int fr = k / 5, fc = k % 5;
            float fx = contentMin.x + padX + fc * (filterW + filterGap);
            float fy = filterTop + fr * (filterH + filterGap);
            ImVec2 fmin(fx, fy), fmax(fx + filterW, fy + filterH);

            bool fHover = (mouse.x >= fmin.x && mouse.x <= fmax.x &&
                           mouse.y >= fmin.y && mouse.y <= fmax.y &&
                           mouse.x >= scaledMin.x && mouse.x <= scaledMax.x &&
                           mouse.y >= scaledMin.y && mouse.y <= scaledMax.y);
            bool fSel = (g_themeFilter == k);

            // Animaciones de Rise: dimAnimation + bloomAnimation
            float dimTarget = (g_themeFilter >= 0 && !fSel) ? 0.0f : 1.0f;
            float bloomTarget = fSel ? 1.0f : 0.0f;
            if (firstFrame)
            {
                g_keyDim[k] = dimTarget;
                g_keyBloom[k] = bloomTarget;
            }
            else
            {
                g_keyDim[k] = approach(g_keyDim[k], dimTarget, 12.0f, dt);
                g_keyBloom[k] = approach(g_keyBloom[k], bloomTarget, 12.0f, dt);
            }

            float dim = 0.30f + 0.70f * g_keyDim[k];
            float bloom = g_keyBloom[k];

            // Glow del filtro seleccionado (bloom)
            if (bloom > 0.01f)
            {
                for (int g = 1; g <= 3; ++g)
                {
                    float e = g * 2.0f * userScale;
                    fg->AddRect(ImVec2(fmin.x - e, fmin.y - e), ImVec2(fmax.x + e, fmax.y + e),
                                IM_COL32(255, 255, 255,
                                         (int)(50.0f * fade * bloom / g)),
                                7.0f, 0, 2.0f);
                }
            }

            ImU32 kc = IM_COL32(kKeyColors[k][0], kKeyColors[k][1], kKeyColors[k][2],
                                (int)(255.0f * fade * dim));
            fg->AddRectFilled(fmin, fmax, kc, 5.0f);
            if (bloom > 0.01f)
                fg->AddRect(fmin, fmax, IM_COL32(255, 255, 255, (int)(230.0f * fade * bloom)),
                            5.0f, 0, 2.0f);
            else if (fHover)
                fg->AddRect(fmin, fmax, IM_COL32(255, 255, 255, (int)(110.0f * fade * dim)),
                            5.0f, 0, 1.5f);

            if (fHover && io.MouseClicked[0])
            {
                g_themeFilter = fSel ? -1 : k;
                g_themeScrollTarget = 0.0f;
            }
        }

        // ---- Grid de temas ----
        float gridTop = filterTop + 2.0f * (filterH + filterGap) + 10.0f * userScale;
        float gridW = contentW - padX * 2.0f;
        float cardGapX = 7.0f * userScale;
        float cardW = (gridW - cardGapX * 2.0f) / 3.0f;
        float cardH = 50.0f * userScale;
        float rowStep = 57.0f * userScale;

        // Lista visible segun el filtro de KeyColors (ThemeScreen.qv)
        int visIndex[kThemeCount];
        int visibleCount = 0;
        for (int i = 0; i < kThemeCount; ++i)
            visIndex[i] = -1;
        for (int i = 0; i < kThemeCount; ++i)
        {
            if (g_themeFilter < 0 || (kRiseThemes[i].keyMask & (1u << g_themeFilter)))
                visIndex[i] = visibleCount++;
        }

        int totalRows = (visibleCount + 2) / 3;
        float contentH = totalRows * rowStep;
        ImVec2 gridClipMin(contentMin.x, gridTop);
        ImVec2 gridClipMax(contentMax.x, contentMax.y - 4.0f);
        float gridVisibleH = gridClipMax.y - gridClipMin.y;
        if (gridVisibleH < 1.0f) gridVisibleH = 1.0f;
        float maxScroll = (contentH > gridVisibleH) ? (contentH - gridVisibleH) : 0.0f;

        bool mouseOverGrid = (mouse.x >= contentMin.x && mouse.x <= contentMax.x &&
                              mouse.y >= gridTop && mouse.y <= contentMax.y - 4.0f);
        if (mouseOverGrid && io.MouseWheel != 0.0f)
            g_themeScrollTarget -= io.MouseWheel * 45.0f;
        if (g_themeScrollTarget < 0.0f) g_themeScrollTarget = 0.0f;
        if (g_themeScrollTarget > maxScroll) g_themeScrollTarget = maxScroll;
        g_themeScroll += (g_themeScrollTarget - g_themeScroll) * clampf(dt * 15.0f, 0.0f, 1.0f);

        fg->PushClipRect(gridClipMin, gridClipMax, true);

        // Renderizamos TODOS los temas: los que salen del filtro hacen fade-out
        // en su ultima posicion (ThemeScreen.onRender -> opacityAnimation).
        for (int i = 0; i < kThemeCount; ++i)
        {
            const RiseTheme& t = kRiseThemes[i];
            ThemeAnim& an = g_themeAnim[i];
            int v = visIndex[i];
            bool vis = (v >= 0);

            if (vis)
            {
                int row = v / 3, col = v % 3;
                float tx = contentMin.x + padX + col * (cardW + cardGapX);
                float ty = gridTop + row * rowStep - g_themeScroll;
                if (firstFrame || !an.inited)
                {
                    an.x = tx;
                    an.y = ty;
                    an.inited = true;
                }
                else
                {
                    an.x = approach(an.x, tx, 14.0f, dt);
                    an.y = approach(an.y, ty, 14.0f, dt);
                }
            }
            else if (!an.inited)
            {
                continue;
            }

            float opTarget = vis ? 1.0f : 0.0f;
            an.opacity = firstFrame ? opTarget : approach(an.opacity, opTarget, 12.0f, dt);
            if (an.opacity <= 0.01f)
                continue;

            // Culling
            if (an.y + cardH < gridClipMin.y || an.y > gridClipMax.y)
                continue;

            float om = an.opacity;
            float cardFade = fade * om;

            ImVec2 cardMin(an.x, an.y);
            ImVec2 cardMax(an.x + cardW, an.y + cardH);

            bool hovered = vis && (mouse.x >= cardMin.x && mouse.x <= cardMax.x &&
                                   mouse.y >= cardMin.y && mouse.y <= cardMax.y &&
                                   mouseOverGrid);

            bool isCurrent = t.fn ? theme::AccentRainbow
                : (std::fabs(theme::Accent.x - t.a[0] / 255.0f) < 0.008f &&
                   std::fabs(theme::Accent.y - t.a[1] / 255.0f) < 0.008f &&
                   std::fabs(theme::Accent.z - t.a[2] / 255.0f) < 0.008f);

            an.selector = firstFrame ? (isCurrent ? 1.0f : 0.0f)
                                     : approach(an.selector, isCurrent ? 1.0f : 0.0f, 14.0f, dt);

            // Fondo de tarjeta (Rise: Color(18,21,30) o Color(15,19,26) si activo)
            ImU32 cardBg = isCurrent
                ? IM_COL32(15, 19, 26, (int)(255.0f * cardFade))
                : IM_COL32(18, 21, 30, (int)(255.0f * cardFade));
            fg->AddRectFilled(cardMin, cardMax, cardBg, 10.0f);

            // Tema funcion (Rainbow): rA/rB/rC se calculan en runtime con
            // getAccentColor (ColorUtil.aB) en (0,0), (0,50) y (0,100) — igual
            // que Themes.rA()/rB()/rC() de Rise. triColor = true.
            RiseTheme tmp;
            const RiseTheme* tp = &t;
            if (t.fn)
            {
                tmp = t;
                ImVec4 rA = theme::rainbowAt(0.0f, 0.0f);
                ImVec4 rB = theme::rainbowAt(0.0f, 50.0f);
                ImVec4 rC = theme::rainbowAt(0.0f, 100.0f);
                const float* fa = &rA.x;
                const float* fb = &rB.x;
                const float* fc = &rC.x;
                for (int k = 0; k < 3; ++k)
                {
                    tmp.a[k] = (unsigned char)(fa[k] * 255.0f + 0.5f);
                    tmp.b[k] = (unsigned char)(fb[k] * 255.0f + 0.5f);
                    tmp.c[k] = (unsigned char)(fc[k] * 255.0f + 0.5f);
                }
                tmp.tri = true;
                tp = &tmp;
            }
            const RiseTheme& tc = *tp;

            // Preview con gradiente ANIMADO (shader rgqtest.glsl de Rise):
            // mix(rA, rB, 0.5 + 0.5*sin(3.0*(coord + tiempo))) sobre el eje X.
            // Se dibuja en 2 tramos lineales (aprox del seno) con las esquinas
            // superiores redondeadas SOLO en los extremos. OJO: para los tramos
            // interiores hay que pasar RoundCornersNone, si no ImGui redondea
            // todas las esquinas y cada tramo se ve como una pastilla suelta.
            float prevH = 30.0f * userScale;
            ImVec2 pMin(cardMin.x, cardMin.y);
            ImVec2 pMax(cardMax.x, cardMin.y + prevH);
            {
                float pw = pMax.x - pMin.x;
                float xm = pMin.x + pw * 0.5f;

                ImU32 c0 = themeShaderColor(tc, 0.0f, cardFade);
                ImU32 c1 = themeShaderColor(tc, 0.5f, cardFade);
                ImU32 c2 = themeShaderColor(tc, 1.0f, cardFade);

                roundedGradient(fg, ImVec2(pMin.x, pMin.y), ImVec2(xm, pMax.y),
                                ImVec2(pMin.x, pMin.y), ImVec2(xm, pMin.y),
                                c0, c1, 10.0f, ImDrawFlags_RoundCornersTopLeft);
                roundedGradient(fg, ImVec2(xm, pMin.y), ImVec2(pMax.x, pMax.y),
                                ImVec2(xm, pMin.y), ImVec2(pMax.x, pMin.y),
                                c1, c2, 10.0f, ImDrawFlags_RoundCornersTopRight);
            }

            // Nombre centrado (Rise: drawString centrado a 37px del top)
            float nameFontSz = 14.0f * userScale;
            ImVec2 nameTs = font->CalcTextSizeA(nameFontSz, FLT_MAX, 0.0f, t.name);
            ImU32 cAstatic = IM_COL32(tc.a[0], tc.a[1], tc.a[2], (int)(255.0f * cardFade));
            ImU32 nameCol = isCurrent ? cAstatic : IM_COL32(255, 255, 255, (int)(255.0f * cardFade));
            fg->AddText(font, nameFontSz,
                        ImVec2(cardMin.x + (cardW - nameTs.x) * 0.5f,
                               cardMin.y + 34.0f * userScale),
                        nameCol, t.name);

            // Bloom del tema activo (Rise: selectorAnimation + shader bloom)
            float sel = an.selector;
            if (sel > 0.01f)
            {
                for (int g = 1; g <= 3; ++g)
                {
                    float e = g * 2.0f * userScale;
                    fg->AddRect(ImVec2(cardMin.x - e, cardMin.y - e),
                                ImVec2(cardMax.x + e, cardMax.y + e),
                                IM_COL32(tc.a[0], tc.a[1], tc.a[2],
                                         (int)(45.0f * fade * sel / (float)g)),
                                12.0f, 0, 2.0f);
                }
                fg->AddRect(cardMin, cardMax,
                            IM_COL32(tc.a[0], tc.a[1], tc.a[2], (int)(255.0f * fade * sel)),
                            10.0f, 0, 2.0f);
            }
            else if (hovered)
            {
                fg->AddRect(cardMin, cardMax,
                            IM_COL32(255, 255, 255, (int)(40.0f * cardFade)), 10.0f, 0, 1.5f);
            }

            // Click: aplicar tema (accent = rA/rB/rC para el gradiente animado)
            if (hovered && io.MouseClicked[0])
            {
                if (t.fn)
                {
                    // Rainbow: acento dinamico (ColorUtil.aB). Guardamos un
                    // snapshot en Accent para los usos estaticos del chrome.
                    theme::AccentRainbow = true;
                    ImVec4 snapA = theme::rainbowAt(0.0f, 0.0f);
                    theme::Accent = snapA;
                    theme::Accent2 = theme::rainbowAt(0.0f, 50.0f);
                    theme::Accent3 = theme::rainbowAt(0.0f, 100.0f);
                    theme::AccentDeep = ImVec4(snapA.x * 0.62f, snapA.y * 0.62f,
                                               snapA.z * 0.62f, 1.0f);
                    theme::AccentTri = true;
                }
                else
                {
                    theme::AccentRainbow = false;
                    theme::Accent.x = t.a[0] / 255.0f;
                    theme::Accent.y = t.a[1] / 255.0f;
                    theme::Accent.z = t.a[2] / 255.0f;
                    theme::AccentDeep.x = t.b[0] / 255.0f * 0.9f;
                    theme::AccentDeep.y = t.b[1] / 255.0f * 0.9f;
                    theme::AccentDeep.z = t.b[2] / 255.0f * 0.9f;
                    theme::Accent2.x = t.b[0] / 255.0f;
                    theme::Accent2.y = t.b[1] / 255.0f;
                    theme::Accent2.z = t.b[2] / 255.0f;
                    theme::Accent3.x = t.c[0] / 255.0f;
                    theme::Accent3.y = t.c[1] / 255.0f;
                    theme::Accent3.z = t.c[2] / 255.0f;
                    theme::AccentTri = t.tri;
                }
                theme::apply();
                config::saveAll();
            }
        }
        fg->PopClipRect();

        // Scrollbar del grid
        if (maxScroll > 0.0f && gridVisibleH > 30.0f)
        {
            float trackX = contentMax.x - 6.0f;
            float barH = ImMax(gridVisibleH * (gridVisibleH / contentH), 24.0f);
            float travel = gridVisibleH - barH;
            float pct = (g_themeScroll / maxScroll);
            float barY = gridTop + pct * travel;
            fg->AddRectFilled(ImVec2(trackX, gridTop),
                              ImVec2(trackX + 4.0f, gridTop + gridVisibleH),
                              IM_COL32(255, 255, 255, (int)(10.0f * fade)), 2.0f);
            fg->AddRectFilled(ImVec2(trackX, barY),
                              ImVec2(trackX + 4.0f, barY + barH),
                              IM_COL32(255, 255, 255, (int)(38.0f * fade)), 2.0f);
        }
    }

    // ========================================================================
    // Lista de Módulos (Rise Module Cards: 283x38)
    // ========================================================================
    // En ThemeScreen la lista queda vacia (el grid ya se renderizo arriba).

    ImVec2 listMin(contentMin.x, headerMax.y + 4.0f);
    ImVec2 listMax(contentMax.x, contentMax.y - 6.0f);
    float listVisibleH = listMax.y - listMin.y;

    std::vector<Module*> modules;
    bool searchActive = isSearchScreen || (g_searchQuery[0] != '\0');

    if (isThemeScreen)
    {
        // ThemeScreen: lista vacia — el grid de temas ya se renderizo.
    }
    else if (searchActive)
    {
        for (auto& up : mgr.all())
        {
            Module* m = up.get();
            if (g_searchQuery[0] == '\0' ||
                containsCI(m->name(), g_searchQuery) ||
                containsCI(m->description(), g_searchQuery))
            {
                modules.push_back(m);
            }
        }
    }
    else
    {
        Category cat = (Category)kRiseCats[g_selectedCat].catEnum;
        modules = mgr.byCategory(cat);
    }

    // Actualizar animaciones de módulos (expand/collapse con easing)
    for (Module* m : modules)
    {
        float targetEnable = m->enabled() ? 1.0f : 0.0f;
        g_enableAnim[m] += (targetEnable - g_enableAnim[m]) * clampf(dt * 15.0f, 0.0f, 1.0f);

        float targetExpand = g_expanded[m] ? 1.0f : 0.0f;
        g_expandAnim[m] += (targetExpand - g_expandAnim[m]) * clampf(dt * 13.0f, 0.0f, 1.0f);
    }

    // Geometría oficial de tarjetas de Rise (ModuleComponent: 283 x 38, gap 7)
    const float cardBaseH = 38.0f * userScale;
    const float cardGap = 7.0f * userScale;
    const float cardMarginX = 8.0f * userScale;
    const float cardW = contentW - cardMarginX * 2.0f;

    // Calcular altura total para scroll — altura animada de settings (para que
    // el scroll acompana el expand/collapse) pero minimo el tamano natural.
    float totalContentH = 0.0f;
    for (Module* m : modules)
    {
        float natH = g_settingsH.count(m) ? g_settingsH[m] : 100.0f;
        float a = g_expandAnim[m];
        float drawerH = (a > 0.001f) ? natH * a : 0.0f;
        totalContentH += cardBaseH + drawerH + cardGap;
    }
    totalContentH += 8.0f;

    // Manejo de scroll — sin WantCaptureMouse (los drawers ImGui dentro de
    // las tarjetas capturan el mouse y mataban el scroll de la lista).
    bool mouseOverList = (mouse.x >= listMin.x && mouse.x <= listMax.x &&
                          mouse.y >= listMin.y && mouse.y <= listMax.y);
    float maxScroll = (totalContentH > listVisibleH) ? (totalContentH - listVisibleH) : 0.0f;

    // [ScrollDBG]: el wheel llega a io.MouseWheel (ver [WheelDBG] tras
    // NewFrame); si la lista no scrollea, la razon exacta queda aqui.
    if (io.MouseWheel != 0.0f)
    {
        static ULONGLONG lastRiseDbg = 0;
        ULONGLONG nowRiseDbg = GetTickCount64();
        if (nowRiseDbg - lastRiseDbg > 400ull)
        {
            lastRiseDbg = nowRiseDbg;
            MC_LOG("[ScrollDBG][Rise] w=%.2f overList=%d pos=(%.0f,%.0f) "
                   "list=(%.0f,%.0f)-(%.0f,%.0f) maxScroll=%.0f mods=%d",
                   io.MouseWheel, mouseOverList ? 1 : 0, mouse.x, mouse.y,
                   listMin.x, listMin.y, listMax.x, listMax.y, maxScroll,
                   (int)modules.size());
        }
    }

    if (mouseOverList && io.MouseWheel != 0.0f)
    {
        g_scrollTarget -= io.MouseWheel * 45.0f;
        if (g_scrollTarget < 0.0f) g_scrollTarget = 0.0f;
        if (g_scrollTarget > maxScroll) g_scrollTarget = maxScroll;
    }
    if (g_scrollTarget > maxScroll) g_scrollTarget = maxScroll;
    if (g_scrollTarget < 0.0f) g_scrollTarget = 0.0f;

    g_scroll += (g_scrollTarget - g_scroll) * clampf(dt * 15.0f, 0.0f, 1.0f);

    fg->PushClipRect(listMin, listMax, true);

    struct SettingsDrawer
    {
        Module* m;
        ImVec4 slot;
        float expandA;
        float natH;
    };
    std::vector<SettingsDrawer> drawersToRender;

    float currentY = listMin.y + 4.0f - g_scroll;

    for (Module* m : modules)
    {
        float natH = g_settingsH.count(m) ? g_settingsH[m] : 100.0f;
        float a = g_expandAnim[m];
        float drawerH = (a > 0.001f) ? (natH * a) : 0.0f;
        float fullCardH = cardBaseH + drawerH;

        ImVec2 cMin(contentMin.x + cardMarginX, currentY);
        ImVec2 cMax(cMin.x + cardW, currentY + fullCardH);
        ImVec2 cHeaderMax(cMax.x, currentY + cardBaseH);

        if (cMax.y >= listMin.y && cMin.y <= listMax.y)
        {
            bool cardHeaderHovered = (mouse.x >= cMin.x && mouse.x <= cMax.x &&
                                      mouse.y >= cMin.y && mouse.y <= cHeaderMax.y &&
                                      mouseOverList);

            float targetHover = cardHeaderHovered ? 1.0f : 0.0f;
            g_hoverAnim[m] += (targetHover - g_hoverAnim[m]) * clampf(dt * 14.0f, 0.0f, 1.0f);
            float hAnim = g_hoverAnim[m];

            // Fondo de la tarjeta: Rise UIColors.OVERLAY (0,0,0,50) + hover sutil
            ImU32 cardBg = IM_COL32(0, 0, 0, (int)((50.0f + 25.0f * hAnim) * fade));
            fg->AddRectFilled(cMin, cMax, cardBg, 6.0f);

            // Borde sutil (acento cuando habilitado)
            float eAnim = g_enableAnim[m];
            ImU32 borderCol = (eAnim > 0.01f)
                ? IM_COL32((int)(accentV4.x * 255.0f), (int)(accentV4.y * 255.0f),
                           (int)(accentV4.z * 255.0f), (int)(55.0f * eAnim * fade))
                : IM_COL32(255, 255, 255, (int)((12.0f + 14.0f * hAnim) * fade));
            fg->AddRect(cMin, cMax, borderCol, 6.0f, 0, 1.0f);

            // Nombre del módulo
            float nameFontSz = 15.0f * userScale;
            float nameX = cMin.x + 8.0f * userScale;
            float nameY = cMin.y + 6.0f * userScale;

            ImU32 nameCol = (eAnim > 0.01f)
                ? accent
                : colors::text(fade * (0.80f + 0.20f * hAnim));

            fg->AddText(font, nameFontSz, ImVec2(nameX, nameY), nameCol, m->name().c_str());

            ImVec2 nameSz = font->CalcTextSizeA(nameFontSz, FLT_MAX, 0.0f, m->name().c_str());

            if (searchActive)
            {
                char catTag[64];
                snprintf(catTag, sizeof(catTag), "(%s)", categoryName(m->category()));
                float tagSz = 11.0f * userScale;
                fg->AddText(font, tagSz,
                            ImVec2(nameX + nameSz.x + 6.0f, nameY + 2.0f),
                            colors::trinaryText(fade), catTag);
            }

            // Descripción del módulo
            float descFontSz = 11.0f * userScale;
            float descY = nameY + nameSz.y + 1.0f;
            if (!m->description().empty())
            {
                fg->AddText(font, descFontSz, ImVec2(nameX, descY),
                            colors::trinaryText(fade), m->description().c_str());
            }

            // Controles del lado derecho
            float rightX = cMax.x - 8.0f * userScale;

            // Chevron de settings
            if (m->hasSettings())
            {
                float chevW = 18.0f * userScale;
                rightX -= chevW;
                ImVec2 chevMin(rightX, cMin.y + (cardBaseH - chevW) * 0.5f);
                ImVec2 chevMax(rightX + chevW, chevMin.y + chevW);

                bool chevHover = (mouse.x >= chevMin.x && mouse.x <= chevMax.x &&
                                  mouse.y >= chevMin.y && mouse.y <= chevMax.y &&
                                  mouseOverList);

                if (chevHover && io.MouseClicked[0])
                {
                    g_expanded[m] = !g_expanded[m];
                }

                const char* chev = g_expanded[m] ? "v" : ">";
                float chSz = 13.0f * userScale;
                ImVec2 chSzBox = font->CalcTextSizeA(chSz, FLT_MAX, 0.0f, chev);
                fg->AddText(font, chSz,
                            ImVec2(chevMin.x + (chevW - chSzBox.x) * 0.5f,
                                   chevMin.y + (chevW - chSzBox.y) * 0.5f),
                            chevHover ? colors::text(fade) : colors::trinaryText(fade), chev);
            }

            // Chip de Keybind
            {
                char bindText[32];
                if (mgr.binding() == m)
                    snprintf(bindText, sizeof(bindText), "...");
                else if (m->keybind() != 0)
                    snprintf(bindText, sizeof(bindText), "%s", input::keyName(m->keybind()).c_str());
                else
                    snprintf(bindText, sizeof(bindText), "NONE");

                float bindFontSz = 10.5f * userScale;
                ImVec2 bindSz = font->CalcTextSizeA(bindFontSz, FLT_MAX, 0.0f, bindText);
                float bindPillW = bindSz.x + 10.0f * userScale;
                float bindPillH = 18.0f * userScale;

                rightX -= (bindPillW + 4.0f * userScale);
                ImVec2 bindMin(rightX, cMin.y + (cardBaseH - bindPillH) * 0.5f);
                ImVec2 bindMax(rightX + bindPillW, bindMin.y + bindPillH);

                bool bindHover = (mouse.x >= bindMin.x && mouse.x <= bindMax.x &&
                                  mouse.y >= bindMin.y && mouse.y <= bindMax.y &&
                                  mouseOverList);

                if (bindHover && io.MouseClicked[0])
                {
                    if (mgr.binding() == m)
                        mgr.setBinding(nullptr);
                    else
                        mgr.setBinding(m);
                }

                ImU32 bindBg = (mgr.binding() == m)
                    ? accent
                    : (bindHover ? IM_COL32(35, 40, 52, (int)(220.0f * fade))
                                 : colors::background(fade));
                fg->AddRectFilled(bindMin, bindMax, bindBg, 4.0f);
                fg->AddRect(bindMin, bindMax,
                            (mgr.binding() == m) ? accent : IM_COL32(255, 255, 255, (int)(20.0f * fade)),
                            4.0f, 0, 1.0f);

                ImU32 bindTxtCol = (mgr.binding() == m) ? IM_COL32(20, 20, 20, 255)
                                                        : colors::secondaryText(fade);
                fg->AddText(font, bindFontSz,
                            ImVec2(bindMin.x + (bindPillW - bindSz.x) * 0.5f,
                                   bindMin.y + (bindPillH - bindSz.y) * 0.5f),
                            bindTxtCol, bindText);
            }

            // Click en el cuerpo de la tarjeta (solo en el HEADER, no en el drawer):
            // - Left click: toggle módulo
            // - Right click: expand/collapse settings
            if (cardHeaderHovered && mouseOverList)
            {
                bool overRightControls = (mouse.x >= rightX - 4.0f);
                if (!overRightControls)
                {
                    if (io.MouseClicked[0])
                    {
                        m->toggle();
                    }
                    if (io.MouseClicked[1] && m->hasSettings())
                    {
                        g_expanded[m] = !g_expanded[m];
                    }
                }
            }

            // Registrar drawer para renderizar settings
            if (a > 0.01f && m->hasSettings())
            {
                // Línea divisoria muy tenue
                fg->AddLine(ImVec2(cMin.x + 6.0f, cHeaderMax.y),
                            ImVec2(cMax.x - 6.0f, cHeaderMax.y),
                            IM_COL32(255, 255, 255, (int)(12.0f * fade)), 1.0f);

                // Fondo sutil del área de settings (mas visible que el card bg)
                fg->AddRectFilled(ImVec2(cMin.x + 2.0f, cHeaderMax.y + 2.0f),
                                 ImVec2(cMax.x - 2.0f, cHeaderMax.y + 2.0f + natH * a),
                                 IM_COL32(255, 255, 255, (int)(8.0f * fade * a)), 4.0f);

                ImVec4 slot(cMin.x + 6.0f, cHeaderMax.y + 4.0f,
                            cMax.x - 6.0f, cHeaderMax.y + 4.0f + natH);
                drawersToRender.push_back({m, slot, a, natH});
            }
        }

        currentY += fullCardH + cardGap;
    }

    fg->PopClipRect();

    // ========================================================================
    // Renderizado de Settings — ventana raíz ImGui (recorta TODO al área
    // de la lista, sin overflow) con child windows animadas por módulo.
    // ========================================================================
    if (!drawersToRender.empty())
    {
        // Escalar los widgets de settings igual que el chrome de Rise.
        float fontBase = ImGui::GetStyle().FontSizeBase;
        if (fontBase <= 0.0f)
            fontBase = 16.0f;
        ImGui::PushFont(nullptr, fontBase * userScale);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * userScale, 6.0f * userScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * userScale, 3.0f * userScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * userScale);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f * userScale);

        // Colores Rise
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(1.0f, 1.0f, 1.0f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.071f, 0.078f, 0.098f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.10f, 0.11f, 0.14f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.12f, 0.13f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09f, 0.10f, 0.13f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.15f, 0.19f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::Accent);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, theme::Accent);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, theme::Accent);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, theme::Accent);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.071f, 0.078f, 0.098f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_SeparatorHovered, theme::Accent);
        ImGui::PushStyleColor(ImGuiCol_SeparatorActive, theme::Accent);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        // Ventana raíz: cubre TODO el área de contenido (incluyendo el espacio
        // scrolleable). Los child windows se posicionan con coordenadas relativas
        // al origen de la lista, pueden ser negativos o > listVisibleH — la raíz
        // recorta visualmente pero el contenido siempre existe para el scroll.
        // NO SCROLL WITH MOUSE: sin este flag, ImGui detecta que la raiz tiene
        // ScrollMax > 0 (los drawers pasan del area visible) y la scrollea
        // INTERNAMENTE con la rueda, desincronizando los settings de las
        // tarjetas (el "clipping" al scrollear). El scroll lo maneja sRise.
        ImGui::SetNextWindowPos(ImVec2(listMin.x, listMin.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(contentW, listVisibleH), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

        ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                                     ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::Begin("##rise_settings_root", nullptr, rootFlags))
        {
            for (auto& drawer : drawersToRender)
            {
                if (drawer.expandA < 0.01f)
                    continue;

                float childH = drawer.natH * drawer.expandA;
                if (childH < 4.0f)
                    continue;

                float childW = drawer.slot.z - drawer.slot.x;

                // Posición local dentro de la ventana raíz
                float localX = drawer.slot.x - listMin.x;
                float localY = drawer.slot.y - listMin.y;

                // Altura = drawer ANIMADO (natH * a): con a=1 el child mide el
                // contenido completo (nada se corta), y durante la animacion de
                // expand/collapse el contenido se recorta al area visible del
                // drawer — igual que Rise — en vez de desbordarse sobre la
                // tarjeta siguiente.
                ImGui::SetCursorPos(ImVec2(localX, localY));

                char childId[96];
                snprintf(childId, sizeof(childId), "##rise_c_%s", drawer.m->name().c_str());

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                    ImVec2(6.0f * userScale, 4.0f * userScale));
                ImGui::BeginChild(childId, ImVec2(childW, childH), false,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoScrollWithMouse);
                {
                    drawer.m->drawSettings();
                    float contentEnd = ImGui::GetCursorPosY();
                    if (contentEnd > 10.0f)
                    {
                        g_settingsH[drawer.m] = contentEnd + 8.0f;
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleVar();
            }
        }
        ImGui::End();

        ImGui::PopStyleColor(20);
        ImGui::PopStyleVar(6);
        ImGui::PopFont();
    }

    // ========================================================================
    // Scrollbar lateral (draggable, como Rise)
    // ========================================================================
    if (totalContentH > listVisibleH && listVisibleH > 30.0f)
    {
        float trackX = contentMax.x - 6.0f;
        float trackH = listVisibleH;
        float barH = ImMax(listVisibleH * (listVisibleH / totalContentH), 24.0f);
        float maxThumbTravel = trackH - barH;
        float scrollPct = (maxScroll > 0.0f) ? (g_scroll / maxScroll) : 0.0f;
        float barY = listMin.y + scrollPct * maxThumbTravel;

        ImVec2 thumbMin(trackX, barY);
        ImVec2 thumbMax(trackX + 4.0f, barY + barH);
        ImVec2 trackMin(trackX, listMin.y);
        ImVec2 trackMax(trackX + 4.0f, listMax.y);

        // Deteccion de hover sobre el thumb
        bool thumbHover = (mouse.x >= thumbMin.x - 3.0f && mouse.x <= thumbMax.x + 3.0f &&
                           mouse.y >= thumbMin.y && mouse.y <= thumbMax.y);

        // Iniciar drag del thumb
        static bool s_barDragging = false;
        static float s_barDragStartY = 0.0f;
        static float s_barDragStartScroll = 0.0f;

        if (thumbHover && io.MouseClicked[0])
        {
            s_barDragging = true;
            s_barDragStartY = mouse.y;
            s_barDragStartScroll = g_scrollTarget;
        }

        // Click en la pista (no en el thumb) = saltar a esa posicion
        bool trackHover = (mouse.x >= trackMin.x - 2.0f && mouse.x <= trackMax.x + 2.0f &&
                           mouse.y >= trackMin.y && mouse.y <= trackMax.y);
        if (trackHover && !thumbHover && io.MouseClicked[0])
        {
            float clickPct = (mouse.y - listMin.y) / trackH;
            g_scrollTarget = clickPct * maxScroll;
            s_barDragging = true;
            s_barDragStartY = mouse.y;
            s_barDragStartScroll = g_scrollTarget;
        }

        if (s_barDragging)
        {
            if (io.MouseDown[0])
            {
                float dy = mouse.y - s_barDragStartY;
                float scrollPerPx = (maxScroll > 0.0f && maxThumbTravel > 0.0f)
                                        ? maxScroll / maxThumbTravel
                                        : 0.0f;
                g_scrollTarget = s_barDragStartScroll + dy * scrollPerPx;
                if (g_scrollTarget < 0.0f) g_scrollTarget = 0.0f;
                if (g_scrollTarget > maxScroll) g_scrollTarget = maxScroll;
            }
            else
            {
                s_barDragging = false;
            }
        }

        // Pista de fondo muy sutil
        fg->AddRectFilled(trackMin, trackMax, IM_COL32(255, 255, 255, (int)(10.0f * fade)), 2.0f);

        // Thumb
        ImU32 thumbCol = (s_barDragging || thumbHover)
                             ? IM_COL32(255, 255, 255, (int)(70.0f * fade))
                             : IM_COL32(255, 255, 255, (int)(38.0f * fade));
        fg->AddRectFilled(thumbMin, thumbMax, thumbCol, 2.0f);
    }

    // ========================================================================
    // Indicador flotante cuando se está esperando una tecla de bind
    // (overlay: se dibuja encima de todo, por eso usa fgTop)
    // ========================================================================
    if (mgr.binding())
    {
        const char* bindNotice = "Binding key: Press any key | ESC to cancel";
        float noticeSz = 14.0f * userScale;
        ImVec2 nSz = font->CalcTextSizeA(noticeSz, FLT_MAX, 0.0f, bindNotice);

        ImVec2 nMin((disp.x - nSz.x) * 0.5f - 12.0f, disp.y - 55.0f);
        ImVec2 nMax(nMin.x + nSz.x + 24.0f, nMin.y + nSz.y + 14.0f);

        fgTop->AddRectFilled(nMin, nMax, colors::secondary(fade), 7.0f);
        fgTop->AddRect(nMin, nMax, accent, 7.0f, 0, 1.2f);
        fgTop->AddText(font, noticeSz, ImVec2(nMin.x + 12.0f, nMin.y + 7.0f),
                    colors::text(fade), bindNotice);
    }
}

float uiScale()
{
    return g_uiScale;
}

} // namespace mc::gui::sRise
