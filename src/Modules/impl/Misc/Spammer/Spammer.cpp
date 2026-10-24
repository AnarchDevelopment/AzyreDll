#include "Spammer.hpp"
#include "GUI/Widgets.hpp"

#include "Features/ChatSender.hpp"
#include "SDK/ChatHook.hpp"

#include <windows.h>
#include <cstdio>
#include <vector>

namespace mc {

namespace {

const char* kPool[] = {
    "gg",
    "wp",
    "lol",
    "nice fight",
    "anyone want to duel?",
    "ez clap",
    "that was close",
    "one more?",
    "gg everyone",
    "who's up for a 1v1?",
};

float rand01(uint32_t& rng)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float)(rng & 0xFFFFFF) / (float)0x1000000;
}

} // namespace

Spammer::Spammer()
    : Module("Spammer", "Spams chat messages with anti-spam obfuscation",
             Category::Misc, 0)
{
    markHasSettings();
}

void Spammer::onEnable()
{
    nextSendMs_ = 0;
    seq_ = 0;
    warnedNoTpl_ = false;
    rng_ = 0x51A9D0F1u ^ (uint32_t)GetTickCount64();
}

void Spammer::displayDebug(const std::string& msg)
{
    if (!debug_)
        return;
    chathook::display("§7[§bSpammer§7]§f " + msg);
}

std::string Spammer::pickMessage()
{
    // Lineas custom no vacias primero.
    std::vector<std::string> custom;
    for (int i = 0; i < kLines; ++i)
    {
        if (lines_[i][0] != '\0')
            custom.push_back(std::string(lines_[i]));
    }

    if (!custom.empty())
    {
        if (order_ == 0)
        {
            std::string s = custom[seq_ % (int)custom.size()];
            seq_ = (seq_ + 1) % (int)custom.size();
            return s;
        }
        return custom[(int)(rand01(rng_) * (float)custom.size()) % (int)custom.size()];
    }

    int n = (int)(sizeof(kPool) / sizeof(kPool[0]));
    return std::string(kPool[(int)(rand01(rng_) * (float)n) % n]);
}

void Spammer::onTick()
{
    ULONGLONG now = GetTickCount64();
    if (nextSendMs_ == 0)
    {
        nextSendMs_ = now + (ULONGLONG)(minDelay_ * 1000.0f);
        return;
    }
    if (now < nextSendMs_)
        return;

    std::string text = chatsend::obfuscate(pickMessage(), obfMode_);
    bool ok = chatsend::send(text);
    if (!ok)
    {
        if (!chatsend::haveTemplate() && !warnedNoTpl_)
        {
            warnedNoTpl_ = true;
            chathook::display("§7[§cSpammer§7]§f Type a normal chat message once "
                              "in-game to capture the packet template.");
        }
        nextSendMs_ = now + 500;
        return;
    }

    displayDebug("sent: " + text);

    float span = maxDelay_ > minDelay_ ? (maxDelay_ - minDelay_) : 0.0f;
    float wait = minDelay_ + span * rand01(rng_);
    nextSendMs_ = now + (ULONGLONG)(wait * 1000.0f);
}

void Spammer::drawSettings()
{
    widgets::CSlider("Min delay##spm", &minDelay_, 0.5f, 120.0f, "%.1f s");
    widgets::CSlider("Max delay##spm", &maxDelay_, 0.5f, 300.0f, "%.1f s");
    if (maxDelay_ < minDelay_)
        maxDelay_ = minDelay_;

    const char* obf[] = {"None", "Invisible", "Suffix", "Full"};
    widgets::CCombo("Obfuscation##spm", &obfMode_, obf, 4);
    const char* orders[] = {"Sequential", "Random"};
    widgets::CCombo("Order##spm", &order_, orders, 2);

    ImGui::TextDisabled("Empty custom lines fall back to the built-in pool");
    for (int i = 0; i < kLines; ++i)
    {
        char label[24];
        std::snprintf(label, sizeof(label), "Line %d##spm", i + 1);
        widgets::InputText(label, lines_[i], sizeof(lines_[i]));
    }
    widgets::Checkbox("Debug##spm", &debug_);
    ImGui::TextDisabled("Invisible: zero-width chars / Suffix: random tag");
}

MC_REGISTER_MODULE(Spammer);

}
