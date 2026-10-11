#include "Spotify.hpp"

#include "Config/Config.hpp"
#include "Features/HudRegistry.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"
#include "Networking/Spotify/SpotifyService.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/DX11.hpp"
#include "Framework/stb_image.h"

#include <d3d11.h>
#include <imgui.h>
#include <windows.h>

#include <cstdio>
#include <string>

namespace mc {

namespace {

std::string formatTime(long long ms)
{
    if (ms < 0)
        ms = 0;
    long long s = ms / 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%lld:%02lld", s / 60, s % 60);
    return buf;
}

// Trunca a maxW con "..." al final (medida con la fuente activa).
std::string ellipsize(const std::string& text, float size, float maxW)
{
    ImFont* font = ImGui::GetFont();
    if (!font)
        return text;
    if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= maxW)
        return text;

    std::string out = text;
    const char* dots = "...";
    float dotsW = font->CalcTextSizeA(size, FLT_MAX, 0.0f, dots).x;
    while (!out.empty())
    {
        out.pop_back();
        // no cortar secuencias UTF-8 a la mitad
        while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80)
            out.pop_back();
        if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, out.c_str()).x + dotsW <= maxW)
            break;
    }
    return out + dots;
}

} // namespace

Spotify::Spotify()
    : Module("Spotify", "Now playing widget for Spotify (localhost:8888/callback)",
             Category::Visuals, 0)
{
    markHasSettings();
}

void Spotify::onEnable()
{
    if (!credsLoaded_)
    {
        credsLoaded_ = true;
        spotify::loadCredentials();
        std::string id, secret;
        spotify::getCredentials(id, secret);
        snprintf(idBuf_, sizeof(idBuf_), "%s", id.c_str());
        snprintf(secretBuf_, sizeof(secretBuf_), "%s", secret.c_str());
    }

    spotify::setPollIntervalMs(pollMs_);
    spotify::startServer(); // idempotente: garantiza el socket :8888
    spotify::startPolling();

    if (!spotify::hasCredentials())
        ModuleManager::get().notifyInfo("Spotify: set Client ID/Secret in settings");
    else if (!spotify::authorized())
        spotify::startAuthFlow();
}

void Spotify::onDisable()
{
    spotify::stopPolling();
}

void Spotify::onShutdown()
{
    spotify::stopPolling();
    releaseArtTexture();
}

void Spotify::releaseArtTexture()
{
    if (artSrv_)
    {
        ((ID3D11ShaderResourceView*)artSrv_)->Release();
        artSrv_ = nullptr;
    }
    artUrl_.clear();
}

void Spotify::ensureArtTexture()
{
    if (!pendingReady_)
        return;

    pendingReady_ = false;
    std::string url = pendingUrl_;
    std::vector<unsigned char> bytes = std::move(pendingBytes_);
    pendingUrl_.clear();
    pendingBytes_.clear();

    if (url.empty() || bytes.empty() || url == artUrl_)
        return;

    ID3D11Device* dev = (ID3D11Device*)dx11::d3dDevice();
    if (!dev)
        return; // sin device todavia: la proxima imagen pendiente llegara sola

    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
    if (!px || w <= 0 || h <= 0)
    {
        MC_LOG_ERROR("[Spotify] artwork decode failed");
        return;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = px;
    init.SysMemPitch = (UINT)(w * 4);

    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&desc, &init, &tex)) && tex)
    {
        if (SUCCEEDED(dev->CreateShaderResourceView(tex, nullptr, &view)) && view)
        {
            releaseArtTexture();
            artSrv_ = view;
            artUrl_ = url;
        }
        tex->Release();
    }
    stbi_image_free(px);
}

void Spotify::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 1.0f || io.DisplaySize.y <= 1.0f)
        return;

    // Consumir caratula descargada por el poller y subirla a textura.
    std::string url;
    std::vector<unsigned char> bytes;
    if (spotify::takeArtwork(url, bytes))
    {
        pendingUrl_ = url;
        pendingBytes_ = std::move(bytes);
    }
    ensureArtTexture();

    spotify::Track t = spotify::track();
    float dt = io.DeltaTime;

    float target = t.valid ? 1.0f : 0.0f;
    anim_ += (target - anim_) * clampf(dt * 8.0f, 0.0f, 1.0f);
    if (anim_ < 0.001f)
        anim_ = 0.0f;
    if (!t.valid && anim_ <= 0.001f && !menu::hudEditing())
        return;

    float s = scale_;
    float pad = 10.0f * s;
    float art = 56.0f * s;
    float textW = 190.0f * s;
    float barH = 5.0f * s;
    float timeH = 14.0f * s;

    // Altura: la columna de texto manda; la caratula (56px) no la supera.
    float textColH = 19.0f * s + 17.0f * s + (showAlbum_ ? 16.0f * s : 0.0f) +
                     (showProgress_ ? 4.0f * s + timeH + 5.0f * s + barH : 0.0f);
    float bodyH = (showArtwork_ && artSrv_ && art > textColH) ? art : textColH;
    float h = bodyH + pad * 2.0f;
    float w = pad + (showArtwork_ && artSrv_ ? art + 10.0f * s : 0.0f) + textW + pad;

    // Escala animada de entrada.
    float a = widgets::EaseOutExpo(anim_);
    float dw = w * (0.94f + 0.06f * a);
    float dh = h * (0.94f + 0.06f * a);

    hud::Registry::get().setConfigPath(config::baseDir() + "\\hud.json");
    ImVec2 p0 = hud::Registry::get().report("Spotify", ImVec2(24.0f, 120.0f),
                                            ImVec2(w, h));
    ImVec2 center(p0.x + w * 0.5f, p0.y + h * 0.5f);
    ImVec2 boxMin(center.x - dw * 0.5f, center.y - dh * 0.5f);
    ImVec2 boxMax(boxMin.x + dw, boxMin.y + dh);

    ImDrawList* d = ImGui::GetForegroundDrawList();
    float alpha = a;
    if (menu::hudEditing())
        alpha = 1.0f;

    bool hasBlur = blur::wanted() && blur::ready() && blur::srv();
    float rnd = 10.0f * s;

    widgets::SoftShadow(d, boxMin, boxMax, rnd, 26.0f * alpha, 9.0f);
    if (hasBlur)
    {
        ImVec2 uv0(boxMin.x / io.DisplaySize.x, boxMin.y / io.DisplaySize.y);
        ImVec2 uv1(boxMax.x / io.DisplaySize.x, boxMax.y / io.DisplaySize.y);
        d->AddImageRounded(ImTextureRef((void*)blur::srv()), boxMin, boxMax, uv0, uv1,
                           IM_COL32(255, 255, 255, (int)(255.0f * alpha)), rnd);
        d->AddRectFilled(boxMin, boxMax, IM_COL32(12, 14, 18, (int)(70.0f * alpha)), rnd);
    }
    else
    {
        d->AddRectFilled(boxMin, boxMax, IM_COL32(16, 18, 22, (int)(232.0f * alpha)), rnd);
    }
    d->AddRect(boxMin, boxMax, IM_COL32(255, 255, 255, (int)(18.0f * alpha)), rnd, 0, 1.0f);

    // Barra de acento izquierda (icono verde Spotify).
    d->AddRectFilled(boxMin, ImVec2(boxMin.x + 3.0f * s, boxMax.y),
                     IM_COL32(30, 215, 96, (int)(255.0f * alpha)), 0.0f);

    ImFont* font = ImGui::GetFont();
    float x = boxMin.x + pad + 3.0f * s;
    float right = boxMax.x - pad;

    // Caratula
    bool showArt = showArtwork_ && artSrv_;
    if (showArt)
    {
        ImVec2 aMin(x, boxMin.y + pad);
        ImVec2 aMax(x + art, boxMin.y + pad + art);
        d->AddImageRounded(ImTextureRef(artSrv_), aMin, aMax, ImVec2(0, 0), ImVec2(1, 1),
                           IM_COL32(255, 255, 255, (int)(255.0f * alpha)), 6.0f * s);
        x += art + 10.0f * s;
    }

    // Columna de texto
    float textMaxW = right - x;
    float y = boxMin.y + pad;

    if (!t.valid)
    {
        d->AddText(font, 15.0f * s, ImVec2(x, y + 4.0f * s),
                   IM_COL32(220, 224, 230, (int)(200.0f * alpha)), "Spotify");
        d->AddText(font, 13.0f * s, ImVec2(x, y + 24.0f * s),
                   IM_COL32(160, 166, 176, (int)(180.0f * alpha)), "Nothing playing");
        return;
    }

    std::string title = ellipsize(t.title.empty() ? "Unknown track" : t.title,
                                  15.0f * s, textMaxW);
    d->AddText(font, 15.0f * s, ImVec2(x, y), IM_COL32(245, 246, 248,
               (int)(255.0f * alpha)), title.c_str());
    y += 19.0f * s;

    std::string artists = ellipsize(t.artists, 13.0f * s, textMaxW);
    d->AddText(font, 13.0f * s, ImVec2(x, y),
               IM_COL32(176, 182, 194, (int)(230.0f * alpha)), artists.c_str());
    y += 17.0f * s;

    if (showAlbum_ && !t.album.empty())
    {
        std::string album = ellipsize(t.album, 12.0f * s, textMaxW);
        d->AddText(font, 12.0f * s, ImVec2(x, y),
                   IM_COL32(140, 146, 158, (int)(210.0f * alpha)), album.c_str());
        y += 16.0f * s;
    }

    // Barra de progreso + tiempos (extrapolados entre polls).
    if (showProgress_)
    {
        y += 4.0f * s;
        long long prog = t.progressMs;
        if (t.playing && t.progressAtTick > 0)
        {
            unsigned long long elapsed = GetTickCount64() - t.progressAtTick;
            prog += (long long)elapsed;
        }
        if (t.durationMs > 0 && prog > t.durationMs)
            prog = t.durationMs;
        if (prog < 0)
            prog = 0;

        float frac = (t.durationMs > 0) ? (float)((double)prog / (double)t.durationMs) : 0.0f;
        frac = clampf(frac, 0.0f, 1.0f);

        std::string elapsed = formatTime(prog);
        std::string total = formatTime(t.durationMs);

        float ts = 11.0f * s;
        float timeY = y;
        d->AddText(font, ts, ImVec2(x, timeY),
                   IM_COL32(150, 156, 168, (int)(220.0f * alpha)), elapsed.c_str());
        ImVec2 totalSz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, total.c_str());
        d->AddText(font, ts, ImVec2(right - totalSz.x, timeY),
                   IM_COL32(150, 156, 168, (int)(220.0f * alpha)), total.c_str());
        y += ts + 5.0f * s;

        ImVec2 barMin(x, y);
        ImVec2 barMax(right, y + barH);
        d->AddRectFilled(barMin, barMax, IM_COL32(255, 255, 255, (int)(26.0f * alpha)),
                         barH * 0.5f);
        if (frac > 0.001f)
        {
            ImVec2 fillMax(barMin.x + (barMax.x - barMin.x) * frac, barMax.y);
            d->AddRectFilled(barMin, fillMax,
                             IM_COL32(30, 215, 96, (int)(255.0f * alpha)), barH * 0.5f);
        }
    }
}

void Spotify::drawSettings()
{
    if (!credsLoaded_)
    {
        credsLoaded_ = true;
        spotify::loadCredentials();
        std::string id, secret;
        spotify::getCredentials(id, secret);
        snprintf(idBuf_, sizeof(idBuf_), "%s", id.c_str());
        snprintf(secretBuf_, sizeof(secretBuf_), "%s", secret.c_str());
    }

    ImGui::TextDisabled("Spotify Developer Dashboard credentials");
    bool idCh = widgets::InputText("Client ID##sp", idBuf_, sizeof(idBuf_));
    bool secCh = widgets::InputText("Client Secret##sp", secretBuf_, sizeof(secretBuf_));
    if (idCh || secCh)
        spotify::setCredentials(idBuf_, secretBuf_);

    if (ImGui::Button("Connect##sp"))
    {
        if (spotify::hasCredentials())
        {
            if (spotify::startServer())
                spotify::startAuthFlow();
            else
                ModuleManager::get().notifyInfo(spotify::lastError());
        }
        else
            ModuleManager::get().notifyInfo("Spotify: set Client ID and Secret first");
    }
    ImGui::SameLine();
    if (ImGui::Button("Disconnect##sp"))
        spotify::clearTokens();

    // Estado
    switch (spotify::status())
    {
    case spotify::Status::NoCredentials:
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "Status: no credentials");
        break;
    case spotify::Status::WaitingAuth:
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.35f, 1.0f),
                           "Status: waiting for authorization...");
        break;
    case spotify::Status::Connected:
        ImGui::TextColored(ImVec4(0.20f, 0.85f, 0.45f, 1.0f), "Status: connected");
        break;
    }
    if (!spotify::lastError().empty())
        ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.40f, 1.0f), "%s",
                           spotify::lastError().c_str());
    ImGui::TextDisabled("Redirect URI: http://localhost:8888/callback");

    ImGui::Separator();

    widgets::CSlider("Scale##sp", &scale_, 0.6f, 1.8f, "%.2f");
    widgets::Checkbox("Progress bar##sp", &showProgress_);
    widgets::Checkbox("Album art##sp", &showArtwork_);
    widgets::Checkbox("Album name##sp", &showAlbum_);

    if (widgets::CSlider("Update interval (ms)##sp", &pollMs_, 250, 10000, "%d"))
        spotify::setPollIntervalMs(pollMs_);
}

MC_REGISTER_MODULE(Spotify);

}
