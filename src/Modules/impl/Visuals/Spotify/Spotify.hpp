#pragma once

#include "Modules/Module.hpp"

#include <string>
#include <vector>

namespace mc {

// Spotify — widget HUD que muestra la sesión publicada por Windows GSMTC.
class Spotify : public Module
{
public:
    Spotify();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"scale", scale_},
            {"showProgress", showProgress_},
            {"showArtwork", showArtwork_},
            {"showAlbum", showAlbum_},
            {"pollMs", pollMs_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        scale_ = j.value("scale", scale_);
        if (scale_ < 0.6f)
            scale_ = 0.6f;
        if (scale_ > 1.8f)
            scale_ = 1.8f;
        showProgress_ = j.value("showProgress", showProgress_);
        showArtwork_ = j.value("showArtwork", showArtwork_);
        showAlbum_ = j.value("showAlbum", showAlbum_);
        pollMs_ = j.value("pollMs", pollMs_);
        if (pollMs_ < 250)
            pollMs_ = 250;
        if (pollMs_ > 10000)
            pollMs_ = 10000;
    }

private:
    void ensureArtTexture();
    void releaseArtTexture();

    // Settings
    float scale_ = 1.0f;
    bool showProgress_ = true;
    bool showArtwork_ = true;
    bool showAlbum_ = true;
    int pollMs_ = 1000;

    // textura de la caratula (hilo de render)
    void* artSrv_ = nullptr;
    std::string artUrl_;
    std::string pendingUrl_;
    std::vector<unsigned char> pendingBytes_;
    bool pendingReady_ = false;

    // animacion de entrada/salida
    float anim_ = 0.0f;
};

}
