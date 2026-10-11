#include "PotionHUD.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"

#include "Config/Config.hpp"
#include "Features/HudRegistry.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/EffectSprites.hpp"
#include "SDK/Effects.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mc {

namespace {

struct EffectInfo
{
    const char* name;
    bool debuff;
};

// ids 1..30, mismos nombres que el prototipo.
constexpr EffectInfo kEffects[31] = {
    {nullptr, false},
    {"Speed", false},
    {"Slowness", true},
    {"Haste", false},
    {"Mining Fatigue", true},
    {"Strength", false},
    {"Instant Health", false},
    {"Instant Damage", true},
    {"Jump Boost", false},
    {"Nausea", true},
    {"Regeneration", false},
    {"Resistance", false},
    {"Fire Resistance", false},
    {"Water Breathing", false},
    {"Invisibility", false},
    {"Blindness", true},
    {"Night Vision", false},
    {"Hunger", true},
    {"Weakness", true},
    {"Poison", true},
    {"Wither", true},
    {"Health Boost", false},
    {"Absorption", false},
    {"Saturation", false},
    {"Levitation", true},
    {"Fatal Poison", true},
    {"Conduit Power", false},
    {"Slow Falling", false},
    {"Bad Omen", true},
    {"Hero of the Village", false},
    {"Darkness", true},
};

const char* roman(int lvl)
{
    static const char* kRoman[] = {"",  "I", "II", "III", "IV",
                                   "V", "VI", "VII", "VIII", "IX", "X"};
    if (lvl < 1)
        return "";
    if (lvl > 10)
        return "X";
    return kRoman[lvl];
}

// Mismo umbral que effects::kPermanentTicks (beacons / conduit).
constexpr float kPermanentTicks = 900000.0f;

} // namespace

PotionHUD::PotionHUD()
    : Module("PotionHUD", "Animated HUD showing your active effects",
             Category::Visuals, 0)
{
    markHasSettings();
}

void PotionHUD::onDisable()
{
    cards_.clear();
}

void PotionHUD::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;
    ImVec2 disp = io.DisplaySize;
    if (disp.x <= 0.0f || disp.y <= 0.0f)
        return;

    // 1. Animacion: entrada ease y salida por fade.
    for (auto& c : cards_)
    {
        if (c.appear < 1.0f)
        {
            c.appear += dt * 4.5f;
            if (c.appear > 1.0f)
                c.appear = 1.0f;
        }
        if (c.leaving && c.exit > 0.0f)
        {
            c.exit -= dt * 3.5f;
            if (c.exit < 0.0f)
                c.exit = 0.0f;
        }
    }

    // 2. Merge con los efectos activos.
    std::vector<effects::Effect> cur = effects::active();
    for (const auto& e : cur)
    {
        Card* card = nullptr;
        for (auto& c : cards_)
        {
            if (c.id == e.id)
            {
                card = &c;
                break;
            }
        }
        if (!card)
        {
            Card c;
            c.id = e.id;
            c.amp = e.amp;
            c.ticks = (float)e.ticks;
            c.maxTicks = (float)e.ticks;
            cards_.push_back(c);
        }
        else
        {
            card->leaving = false;
            card->exit = 1.0f;
            card->ticks = (float)e.ticks;
            if (card->amp != e.amp)
            {
                card->amp = e.amp;
                card->maxTicks = (float)e.ticks;
            }
            if (card->ticks > card->maxTicks)
                card->maxTicks = card->ticks;
        }
    }
    for (auto& c : cards_)
    {
        bool found = false;
        for (const auto& e : cur)
        {
            if (e.id == c.id)
            {
                found = true;
                break;
            }
        }
        if (!found && !c.leaving)
            c.leaving = true;
    }
    cards_.erase(std::remove_if(cards_.begin(), cards_.end(),
                                [](const Card& c) { return c.leaving && c.exit <= 0.0f; }),
                 cards_.end());

    // 3. Layout + HudRegistry (arrastrable desde el HUD editor).
    float cardH = 40.0f * scale_;
    float cardW = 168.0f * scale_;
    float gap = 6.0f * scale_;
    float totalH = cards_.empty()
                       ? 0.0f
                       : cards_.size() * cardH + (cards_.size() - 1) * gap;

    hud::Registry::get().setConfigPath(config::baseDir() + "\\hud.json");
    ImVec2 defaultPos(disp.x - cardW - 24.0f, 140.0f);
    ImVec2 origin = hud::Registry::get().report("PotionHUD", defaultPos,
                                                ImVec2(cardW, totalH));
    if (cards_.empty())
        return;

    bool hasBlur = blur::wanted() && blur::ready() && blur::srv();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    float rnd = 7.0f * scale_;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();

    float y = origin.y;
    for (auto& c : cards_)
    {
        float aIn = widgets::EaseOutExpo(c.appear);
        float alpha = aIn * c.exit;
        if (alpha <= 0.001f)
            continue;
        float slide = (1.0f - aIn) * 46.0f + (1.0f - c.exit) * 46.0f;

        const EffectInfo& info = (c.id >= 1 && c.id <= 30) ? kEffects[c.id] : kEffects[0];
        if (!info.name)
            continue;

        float x = origin.x + slide;
        ImVec2 pMin(x, y);
        ImVec2 pMax(x + cardW, y + cardH);

        bool permanent = c.maxTicks >= kPermanentTicks;
        float barFrac = 1.0f;
        if (!permanent && c.maxTicks > 0.5f)
        {
            barFrac = c.ticks / c.maxTicks;
            if (barFrac < 0.0f)
                barFrac = 0.0f;
            if (barFrac > 1.0f)
                barFrac = 1.0f;
        }

        // Blink en los ultimos 5s (pulsa la barra y el tiempo).
        float pulse = 1.0f;
        if (blink_ && !permanent && c.ticks < 100.0f)
            pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf((float)ImGui::GetTime() * 10.0f));

        // Tarjeta: sombra + acrilico (o solido) + borde sutil.
        widgets::SoftShadow(dl, pMin, pMax, rnd, 26.0f * alpha, 9.0f);
        if (hasBlur)
        {
            ImVec2 uv0(pMin.x / disp.x, pMin.y / disp.y);
            ImVec2 uv1(pMax.x / disp.x, pMax.y / disp.y);
            dl->AddImageRounded(ImTextureRef((void*)blur::srv()), pMin, pMax, uv0, uv1,
                                IM_COL32(255, 255, 255, (int)(255.0f * alpha)), rnd);
            dl->AddRectFilled(pMin, pMax,
                              IM_COL32(16, 20, 28, (int)(64.0f * alpha)), rnd);
        }
        else
        {
            dl->AddRectFilled(pMin, pMax,
                              IM_COL32(16, 18, 22, (int)(225.0f * alpha)), rnd);
        }
        dl->AddRect(pMin, pMax, IM_COL32(255, 255, 255, (int)(16.0f * alpha)), rnd, 0, 1.0f);

        // Sprite del efecto (fit cuadrado preservando aspecto). Nearest
        // sampling: son 18x18 de pixel art, con linear se ve borroso.
        if (void* s = fxsprites::srv(c.id))
        {
            float slot = 26.0f * scale_;
            float wD = slot, hD = slot;
            int iw = 0, ih = 0;
            if (fxsprites::dims(c.id, iw, ih) && iw > 0 && ih > 0)
            {
                if (iw > ih)
                    hD = slot * (float)ih / (float)iw;
                else
                    wD = slot * (float)iw / (float)ih;
            }
            ImVec2 iMin(pMin.x + 7.0f * scale_ + (slot - wD) * 0.5f,
                        pMin.y + (cardH - hD) * 0.5f);
            if (pio.DrawCallback_SetSamplerNearest && pio.DrawCallback_SetSamplerLinear)
            {
                dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
                dl->AddImage(ImTextureRef(s), iMin, ImVec2(iMin.x + wD, iMin.y + hD),
                             ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                             IM_COL32(255, 255, 255, (int)(255.0f * alpha)));
                dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
            }
            else
            {
                dl->AddImage(ImTextureRef(s), iMin, ImVec2(iMin.x + wD, iMin.y + hD),
                             ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                             IM_COL32(255, 255, 255, (int)(255.0f * alpha)));
            }
        }

        // Nombre + nivel romano.
        char name[64];
        if (c.amp > 0)
            snprintf(name, sizeof(name), "%s %s", info.name, roman(c.amp + 1));
        else
            snprintf(name, sizeof(name), "%s", info.name);

        float nameSize = 15.0f * scale_;
        dl->AddText(font, nameSize,
                    ImVec2(pMin.x + 40.0f * scale_, pMin.y + 6.0f * scale_),
                    IM_COL32(245, 246, 248, (int)(235.0f * alpha)), name);

        // Tiempo restante (mm:ss o "inf" para permanentes).
        char time[16];
        if (permanent)
        {
            snprintf(time, sizeof(time), "inf");
        }
        else
        {
            int secs = (int)(c.ticks / 20.0f);
            if (secs < 0)
                secs = 0;
            snprintf(time, sizeof(time), "%d:%02d", secs / 60, secs % 60);
        }
        float tSize = 13.0f * scale_;
        ImVec2 ts = font->CalcTextSizeA(tSize, FLT_MAX, 0.0f, time);
        dl->AddText(font, tSize,
                    ImVec2(pMax.x - 7.0f * scale_ - ts.x, pMin.y + 8.0f * scale_),
                    IM_COL32(200, 204, 210, (int)(255.0f * alpha * pulse)), time);

        // Barra de progreso inferior: accent para buffs, rojo para debuffs.
        float barH = 3.0f * scale_;
        ImVec2 bMin(pMin.x + 1.0f, pMax.y - barH - 1.0f);
        ImVec2 bMax(pMax.x - 1.0f, pMax.y - 1.0f);
        dl->AddRectFilled(bMin, bMax, IM_COL32(255, 255, 255, (int)(22.0f * alpha)),
                          barH * 0.5f);
        ImU32 barCol = info.debuff
                           ? IM_COL32(255, 92, 92, (int)(255.0f * alpha * pulse))
                           : theme::AccentU32(alpha * pulse);
        dl->AddRectFilled(bMin,
                          ImVec2(bMin.x + (bMax.x - bMin.x) * barFrac, bMax.y),
                          barCol, barH * 0.5f);

        // El slot crece con la animacion: la pila se abre y se cierra sola.
        y += (cardH + gap) * alpha;
    }
}

void PotionHUD::drawSettings()
{
    widgets::CSlider("Scale##ph", &scale_, 0.7f, 1.6f, "%.2f");
    widgets::Checkbox("Blink when expiring##ph", &blink_);
}

MC_REGISTER_MODULE(PotionHUD);

}
