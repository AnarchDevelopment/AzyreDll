#pragma once

#include "Modules/Module.hpp"

#include <imgui.h>

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace mc {

// Overlay de chat de Bedrock. KeyGate bloquea T solo con LocalPlayer y captura
// el teclado al escribir; el mouse y la rueda siguen siendo propiedad del juego.
class CustomChat : public Module
{
public:
    CustomChat();

    void onEnable() override;
    void onDisable() override;
    void onFrame(float dt) override;
    void onRender() override;
    void drawSettings() override;
    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

    bool capturesText() const override { return open_; }

private:
    struct Run
    {
        std::string text;
        uint32_t color = 0;
        bool bold = false;
    };

    struct Entry
    {
        std::string source;
        std::string plain;
        std::vector<Run> runs;
        std::vector<std::vector<Run>> lines;
        bool self = false;
        int repeat = 1;
        double lastSeen = 0.0;
        float wrapWidth = -1.0f;
        float wrapSize = -1.0f;
        int wrapRepeat = 0;
    };

    struct Echo
    {
        std::string text;
        double time = 0.0;
    };

    void openPanel(bool command);
    void closePanel();
    void sendCurrent();
    void drainChat();
    void drainList(uintptr_t helper, uintptr_t offset);
    void addMessage(const std::string& source, const std::string& text, bool self);
    bool consumeEcho(const std::string& text);
    void rebuildLines(Entry& entry, float width);
    void drawRuns(ImDrawList* list, ImVec2 pos, const std::vector<Run>& runs,
                  float alpha, bool shadow);
    void renderHistory(bool panelOpen, float alpha);
    void renderPanel(float alpha);
    void renderHud(float alpha);
    std::string localName();

    bool open_ = false;
    bool command_ = false;
    bool focusPending_ = false;
    bool pickKey_ = false;
    bool lastAtBottom_ = true;
    bool scrollDirty_ = true;
    bool tWasDown_ = false;
    bool closingPanel_ = false;
    float anim_ = 0.0f;
    float openAnimTime_ = 0.0f;
    float closeAnimTime_ = 0.0f;
    float closeStartScale_ = 1.0f;
    float panelScale_ = 0.0f;
    float panelHeight_ = 0.0f;
    float renderScale_ = 1.0f;
    float scrollY_ = 0.0f;
    float scrollDrawY_ = 0.0f;
    float scrollMax_ = 0.0f;
    char input_[256]{};
    int sentCursor_ = -1;
    int dbgFrames_ = 0;
    double lastMessageAt_ = 0.0;

    std::deque<Entry> history_;
    std::vector<Echo> echoes_;
    std::vector<std::string> frameSeen_;
    std::deque<std::string> sentHistory_;

    // Layout follows Rise Chat: compact, anchored at the lower left.
    float fontSize_ = 16.0f;
    float width_ = 320.0f;
    float openHeight_ = 130.0f;
    float closedHeight_ = 130.0f;
    int historySize_ = 400;
    int disappearMs_ = 5000;
    bool animations_ = true;
    bool background_ = true;
    bool shadow_ = true;
    int openKey_ = 'T';
};

}
