#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Keystrokes : public Module
{
public:
    Keystrokes();

    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"scale", scale_},
            {"keySize", keySize_},
            {"gap", gap_},
            {"rounding", rounding_},
            {"rainbow", rainbow_},
            {"showSpace", showSpace_},
            {"showMouse", showMouse_},
            {"animSpeed", animSpeed_},
            {"keyOpacity", keyOpacity_},
            {"pressOpacity", pressOpacity_},
            {"textOpacity", textOpacity_},
            {"posX", posX_},
            {"posY", posY_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        scale_ = j.value("scale", scale_);
        keySize_ = j.value("keySize", keySize_);
        gap_ = j.value("gap", gap_);
        rounding_ = j.value("rounding", rounding_);
        rainbow_ = j.value("rainbow", rainbow_);
        showSpace_ = j.value("showSpace", showSpace_);
        showMouse_ = j.value("showMouse", showMouse_);
        animSpeed_ = j.value("animSpeed", animSpeed_);
        keyOpacity_ = j.value("keyOpacity", keyOpacity_);
        pressOpacity_ = j.value("pressOpacity", pressOpacity_);
        textOpacity_ = j.value("textOpacity", textOpacity_);
        posX_ = j.value("posX", posX_);
        posY_ = j.value("posY", posY_);
    }

private:
    // Layout
    float scale_ = 1.0f;
    float keySize_ = 40.0f;
    float gap_ = 5.0f;
    float rounding_ = 8.0f;
    float posX_ = 20.0f;
    float posY_ = -20.0f; // negativo = desde abajo

    // Visual
    bool rainbow_ = false;
    float keyOpacity_ = 0.55f;
    float pressOpacity_ = 0.78f;
    float textOpacity_ = 0.88f;
    float animSpeed_ = 12.0f;

    // Teclas
    bool showSpace_ = true;
    bool showMouse_ = false;

    // Animaciones
    float animW_ = 0.0f;
    float animA_ = 0.0f;
    float animS_ = 0.0f;
    float animD_ = 0.0f;
    float animSpace_ = 0.0f;
    float animLmb_ = 0.0f;
    float animRmb_ = 0.0f;
};

}
