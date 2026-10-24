#include "ArrayList.hpp"

#include "Framework/ColorUtils.hpp"
#include "Framework/ImTween.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Interface/Interface.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/DX11.hpp"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace mc {

namespace {

struct RowState
{
    float anim = 0.0f; // 0 = fuera, 1 = dentro
    std::unique_ptr<ImTween<float>> tween;
};

std::map<const Module*, RowState>& rowStates()
{
    static std::map<const Module*, RowState> s;
    return s;
}

float easeOutCubic(float t)
{
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

std::string separateWords(const std::string& s)
{
    if (s.empty())
        return s;
    std::string out;
    out.reserve(s.size() + 4);
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (i > 0)
        {
            char prev = s[i - 1];
            char next = (i + 1 < s.size()) ? s[i + 1] : '\0';

            bool lowerToUpper = (islower((unsigned char)prev) && isupper((unsigned char)c));
            bool letterToDigit = (isalpha((unsigned char)prev) && isdigit((unsigned char)c));
            bool acronymToWord = (isupper((unsigned char)prev) && isupper((unsigned char)c) && islower((unsigned char)next));

            if (lowerToUpper || letterToDigit || acronymToWord)
            {
                out += ' ';
            }
        }
        out += c;
    }
    return out;
}

std::string formatModuleName(const std::string& name, int charStyle)
{
    switch (charStyle)
    {
    case 0: // lowercase
    {
        std::string res = name;
        for (char& c : res)
            c = (char)tolower((unsigned char)c);
        return res;
    }
    case 1: // lowercase spaced
    {
        std::string res = separateWords(name);
        for (char& c : res)
            c = (char)tolower((unsigned char)c);
        return res;
    }
    case 2: // Normalcase
        return name;
    case 3: // Normalcase separated
        return separateWords(name);
    default:
        return name;
    }
}

std::string formatSuffix(const std::string& sfx, int charStyle)
{
    if (sfx.empty())
        return sfx;
    if (charStyle == 0 || charStyle == 1)
    {
        std::string res = sfx;
        for (char& c : res)
            c = (char)tolower((unsigned char)c);
        return res;
    }
    return sfx;
}

// Glow por anillos concentricos (sustituto del AddShadowRect de Solstice):
// cada capa es un AddRect OUTLINE con caida cubica, asi el interior queda
// limpio como el shadow real (un rect lleno dejaria una mancha solida
// detras del texto). `inner` es el margen limpio alrededor del rect.
void glowRect(ImDrawList* d, const ImVec2& min, const ImVec2& max,
              ImU32 rgb, float strength, float spread, float inner)
{
    const int layers = 9;
    float step = spread / (float)(layers - 1);
    for (int i = 0; i < layers; ++i)
    {
        float t = (float)i / (float)(layers - 1);
        float pad = inner + step * i;
        float f = 1.0f - t;
        f = f * f * f;
        int a = (int)(strength * f);
        if (a <= 0)
            continue;
        // Anillo con solape para que no queden seams entre capas.
        float thick = step + 1.5f;
        d->AddRect(ImVec2(min.x - pad - thick * 0.5f, min.y - pad - thick * 0.5f),
                   ImVec2(max.x + pad + thick * 0.5f, max.y + pad + thick * 0.5f),
                   (rgb & 0x00FFFFFFu) | ((unsigned)a << 24), 0.0f, 0, thick);
    }
}

} // namespace

// HSV -> RGB inline (ImGui no expone ColorHSV como funcion publica)
static ImVec4 hsvToRgb(float h, float s, float v)
{
    int hi = (int)(h * 6.0f) % 6;
    float f = h * 6.0f - (float)(int)(h * 6.0f);
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (hi)
    {
    case 0: return ImVec4(v, t, p, 1.0f);
    case 1: return ImVec4(q, v, p, 1.0f);
    case 2: return ImVec4(p, v, t, 1.0f);
    case 3: return ImVec4(p, q, v, 1.0f);
    case 4: return ImVec4(t, p, v, 1.0f);
    default: return ImVec4(v, p, q, 1.0f);
    }
}

ImVec4 ArrayList::rowColor(size_t idx, size_t total)
{
    if (total <= 1)
        return theme::Accent;
    float hueShift = (float)idx / (float)(total - 1);
    float h = 0.55f + hueShift * 0.25f;
    return hsvToRgb(h, 0.72f, 0.95f);
}

// Color por fila estilo ColorUtils de Solstice: la fase avanza con el
// tiempo y se desplaza con la Y (index = y*2). Usa la paleta global del
// proyecto (Framework/ColorUtils, configurada desde el modulo Interface).
// En el estilo Rise se usa el gradiente animado del tema seleccionado
// (Themes.getAccentColor -> getBlendFactor segun la Y de la fila).
ImVec4 ArrayList::solsticeColor(float y) const
{
    if (gui::style() == 2)
        return theme::AccentAt4(0.0f, y);
    return ImGui::ColorConvertU32ToFloat4(colors::getThemedColor(y * 2.0f, 1.0f));
}

ArrayList::ArrayList()
    : Module("ArrayList", "On-screen list of enabled modules", Category::Visuals, 0)
{
    markHasSettings();
    setEnabled(true);
}

void ArrayList::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    ULONGLONG now = GetTickCount64();
    ImDrawList* d = ImGui::GetForegroundDrawList();
    auto& states = rowStates();

    // --- Actualizar tweens de todos los modulos ---
    std::vector<const Module*> dead;
    for (auto& up : ModuleManager::get().all())
    {
        const Module* m = up.get();
        auto it = states.find(m);
        if (it == states.end())
        {
            it = states.emplace(m, RowState{}).first;
            RowState& st = it->second;
            // La copia vendoreada tiene el miembro por valor: la factory es
            // segura (el original colgaba la referencia).
            st.tween = std::make_unique<ImTween<float>>(
                ImTween<float>::Tween(std::make_tuple(0.0f, 1.0f, &st.anim)));
            st.tween->OfType(ImTween<float>::PingPong).When([m] { return m->enabled(); });
        }

        RowState& st = it->second;
        // Velocidad escalada por dt (independiente de los FPS del juego).
        st.tween->Speed(0.09f * io.DeltaTime * 60.0f);
        st.tween->Tick();

        if (!m->enabled() && st.anim <= 0.0f)
            dead.push_back(m);
    }
    for (const Module* m : dead)
        states.erase(m);

    // --- Filas visibles: anim > 0, ordenadas por ancho (las mas anchas arriba) ---
    ImFont* hudFont = dx11::hudFont();
    ImFont* riseF = dx11::riseFont();
    bool usingHud = (style_ == 2) && solFont_ && hudFont != nullptr;
    bool usingRise = (style_ == 3) && riseF != nullptr;
    ImFont* font = usingHud ? hudFont : (usingRise ? riseF : ImGui::GetFont());
    float fs = (style_ == 2) ? fontSize_
                              : ((style_ == 3) ? riseFontSize_ : ImGui::GetFontSize());

    struct Row
    {
        const Module* m;
        std::string nameStr;
        std::string sfxStr;
        float nameW;
        float sfxW;
        float w;
        float h;
        float eased;
    };
    std::vector<Row> rows;
    rows.reserve(states.size());
    for (auto& [m, st] : states)
    {
        if (st.anim <= 0.001f)
            continue;

        std::string nameStr = formatModuleName(m->name(), charStyle_);
        std::string sfxStr = suffixes_ ? formatSuffix(m->suffix(), charStyle_) : "";
        std::string nameWithSpace = sfxStr.empty() ? nameStr : (nameStr + " ");

        ImVec2 tsName = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, nameWithSpace.c_str());
        ImVec2 tsSfx = sfxStr.empty() ? ImVec2(0.0f, 0.0f) : font->CalcTextSizeA(fs, FLT_MAX, 0.0f, sfxStr.c_str());

        float totalW = tsName.x + tsSfx.x;
        float totalH = std::max(tsName.y, tsSfx.y);

        rows.push_back({m, nameWithSpace, sfxStr, tsName.x, tsSfx.x, totalW, totalH + 7.0f, easeOutCubic(st.anim)});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.w > b.w; });

    if (rows.empty())
        return;

    if (style_ == 2)
    {
        // =============== MODO SOLSTICE ===============
        // Puerto del Arraylist de Solstice: texto right-aligned (cada fila
        // con su propio ancho), slide-in desde la derecha, color fluido por
        // fila, glow, sombra y las decoraciones Outline/Bar/Split.
        bool menuOpen = menu::visible();
        ImVec2 mouse = io.MousePos;
        bool hasBlur = solBlur_ && blur::wanted() && blur::ready() && blur::srv();
        float pad7 = (displayMode_ == 1 || displayMode_ == 2) ? 7.0f : 0.0f;
        float anchor = sw - 10.0f - pad7;

        struct SRow
        {
            const Module* m;
            std::string nameStr;
            std::string sfxStr;
            float nameW;
            float sfxW;
            float a;      // anim eased
            ImVec2 pos;   // posicion del texto (con slide)
            ImVec2 ts;    // tamano del texto
            ImVec4 rect;  // fondo (x, y, z, w)
            ImVec4 bg;    // rect de lineas (con el pad de Bar/Split)
            ImVec4 col;
            bool hovered;
        };
        std::vector<SRow> lrows;

        float y = 5.0f;
        for (const Row& row : rows)
        {
            if (row.m == this)
                continue; // no auto-listarse (el mVisibleInArrayList de Solstice)
            if (visIdx_ == 1 && row.m->keybind() == 0)
                continue;

            ImVec2 ts(row.w, row.h - 7.0f);
            float endX = anchor - ts.x;
            float x = sw + 14.0f + (endX - (sw + 14.0f)) * row.eased;

            SRow sr;
            sr.m = row.m;
            sr.nameStr = row.nameStr;
            sr.sfxStr = row.sfxStr;
            sr.nameW = row.nameW;
            sr.sfxW = row.sfxW;
            sr.a = row.eased;
            sr.ts = ts;
            sr.pos = ImVec2(x, y);
            sr.rect = ImVec4(x - 3.0f, y, x + ts.x + 4.0f, y + ts.y);
            sr.bg = ImVec4(sr.rect.x + pad7, sr.rect.y, sr.rect.z + pad7, sr.rect.w);
            sr.col = solsticeColor(y);
            sr.hovered = mouse.x >= sr.rect.x && mouse.x <= sr.rect.z &&
                         mouse.y >= sr.rect.y && mouse.y <= sr.rect.w;
            lrows.push_back(sr);

            y += ts.y * row.eased;
        }

        if (lrows.empty())
            return;

        auto colU32 = [](const SRow& sr, float alpha) {
            return ImGui::ColorConvertFloat4ToU32(
                ImVec4(sr.col.x, sr.col.y, sr.col.z, alpha));
        };

        // PASADA 1: sombra de fondo, blur, glow del texto y barras Split.
        for (const SRow& sr : lrows)
        {
            ImU32 colRgb = colU32(sr, 0.0f) & 0x00FFFFFFu;

            if (solShadow_ && displayMode_ != 0)
                glowRect(d, ImVec2(sr.rect.x, sr.rect.y), ImVec2(sr.rect.z, sr.rect.w),
                         IM_COL32(0, 0, 0, 0), 150.0f * solShadowOpacity_ * sr.a,
                         26.0f, 4.0f);

            if (hasBlur)
            {
                ImVec2 uv0(sr.rect.x / sw, sr.rect.y / sh);
                ImVec2 uv1(sr.rect.z / sw, sr.rect.w / sh);
                d->AddImageRounded(ImTextureRef((void*)blur::srv()),
                                   ImVec2(sr.rect.x, sr.rect.y),
                                   ImVec2(sr.rect.z, sr.rect.w),
                                   uv0, uv1, IM_COL32(255, 255, 255, (int)(255.0f * sr.a)), 0.0f);
                d->AddRectFilled(ImVec2(sr.rect.x, sr.rect.y),
                                 ImVec2(sr.rect.z, sr.rect.w),
                                 IM_COL32(16, 20, 28, (int)(70.0f * sr.a)), 0.0f);
            }

            if (glow_ && displayMode_ != 1 && displayMode_ != 2)
                glowRect(d, sr.pos, ImVec2(sr.pos.x + sr.ts.x, sr.pos.y + sr.ts.y),
                         colRgb, 205.0f * glowStrength_ * sr.a, 18.0f, 3.0f);

            if (displayMode_ == 2)
            {
                // Split: barra de color a la derecha del texto.
                ImVec2 bMin(sr.rect.z + 2.0f, sr.pos.y + 4.0f);
                ImVec2 bMax(bMin.x + 4.0f, sr.pos.y + sr.ts.y - 6.0f);
                d->AddRectFilled(bMin, bMax, colU32(sr, sr.a), 3.0f);
                if (glow_)
                    glowRect(d, bMin, bMax, colRgb, 160.0f * glowStrength_ * sr.a, 10.0f, 1.0f);
            }
        }

        // PASADA 2: relleno del Outline, texto, hover y click-para-toggle.
        for (SRow& sr : lrows)
        {
            if (displayMode_ == 0)
            {
                // mBackgroundValue: 0 = negro, 1 = el color de la fila.
                d->AddRectFilled(ImVec2(sr.bg.x, sr.bg.y), ImVec2(sr.bg.z, sr.bg.w),
                                 ImGui::ColorConvertFloat4ToU32(ImVec4(
                                     sr.col.x * solBgValue_,
                                     sr.col.y * solBgValue_,
                                     sr.col.z * solBgValue_,
                                     0.4f * sr.a)));
            }

            ImU32 textCol = colU32(sr, sr.a);
            ImU32 sfxCol = IM_COL32(165, 170, 180, (int)(255.0f * sr.a));

            d->AddText(font, fs, sr.pos, textCol, sr.nameStr.c_str());
            if (solBold_ && !usingHud)
            {
                // Bold por doble pasada con offset sub-pixel (solo para el
                // font default; el hud font ya es bold).
                d->AddText(font, fs, ImVec2(sr.pos.x + 0.7f, sr.pos.y), textCol,
                           sr.nameStr.c_str());
            }

            if (!sr.sfxStr.empty())
            {
                ImVec2 sfxPos(sr.pos.x + sr.nameW, sr.pos.y);
                d->AddText(font, fs, sfxPos, sfxCol, sr.sfxStr.c_str());
                if (solBold_ && !usingHud)
                {
                    d->AddText(font, fs, ImVec2(sfxPos.x + 0.7f, sfxPos.y), sfxCol,
                               sr.sfxStr.c_str());
                }
            }

            if (sr.hovered)
            {
                d->AddRectFilled(ImVec2(sr.rect.x, sr.rect.y),
                                 ImVec2(sr.rect.z, sr.rect.w),
                                 IM_COL32(255, 255, 255, (int)(25.0f * sr.a)));
                if (!menuOpen && ImGui::IsMouseClicked(0))
                {
                    if (Module* mod = ModuleManager::get().find(sr.m->name()))
                        mod->toggle();
                }
            }
        }

        // PASADA 3 (Bar): glow sobre el borde derecho del texto.
        if (glow_ && displayMode_ == 1)
        {
            for (const SRow& sr : lrows)
            {
                glowRect(d, ImVec2(sr.pos.x + sr.ts.x, sr.pos.y),
                         ImVec2(sr.pos.x + sr.ts.x, sr.pos.y + sr.ts.y),
                         colU32(sr, 0.0f) & 0x00FFFFFFu,
                         175.0f * glowStrength_ * sr.a, 12.0f, 0.0f);
            }
        }

        // PASADA 4: lineas. Outline = escalera alrededor de todo el bloque
        // (2px), Bar = linea vertical al borde derecho de cada fila (4px).
        if (displayMode_ == 0 || displayMode_ == 1)
        {
            struct LineSeg
            {
                ImVec2 a, b;
                ImU32 col;
            };
            std::vector<LineSeg> lines;

            size_t n = lrows.size();
            float firstX0 = 0.0f;
            for (size_t i = 0; i < n; ++i)
            {
                // Solstice dibuja las lineas 2px a la izquierda de los rects.
                float x0 = lrows[i].bg.x - 2.0f;
                float x1 = lrows[i].bg.z - 2.0f;
                float y0 = lrows[i].bg.y;
                float y1 = lrows[i].bg.w;
                ImU32 col = colU32(lrows[i], lrows[i].a);

                // "next": el rect mas angosto de las filas de abajo.
                bool hasNext = (i + 1) < n;
                float nextX0 = FLT_MAX;
                for (size_t j = i + 1; j < n; ++j)
                {
                    float nx = lrows[j].bg.x - 2.0f;
                    if (nx < nextX0)
                        nextX0 = nx;
                }

                if (displayMode_ == 0)
                {
                    lines.push_back({ImVec2(x1 + 2.0f, y0), ImVec2(x1 + 2.0f, y1), col}); // derecha
                    if (!hasNext || nextX0 >= x0)
                        lines.push_back({ImVec2(x0, y0), ImVec2(x0, y1), col});          // izquierda
                    if (i == 0)
                    {
                        firstX0 = x0;
                        lines.push_back({ImVec2(x0, y0), ImVec2(x1 + 2.0f, y0), col});    // tope
                    }
                    if (!hasNext)
                        lines.push_back({ImVec2(x0, y1), ImVec2(x1 + 2.0f, y1), col});   // base
                    else if (nextX0 >= x0 && nextX0 - x0 > 2.0f)
                        lines.push_back({ImVec2(x0, y1), ImVec2(nextX0 - 1.0f, y1), col}); // base parcial
                }
                else
                {
                    lines.push_back({ImVec2(x1, y0), ImVec2(x1, y1), col}); // Bar
                }
            }

            // Cierre de la esquina superior izquierda: desde el rect mas
            // angosto hasta el tope de la primera fila.
            if (displayMode_ == 0 && n > 0)
            {
                size_t lowest = 0;
                for (size_t j = 1; j < n; ++j)
                    if (lrows[j].bg.x < lrows[lowest].bg.x)
                        lowest = j;
                float lx = lrows[lowest].bg.x - 2.0f;
                float ly = lrows[lowest].bg.y;
                lines.push_back({ImVec2(lx, ly), ImVec2(firstX0 + 2.0f, ly),
                                 colU32(lrows[lowest], lrows[lowest].a)});
            }

            float thick = (displayMode_ == 0) ? 2.0f : 4.0f;
            for (const LineSeg& l : lines)
                d->AddLine(l.a, l.b, l.col, thick);
        }
        return;
    }

    if (style_ == 3)
    {
        // =============== MODO RISE (puerto de ModernInterface.java) ===============
        // Filas alineadas a la derecha, ordenadas por ancho (mas anchas arriba),
        // texto Product Sans con sombra en el color ANIMADO del tema, fondo
        // oscuro (0,0,0,150) y barra de acento 2x9 en el borde derecho.
        float textH = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "Ag").y;
        float spacing = textH + 1.0f;       // moduleSpacing de Rise (font.height())
        const float edge = 10.0f;           // edgeOffset
        const float d2 = 2.0f;

        bool menuOpen = menu::visible();
        ImVec2 mouse = io.MousePos;
        bool hasBlur = riseBlur_ && blur::wanted() && blur::ready() && blur::srv();

        float y = edge;
        for (const Row& row : rows)
        {
            if (row.m == this)
                continue;
            if (visIdx_ == 1 && row.m->keybind() == 0)
                continue;

            float a = row.eased;
            float w = row.w;

            // Slide-in desde la derecha + colapso vertical (Rise: animationTime)
            float slideOff = (1.0f - a) * (w + 60.0f);
            float x = (sw - edge - w) + slideOff;
            float rH = spacing * a;
            float rowTop = y - 3.0f;        // fondo sube 3px (Rise: d5 - 3.0)

            // Color: default Fade = getAccentColor((0, y)) -> gradiente animado.
            // Static en Rise usa rz().rA(): para el tema Rainbow es dinamico.
            ImVec4 col;
            if (riseColorMode_ == 0)
                col = theme::AccentRainbow ? theme::rainbowAt(0.0f, 0.0f) : theme::Accent;
            else if (riseColorMode_ == 2)
                col = theme::AccentAt4(0.0f, 0.0f);         // Breathe
            else
                col = theme::AccentAt4(0.0f, y);            // Fade
            ImU32 colU32 = ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, a));

            // Fondo (Background = Normal): rect negro alpha 150 (Themes.rK)
            if (riseBg_)
            {
                ImVec2 bgMin(x - d2, rowTop);
                ImVec2 bgMax(x - d2 + w + 5.0f, rowTop + rH);
                if (hasBlur)
                {
                    ImVec2 uv0(bgMin.x / sw, bgMin.y / sh);
                    ImVec2 uv1(bgMax.x / sw, bgMax.y / sh);
                    d->AddImage(ImTextureRef((void*)blur::srv()), bgMin, bgMax, uv0, uv1,
                                IM_COL32(255, 255, 255, (int)(255.0f * a)));
                }
                d->AddRectFilled(bgMin, bgMax, IM_COL32(0, 0, 0, (int)(150.0f * a)));
            }

            // Texto con drop shadow (Rise: drawStringWithShadow)
            ImVec2 tp(x, y);
            if (riseShadow_)
            {
                d->AddText(font, fs, ImVec2(tp.x + 1.0f, tp.y + 1.0f),
                           IM_COL32(0, 0, 0, (int)(255.0f * a)), row.nameStr.c_str());
                if (!row.sfxStr.empty())
                    d->AddText(font, fs, ImVec2(tp.x + row.nameW + 4.0f, tp.y + 1.0f),
                               IM_COL32(0, 0, 0, (int)(255.0f * a)), row.sfxStr.c_str());
            }
            d->AddText(font, fs, tp, colU32, row.nameStr.c_str());
            if (!row.sfxStr.empty())
                d->AddText(font, fs, ImVec2(tp.x + row.nameW + 3.0f, tp.y),
                           IM_COL32(204, 204, 204, (int)(255.0f * a)), row.sfxStr.c_str());

            // Sidebar: barra de acento 2x9 (radio 1) a la derecha del texto
            if (riseSidebar_)
            {
                d->AddRectFilled(ImVec2(x + w + 2.0f, y - 1.5f),
                                 ImVec2(x + w + 4.0f, y - 1.5f + 9.0f), colU32, 1.0f);
            }

            // Hover + click para togglear (consistente con el resto de modos)
            if (mouse.x >= x - d2 && mouse.x <= x + w + 4.0f &&
                mouse.y >= rowTop && mouse.y <= rowTop + rH)
            {
                d->AddRectFilled(ImVec2(x - d2, rowTop), ImVec2(x + w + 4.0f, rowTop + rH),
                                 IM_COL32(255, 255, 255, (int)(18.0f * a)));
                if (!menuOpen && ImGui::IsMouseClicked(0))
                {
                    if (Module* mod = ModuleManager::get().find(row.m->name()))
                        mod->toggle();
                }
            }

            y += rH;
        }
        return;
    }

    float y = 14.0f;
    float right = sw - 12.0f;

    if (style_ == 1)
    {
        // =============== MODO TYPICAL (clientes clasicos) ===============
        // Cada fila con SU PROPIO ancho (escalera), flush (sin gap),
        // alineadas a la derecha, texto left-aligned dentro de su tira,
        // sidebar con gradiente por fila — el look Onix/Flarial/Vintage.
        float barW = barWidth_;
        float padX = 8.0f;

        // Pre-calcular el bounding box
        float totalH = 0.0f;
        float maxW = 0.0f;
        for (const Row& r : rows)
        {
            totalH += r.h * r.eased;
            if (r.w > maxW) maxW = r.w;
        }

        // Borde full: UNA linea de accent alrededor del bloque (solo si
        // el usuario lo pide con el outline mode). Sin esto, el "borde"
        // exterior es simplemente donde termina el blur contra el juego.
        if (outlineMode_ == 2)
        {
            float bgW = maxW + padX * 2.0f + barW;
            d->AddRect(ImVec2(right - bgW, y), ImVec2(right, y + totalH),
                       theme::AccentU32(0.75f), 0.0f, 0, 1.0f);
        }

        // --- PASADA 1: Sombras Suaves (Cubic Falloff) ---
        // Usamos una caida cubica real para lograr el look "difuminado" de la referencia.
        // Evitamos el bug de la derecha (negro solido por suma de 15 capas) separando
        // el renderizado: las filas solo expanden a la izquierda, y se dibuja UNA 
        // unica capa de sombra para todo el lado derecho.
        if (!rows.empty())
        {
            float spread = 14.0f;
            float strength = 130.0f;

            auto DrawShadow = [&](ImVec2 min, ImVec2 max, float a_mult, int expand) {
                const int layers = 9;
                for (int i = 0; i < layers; ++i)
                {
                    float t = (float)i / (float)(layers - 1);
                    float pad = spread * t;
                    float f = 1.0f - t;
                    f = f * f * f;
                    int a = (int)(strength * a_mult * f);
                    if (a <= 0) continue;
                    
                    ImVec2 pMin = ImVec2((expand & 1) ? min.x - pad : min.x, min.y - pad);
                    ImVec2 pMax = ImVec2((expand & 2) ? max.x + pad : max.x, max.y + pad);
                    
                    d->AddRectFilled(pMin, pMax, IM_COL32(0, 0, 0, a), pad);
                }
            };

            float yPass = y;
            for (const Row& row : rows)
            {
                float slideOff = (1.0f - row.eased) * (row.w + 60.0f);
                float rowW = row.w + padX * 2.0f + barW;
                float x1 = right + slideOff;
                float x0 = x1 - rowW;
                
                // Sombra de cada fila: expande Izquierda, Tope y Base (expand = 1)
                // NO expande a la derecha para no sumar 15 capas de sombra.
                if (row.eased > 0.05f)
                    DrawShadow(ImVec2(x0, yPass), ImVec2(x1, yPass + row.h), row.eased, 1);

                yPass += row.h * row.eased;
            }

            // Sombra unificada para el lado Derecho (expand = 2)
            // Se dibuja solo una vez para toda la altura de la lista.
            float totalH = yPass - y;
            DrawShadow(ImVec2(right - 20.0f, y), ImVec2(right, y + totalH), 1.0f, 2);
        }


        // Fondos, sidebars y texto (encima de la sombra)
        for (size_t idx = 0; idx < rows.size(); ++idx)
        {
            const Row& row = rows[idx];
            float slideOff = (1.0f - row.eased) * (row.w + 60.0f);
            float rowW = row.w + padX * 2.0f + barW;
            float x1 = right + slideOff;
            float x0 = x1 - rowW;
            ImVec2 rMin(x0, y);
            ImVec2 rMax(x1, y + row.h);

            // Fondo: blur o solido por fila
            if (row.eased > 0.01f)
            {
                if (blurBg_ && blur::wanted() && blur::ready() && blur::srv())
                {
                    ImVec2 uv0(rMin.x / sw, rMin.y / sh);
                    ImVec2 uv1(rMax.x / sw, rMax.y / sh);
                    d->AddImageRounded(ImTextureRef((void*)blur::srv()),
                                       rMin, rMax, uv0, uv1,
                                       IM_COL32(255, 255, 255, (int)(255.0f * row.eased)), 0.0f);
                    d->AddRectFilled(rMin, rMax,
                                     IM_COL32(16, 20, 28, (int)(78.0f * row.eased)), 0.0f);
                }
                else
                    d->AddRectFilled(rMin, rMax,
                                     IM_COL32(16, 18, 22, (int)(255.0f * bgOpacity_ * row.eased)));
            }

            // Sidebar: barra con el color de ESTE modulo (en Rise, el gradiente
            // animado del tema segun la Y de la fila).
            ImVec4 col = (gui::style() == 2) ? theme::AccentAt4(0.0f, y)
                                             : rowColor(idx, rows.size());
            ImU32 barCol = ImGui::ColorConvertFloat4ToU32(
                ImVec4(col.x, col.y, col.z, row.eased));

            if (outlineMode_ == 0 || outlineMode_ == 2)
                d->AddRectFilled(ImVec2(x1 - barW, y), ImVec2(x1, y + row.h), barCol);
            else if (outlineMode_ == 1)
                d->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + barW, y + row.h), barCol);

            // Texto: LEFT-ALIGNED dentro de SU propia tira
            float tx = (outlineMode_ == 1) ? x0 + barW + padX : x0 + padX;
            d->AddText(ImVec2(tx, y + 3.0f),
                       IM_COL32(240, 242, 245, (int)(255 * row.eased)),
                       row.nameStr.c_str());
            if (!row.sfxStr.empty())
            {
                d->AddText(ImVec2(tx + row.nameW, y + 3.0f),
                           IM_COL32(165, 170, 180, (int)(255 * row.eased)),
                           row.sfxStr.c_str());
            }

            y += row.h * row.eased;
        }
        return;
    }

    // =============== MODO AZYRE (acrilico con paneles) ===============
    for (const Row& row : rows)
    {
        // Desplazamiento deslizante desde la derecha + colapso vertical suave.
        float slideOff = (1.0f - row.eased) * (row.w + 40.0f);
        float x0 = right - row.w - 16.0f + slideOff;
        ImVec2 rMin(x0, y);
        ImVec2 rMax(right + (1.0f - row.eased) * (row.w + 40.0f), y + row.h);

        if (row.eased > 0.01f)
            widgets::AcrylicPanel(d, rMin, rMax, 4.0f, row.eased, sw, sh, 26.0f, 8.0f);

        d->AddRectFilled(ImVec2(rMax.x - 3.0f, y), ImVec2(rMax.x, y + row.h),
                         (gui::style() == 2) ? theme::AccentAtU32(0.0f, y, row.eased)
                                             : theme::AccentU32(row.eased), 4.0f);
        d->AddText(ImVec2(x0 + 7.0f, y + 3.0f),
                   IM_COL32(240, 242, 245, (int)(255 * row.eased)), row.nameStr.c_str());
        if (!row.sfxStr.empty())
        {
            d->AddText(ImVec2(x0 + 7.0f + row.nameW, y + 3.0f),
                       IM_COL32(165, 170, 180, (int)(255 * row.eased)),
                       row.sfxStr.c_str());
        }

        y += (row.h + 4.0f) * row.eased;
    }

    (void)now;
}

void ArrayList::drawSettings()
{
    const char* styles[] = {"Azyre", "Typical", "Solstice", "Rise"};
    widgets::CCombo("Style##al", &style_, styles, 4);

    const char* charStyles[] = {"lowercase", "lowercase spaced", "Normalcase", "Normalcase separated"};
    widgets::CCombo("Character Style##al", &charStyle_, charStyles, 4);
    widgets::Checkbox("Suffixs##al", &suffixes_);

    if (style_ == 1)
    {
        ImGui::Indent();
        const char* outlines[] = {"Barra derecha", "Barra izquierda", "Borde full", "Sin outline"};
        widgets::CCombo("Outline##al", &outlineMode_, outlines, 4);
        widgets::Checkbox("Fondo blur##al", &blurBg_);
        if (!blurBg_)
            widgets::CSlider("Opacidad fondo##al", &bgOpacity_, 0.1f, 1.0f, "%.2f");
        widgets::CSlider("Ancho barra##al", &barWidth_, 1.0f, 6.0f, "%.1f");
        widgets::CSlider("Redondeo barra##al", &barRounding_, 0.0f, 6.0f, "%.1f");
        ImGui::Unindent();
    }
    else if (style_ == 2)
    {
        ImGui::Indent();
        const char* displays[] = {"Outline", "Bar", "Split", "Ninguno"};
        widgets::CCombo("Display##al", &displayMode_, displays, 4);
        const char* vis[] = {"Todos", "Solo con keybind"};
        widgets::CCombo("Visibilidad##al", &visIdx_, vis, 2);
        ImGui::Separator();
        widgets::Checkbox("Glow##al", &glow_);
        if (glow_)
            widgets::CSlider("Fuerza glow##al", &glowStrength_, 0.2f, 1.0f, "%.2f");
        widgets::Checkbox("Texto bold##al", &solBold_);
        widgets::Checkbox("Fuente Product Sans##al", &solFont_);
        ImGui::Separator();
        const char* bgStyle[] = {"Ninguna", "Sombra"};
        int shadowMode = solShadow_ ? 1 : 0;
        if (widgets::CCombo("Sombra de fondo##al", &shadowMode, bgStyle, 2))
            solShadow_ = shadowMode == 1;
        if (solShadow_)
            widgets::CSlider("Opacidad sombra##al", &solShadowOpacity_, 0.1f, 1.0f, "%.2f");
        if (displayMode_ == 0)
            widgets::CSlider("Color del fondo##al", &solBgValue_, 0.0f, 1.0f, "%.2f");
        widgets::Checkbox("Fondo blur##al", &solBlur_);
        ImGui::Separator();
        widgets::CSlider("Tamano fuente##al", &fontSize_, 12.0f, 36.0f, "%.0f");
        ImGui::Unindent();
    }
    else if (style_ == 3)
    {
        ImGui::Indent();
        const char* colorModes[] = {"Static", "Fade", "Breathe"};
        widgets::CCombo("Color Mode##al", &riseColorMode_, colorModes, 3);
        if (riseColorMode_ == 0)
            ImGui::TextDisabled("(Static = accent plano)");
        else if (riseColorMode_ == 2)
            ImGui::TextDisabled("(Breathe = pulso global)");

        const char* bgStyle[] = {"Off", "Normal"};
        int bg = riseBg_ ? 1 : 0;
        if (widgets::CCombo("Background##al", &bg, bgStyle, 2))
            riseBg_ = bg == 1;
        if (riseBg_)
            widgets::Checkbox("Blur del fondo##al", &riseBlur_);

        widgets::Checkbox("Drop Shadow##al", &riseShadow_);
        widgets::Checkbox("Sidebar##al", &riseSidebar_);
        ImGui::Separator();
        widgets::CSlider("Tamano fuente##al", &riseFontSize_, 10.0f, 30.0f, "%.0f");
        ImGui::Unindent();
    }

    widgets::ColorEdit4("Accent##al", (float*)&theme::Accent);
}

nlohmann::json ArrayList::saveSettings() const
{
    return nlohmann::json{
        {"style", style_},
        {"charStyle", charStyle_},
        {"suffixes", suffixes_},
        {"outlineMode", outlineMode_},
        {"bgOpacity", bgOpacity_},
        {"barRounding", barRounding_},
        {"barWidth", barWidth_},
        {"blurBg", blurBg_},
        {"displayMode", displayMode_},
        {"visIdx", visIdx_},
        {"glow", glow_},
        {"glowStrength", glowStrength_},
        {"solShadow", solShadow_},
        {"solShadowOpacity", solShadowOpacity_},
        {"solBgValue", solBgValue_},
        {"solBlur", solBlur_},
        {"solBold", solBold_},
        {"solFont", solFont_},
        {"fontSize", fontSize_},
        {"riseColorMode", riseColorMode_},
        {"riseBg", riseBg_},
        {"riseShadow", riseShadow_},
        {"riseSidebar", riseSidebar_},
        {"riseBlur", riseBlur_},
        {"riseFontSize", riseFontSize_},
        {"accent", {theme::Accent.x, theme::Accent.y, theme::Accent.z, theme::Accent.w}},
    };
}

void ArrayList::loadSettings(const nlohmann::json& j)
{
    style_ = j.value("style", style_);
    if (style_ < 0 || style_ > 3)
        style_ = 0;
    charStyle_ = j.value("charStyle", charStyle_);
    if (charStyle_ < 0 || charStyle_ > 3)
        charStyle_ = 2;
    suffixes_ = j.value("suffixes", suffixes_);
    outlineMode_ = j.value("outlineMode", outlineMode_);
    bgOpacity_ = j.value("bgOpacity", bgOpacity_);
    barRounding_ = j.value("barRounding", barRounding_);
    barWidth_ = j.value("barWidth", barWidth_);
    blurBg_ = j.value("blurBg", blurBg_);
    displayMode_ = j.value("displayMode", displayMode_);
    if (displayMode_ < 0 || displayMode_ > 3)
        displayMode_ = 2;
    visIdx_ = j.value("visIdx", visIdx_);
    if (visIdx_ < 0 || visIdx_ > 1)
        visIdx_ = 0;
    glow_ = j.value("glow", glow_);
    glowStrength_ = j.value("glowStrength", glowStrength_);
    if (glowStrength_ < 0.2f)
        glowStrength_ = 0.2f;
    if (glowStrength_ > 1.0f)
        glowStrength_ = 1.0f;
    solShadow_ = j.value("solShadow", solShadow_);
    solShadowOpacity_ = j.value("solShadowOpacity", solShadowOpacity_);
    if (solShadowOpacity_ < 0.1f)
        solShadowOpacity_ = 0.1f;
    if (solShadowOpacity_ > 1.0f)
        solShadowOpacity_ = 1.0f;
    solBgValue_ = j.value("solBgValue", solBgValue_);
    if (solBgValue_ < 0.0f)
        solBgValue_ = 0.0f;
    if (solBgValue_ > 1.0f)
        solBgValue_ = 1.0f;
    solBlur_ = j.value("solBlur", solBlur_);
    solBold_ = j.value("solBold", solBold_);
    solFont_ = j.value("solFont", solFont_);
    fontSize_ = j.value("fontSize", fontSize_);
    if (fontSize_ < 12.0f)
        fontSize_ = 12.0f;
    if (fontSize_ > 36.0f)
        fontSize_ = 36.0f;
    riseColorMode_ = j.value("riseColorMode", riseColorMode_);
    if (riseColorMode_ < 0 || riseColorMode_ > 2)
        riseColorMode_ = 1;
    riseBg_ = j.value("riseBg", riseBg_);
    riseShadow_ = j.value("riseShadow", riseShadow_);
    riseSidebar_ = j.value("riseSidebar", riseSidebar_);
    riseBlur_ = j.value("riseBlur", riseBlur_);
    riseFontSize_ = j.value("riseFontSize", riseFontSize_);
    if (riseFontSize_ < 10.0f)
        riseFontSize_ = 10.0f;
    if (riseFontSize_ > 30.0f)
        riseFontSize_ = 30.0f;
    if (j.contains("accent") && j["accent"].is_array() && j["accent"].size() == 4)
    {
        theme::Accent.x = j["accent"][0].get<float>();
        theme::Accent.y = j["accent"][1].get<float>();
        theme::Accent.z = j["accent"][2].get<float>();
        theme::Accent.w = j["accent"][3].get<float>();
    }
}

MC_REGISTER_MODULE(ArrayList);

}
