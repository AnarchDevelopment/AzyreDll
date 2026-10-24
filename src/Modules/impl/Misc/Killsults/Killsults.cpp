#include "Killsults.hpp"
#include "GUI/Widgets.hpp"

#include "Features/ChatSender.hpp"
#include "Features/Friends.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Classes/LocalPlayer.hpp"
#include "SDK/Game.hpp"

#include <windows.h>
#include <cstdio>
#include <vector>

namespace mc {

namespace {

const char* kPool[] = {
    "ez",
    "L",
    "sit down",
    "skill issue",
    "imagine",
    "too easy",
    "free kill",
    "my grandma has better ping than you",
    "try harder",
    "stay mad",
    "uninstall?",
    "caught lacking",
    "gg ez",
    "want a rematch? you'll lose again",
    "didn't even try",
};

float rand01(uint32_t& rng)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float)(rng & 0xFFFFFF) / (float)0x1000000;
}

} // namespace

Killsults::Killsults()
    : Module("Killsults", "Sends a message after every kill detected nearby",
             Category::Misc, 0)
{
    markHasSettings();
}

void Killsults::onEnable()
{
    track_.clear();
    queue_.clear();
    lastQueuedMs_ = 0;
    warnedNoTpl_ = false;
    rng_ = 0x7A3F19C7u ^ (uint32_t)GetTickCount64();
}

void Killsults::onDisable()
{
    queue_.clear();
}

void Killsults::displayDebug(const std::string& msg)
{
    if (!debug_)
        return;
    chathook::display("§7[§bKillsults§7]§f " + msg);
}

std::string Killsults::pickMessage()
{
    std::vector<std::string> custom;
    for (int i = 0; i < kLines; ++i)
    {
        if (lines_[i][0] != '\0')
            custom.push_back(std::string(lines_[i]));
    }
    if (!custom.empty())
        return custom[(int)(rand01(rng_) * (float)custom.size()) % (int)custom.size()];

    int n = (int)(sizeof(kPool) / sizeof(kPool[0]));
    return std::string(kPool[(int)(rand01(rng_) * (float)n) % n]);
}

void Killsults::queueInsult()
{
    ULONGLONG now = GetTickCount64();
    float span = maxDelay_ > minDelay_ ? (maxDelay_ - minDelay_) : 0.0f;
    float d = minDelay_ + span * rand01(rng_);
    ULONGLONG at = now + (ULONGLONG)(d * 1000.0f);
    // Anti-rafaga multi-kill: separacion minima de 2s entre insultos.
    if (at < lastQueuedMs_ + 2000ull)
        at = lastQueuedMs_ + 2000ull;
    lastQueuedMs_ = at;
    queue_.push_back({at, chatsend::obfuscate(pickMessage(), obfMode_)});
}

void Killsults::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
    {
        queue_.clear();
        return;
    }
    LocalPlayer local = game.localPlayer();
    if (!local.valid())
    {
        queue_.clear();
        return;
    }

    tickKills(local);

    // Drenar la cola de mensajes pendientes.
    ULONGLONG now = GetTickCount64();
    while (!queue_.empty() && now >= queue_.front().at)
    {
        const std::string text = queue_.front().text;
        queue_.erase(queue_.begin());
        bool ok = chatsend::send(text);
        if (!ok)
        {
            if (!chatsend::haveTemplate() && !warnedNoTpl_)
            {
                warnedNoTpl_ = true;
                chathook::display("§7[§cKillsults§7]§f Type a normal chat message once "
                                  "in-game to capture the packet template.");
            }
            continue;
        }
        displayDebug("sent: " + text);
    }

    // Poda de tracks viejos.
    if (now - lastPruneMs_ > 10000ull)
    {
        lastPruneMs_ = now;
        for (auto it = track_.begin(); it != track_.end();)
        {
            if (now - it->second.lastSeenMs > 15000ull)
                it = track_.erase(it);
            else
                ++it;
        }
    }
}

void Killsults::tickKills(LocalPlayer& local)
{
    Game& game = Game::get();
    ULONGLONG now = GetTickCount64();
    Vec3 lp = local.pos();

    for (const Actor& a : game.remotePlayers())
    {
        if (!a.valid())
            continue;

        int hp = a.health();
        float dist = distance3D(lp, a.pos());

        Track& t = track_[a.address()];
        t.lastSeenMs = now;

        // Solo se confia en la caida de HP tras varias lecturas positivas:
        // si el offset de health no resuelve en el server, todo lee 0 y
        // jamas se dispara un falso kill.
        if (t.samples >= 3 && t.lastHp > 0 && hp <= 0)
        {
            bool skip = skipFriends_ && friends::isFriend(a.name());
            if (!skip && (t.lastDist <= killDistance_ || dist <= killDistance_) &&
                now - t.lastInsultMs > 8000ull)
            {
                t.lastInsultMs = now;
                queueInsult();
                displayDebug("kill detected: " + a.name());
            }
        }

        if (hp > 0)
            t.samples++;
        t.lastHp = hp;
        t.lastDist = dist;
    }
}

void Killsults::drawSettings()
{
    widgets::CSlider("Min delay##ks", &minDelay_, 0.0f, 10.0f, "%.2f s");
    widgets::CSlider("Max delay##ks", &maxDelay_, 0.0f, 20.0f, "%.2f s");
    if (maxDelay_ < minDelay_)
        maxDelay_ = minDelay_;
    widgets::CSlider("Kill distance##ks", &killDistance_, 1.0f, 32.0f, "%.0f");
    widgets::Checkbox("Skip friends##ks", &skipFriends_);

    const char* obf[] = {"None", "Invisible", "Suffix", "Full"};
    widgets::CCombo("Obfuscation##ks", &obfMode_, obf, 4);

    ImGui::TextDisabled("Empty custom lines fall back to the built-in pool");
    for (int i = 0; i < kLines; ++i)
    {
        char label[24];
        std::snprintf(label, sizeof(label), "Line %d##ks", i + 1);
        widgets::InputText(label, lines_[i], sizeof(lines_[i]));
    }
    widgets::Checkbox("Debug##ks", &debug_);
    ImGui::TextDisabled("Kill = a tracked player's health drops to 0 nearby");
}

MC_REGISTER_MODULE(Killsults);

}
