#include "PlayerList.hpp"

#include "Features/ECS/ECS.hpp"
#include "Framework/Math.hpp"
#include "GUI/Widgets.hpp"
#include "Networking/IRC/IRC.hpp"
#include "Render/DX11.hpp"
#include "Render/ResourceSprites.hpp"
#include "SDK/Game.hpp"
#include "resource.h"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mc {

namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

// Easing cubica.
float easeOutCubic(float t)
{
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

// Texto con gradiente horizontal por caracter.
void gradientText(ImDrawList* d, ImFont* font, float size, const ImVec2& pos,
                  const std::string& text, ImU32 ca, ImU32 cb, float alpha)
{
    if (!font || text.empty())
        return;
    ImVec4 a = ImGui::ColorConvertU32ToFloat4(ca);
    ImVec4 b = ImGui::ColorConvertU32ToFloat4(cb);
    int n = (int)text.size();
    float x = pos.x;
    for (int i = 0; i < n; ++i)
    {
        float t = (n > 1) ? (float)i / (float)(n - 1) : 0.0f;
        ImVec4 m(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                 alpha);
        ImU32 col = ImGui::ColorConvertFloat4ToU32(m);
        char ch = text[i];
        d->AddText(font, size, ImVec2(x, pos.y), col, &ch, &ch + 1);
        x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, &ch, &ch + 1).x;
    }
}

float gradientWidth(ImFont* font, float size, const std::string& text)
{
    if (!font || text.empty())
        return 0.0f;
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str(), text.c_str() + text.size()).x;
}

} // namespace

PlayerList::PlayerList()
    : Module("PlayerList", "Animated TAB player list (Azyre users highlighted)",
             Category::Visuals, 0)
{
    markHasSettings();
    setEnabled(true);
}

void PlayerList::onEnable()
{
    open_ = 0.0f;
    toggled_ = false;
    wasDown_ = false;
}

void PlayerList::onDisable()
{
    open_ = 0.0f;
}

void PlayerList::onTick()
{
    // Captura de tecla para el modo "hold/toggle".
    if (capturing_)
    {
        for (int k = 1; k < 256; ++k)
        {
            if (k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON)
                continue;
            if (GetAsyncKeyState(k) & 0x8000)
            {
                if (k == VK_ESCAPE)
                    key_ = 0;
                else
                    key_ = k;
                capturing_ = false;
                break;
            }
        }
    }
}

void PlayerList::collect(std::vector<Row>& out)
{
    out.clear();
    Game& game = Game::get();

    Vec3 lp{};
    bool havePos = false;
    if (game.localFound())
    {
        lp = game.localPlayer().pos();
        havePos = true;
    }

    auto& reg = ecs::get().registry;
    // Dedup por nombre: el ECS puede tener la misma entidad repetida (p. ej.
    // la entidad local antigua tras respawn, o dos direcciones del scanner).
    std::unordered_map<std::string, Row> byName;
    auto view = reg.view<ecs::PlayerName>();
    for (auto e : view)
    {
        const std::string& n = view.get<ecs::PlayerName>(e).value;
        if (n.empty())
            continue;

        Row r;
        r.name = n;
        r.local = reg.all_of<ecs::IsLocal>(e);
        if (auto* h = reg.try_get<ecs::Health>(e))
            r.health = h->current;
        if (havePos)
        {
            if (auto* t = reg.try_get<ecs::Transform>(e))
                r.dist = distance3D(lp, t->pos);
        }
        r.azyre = irc::isAzyreUser(n);

        std::string key = lower(n);
        auto it = byName.find(key);
        if (it == byName.end())
            byName.emplace(std::move(key), std::move(r));
        else if (r.local && !it->second.local)
            it->second = std::move(r); // preferir la entidad local
    }

    out.reserve(byName.size());
    for (auto& kv : byName)
        out.push_back(std::move(kv.second));

    std::sort(out.begin(), out.end(), [](const Row& a, const Row& b) {
        if (a.local != b.local)
            return a.local; // el jugador local primero
        return lower(a.name) < lower(b.name);
    });
}

void PlayerList::onRender()
{
    if (!Game::get().localFound())
    {
        open_ = 0.0f;
        return;
    }

    float dt = ImGui::GetIO().DeltaTime;

    bool down = (key_ > 0) && ((GetAsyncKeyState(key_) & 0x8000) != 0);
    if (toggleMode_)
    {
        if (down && !wasDown_)
            toggled_ = !toggled_;
    }
    wasDown_ = down;

    bool show = toggleMode_ ? toggled_ : down;

    // Tween por tiempo (independiente del framerate): abre en 0.22s,
    // cierra en 0.14s, con easing cubico.
    float dur = show ? 0.22f : 0.14f;
    open_ = clampf(open_ + (show ? 1.0f : -1.0f) * (dt / dur), 0.0f, 1.0f);
    if (!show && open_ <= 0.001f)
    {
        open_ = 0.0f;
        return;
    }

    std::vector<Row> rows;
    collect(rows);
    if (rows.empty())
        return;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    ImFont* font = dx11::gameFont();
    if (!font)
        font = ImGui::GetFont();
    ImDrawList* d = ImGui::GetForegroundDrawList();

    const float sc = scale_;
    const float fsize = 10.0f * sc;
    const float rowH = 12.0f * sc;
    const float head = 9.0f * sc;
    const float colW = 150.0f * sc;
    const float headerH = 22.0f * sc;
    const float pad = 8.0f * sc;

    int n = (int)rows.size();
    int maxRows = 20;
    int cols = (n + maxRows - 1) / maxRows;
    if (cols < 1)
        cols = 1;
    if (cols > 6)
        cols = 6;
    int perCol = (n + cols - 1) / cols;

    float panelW = cols * colW + pad * 2.0f;
    float panelH = headerH + perCol * rowH + pad * 2.0f;
    float x0 = sw * 0.5f - panelW * 0.5f;
    float y0 = 6.0f * sc;

    float ease = easeOutCubic(open_);
    ImVec2 mn(x0, y0);
    ImVec2 mx(x0 + panelW, y0 + panelH * ease);

    ImU32 ca = ImGui::ColorConvertFloat4ToU32(colA_);
    ImU32 cb = ImGui::ColorConvertFloat4ToU32(colB_);

    // Fondo
    if (glass_)
        widgets::AcrylicPanel(d, mn, mx, 6.0f * sc, ease, sw, sh, 26.0f * sc, 12.0f * sc);
    else
    {
        d->AddRectFilled(mn, mx, IM_COL32(10, 10, 15, (int)(205.0f * ease)), 6.0f * sc);
        d->AddRect(mn, mx, IM_COL32(255, 255, 255, (int)(26.0f * ease)), 6.0f * sc, 0, 1.0f);
    }

    // Header: "Azyre Client" + contador
    {
        std::string azyre = "Azyre";
        std::string client = " Client";
        std::string count = " - " + std::to_string(n) + (n == 1 ? " user" : " users");

        float hsize = fsize * 1.15f;
        float wA = gradientWidth(font, hsize, azyre);
        float wB = font->CalcTextSizeA(hsize, FLT_MAX, 0.0f, client.c_str(),
                                       client.c_str() + client.size()).x;
        float wC = font->CalcTextSizeA(hsize, FLT_MAX, 0.0f, count.c_str(),
                                       count.c_str() + count.size()).x;
        float total = wA + wB + (showCount_ ? wC : 0.0f);
        float hx = x0 + panelW * 0.5f - total * 0.5f;
        float hy = y0 + pad + (headerH - hsize) * 0.5f - 2.0f * sc;

        gradientText(d, font, hsize, ImVec2(hx, hy), azyre, ca, cb, ease);
        d->AddText(font, hsize, ImVec2(hx + wA, hy), IM_COL32(240, 242, 246, (int)(240 * ease)),
                   client.c_str(), client.c_str() + client.size());
        if (showCount_)
            d->AddText(font, hsize, ImVec2(hx + wA + wB, hy),
                       IM_COL32(150, 158, 172, (int)(230 * ease)), count.c_str(),
                       count.c_str() + count.size());
    }

    void* headTex = showHeads_ ? resources::srv(IDR_STEVE_HEAD) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const Row& r = rows[i];
        int col = i / perCol;
        int rowInCol = i % perCol;

        // Stagger por fila: cada jugador entra un poco despues que el anterior.
        float rt = clampf(open_ * 1.5f - (float)i * 0.02f, 0.0f, 1.0f);
        float rEase = easeOutCubic(rt);
        float offY = (1.0f - rEase) * -6.0f * sc;

        float rx = x0 + pad + col * colW;
        float ry = y0 + pad + headerH + rowInCol * rowH + offY;
        float a = ease * rEase;

        float textX = rx;
        if (headTex)
        {
            d->AddImage(ImTextureRef(headTex), ImVec2(rx, ry + (rowH - head) * 0.5f),
                        ImVec2(rx + head, ry + (rowH + head) * 0.5f), ImVec2(0, 0),
                        ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(255 * a)));
            textX = rx + head + 4.0f * sc;
        }

        float ty = ry + (rowH - fsize) * 0.5f;
        if (r.azyre)
        {
            gradientText(d, font, fsize, ImVec2(textX, ty), r.name, ca, cb, a);
        }
        else
        {
            ImU32 col = r.local ? IM_COL32(150, 235, 170, (int)(240 * a))
                                : IM_COL32(236, 239, 244, (int)(235 * a));
            d->AddText(font, fsize, ImVec2(textX, ty), col, r.name.c_str(),
                       r.name.c_str() + r.name.size());
        }

        // Salud + distancia a la derecha
        if (showHealth_)
        {
            char hb[32];
            snprintf(hb, sizeof(hb), "%d", r.health);
            float w = font->CalcTextSizeA(fsize, FLT_MAX, 0.0f, hb, hb + strlen(hb)).x;
            d->AddText(font, fsize, ImVec2(rx + colW - w - 6.0f * sc, ty),
                       IM_COL32(230, 120, 120, (int)(220 * a)), hb, hb + strlen(hb));
        }
    }
}

void PlayerList::loadSettings(const nlohmann::json& j)
{
    key_ = j.value("key", key_);
    toggleMode_ = j.value("toggleMode", toggleMode_);
    glass_ = j.value("glass", glass_);
    showHeads_ = j.value("showHeads", showHeads_);
    showHealth_ = j.value("showHealth", showHealth_);
    showCount_ = j.value("showCount", showCount_);
    scale_ = j.value("scale", scale_);
    if (j.contains("colA") && j["colA"].is_array() && j["colA"].size() == 4)
    {
        colA_.x = j["colA"][0].get<float>();
        colA_.y = j["colA"][1].get<float>();
        colA_.z = j["colA"][2].get<float>();
        colA_.w = j["colA"][3].get<float>();
    }
    if (j.contains("colB") && j["colB"].is_array() && j["colB"].size() == 4)
    {
        colB_.x = j["colB"][0].get<float>();
        colB_.y = j["colB"][1].get<float>();
        colB_.z = j["colB"][2].get<float>();
        colB_.w = j["colB"][3].get<float>();
    }
}

void PlayerList::drawSettings()
{
    const char* modes[] = {"Hold", "Toggle"};
    int m = toggleMode_ ? 1 : 0;
    if (widgets::CCombo("Mode##pl", &m, modes, 2))
        toggleMode_ = (m == 1);

    char keyLabel[32];
    if (capturing_)
        snprintf(keyLabel, sizeof(keyLabel), "Press a key...##plkey");
    else if (key_ <= 0)
        snprintf(keyLabel, sizeof(keyLabel), "Key: NONE##plkey");
    else if ((key_ >= 'A' && key_ <= 'Z') || (key_ >= '0' && key_ <= '9'))
        snprintf(keyLabel, sizeof(keyLabel), "Key: %c##plkey", (char)key_);
    else
        snprintf(keyLabel, sizeof(keyLabel), "Key: VK_%02X##plkey", key_);
    if (ImGui::Button(keyLabel))
        capturing_ = !capturing_;

    widgets::Checkbox("Show heads##pl", &showHeads_);
    widgets::Checkbox("Show health##pl", &showHealth_);
    widgets::Checkbox("Show count##pl", &showCount_);
    widgets::Checkbox("Glass background##pl", &glass_);
    widgets::CSlider("Scale##pl", &scale_, 0.6f, 2.0f, "%.2f");
    widgets::ColorEdit4("Azyre color A##pl", (float*)&colA_);
    widgets::ColorEdit4("Azyre color B##pl", (float*)&colB_);
    ImGui::TextDisabled("Users detected via IRC get the gradient name.");
}

MC_REGISTER_MODULE(PlayerList);

}
