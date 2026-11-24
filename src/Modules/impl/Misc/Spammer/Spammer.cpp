#include "Spammer.hpp"
#include "Config/Config.hpp"
#include "GUI/Widgets.hpp"

#include "Features/ChatSender.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/UI/Hooks/ChatHook.hpp"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>
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
    warnedWordlist_ = false;
    warnedNoPlayer_ = false;
    rng_ = 0x51A9D0F1u ^ (uint32_t)GetTickCount64();
    refreshWordlists();
    loadSelectedWordlist();
}

void Spammer::displayDebug(const std::string& msg)
{
    if (!debug_)
        return;
    chathook::display("§7[§bSpammer§7]§f " + msg);
}

bool Spammer::expandPlayerPlaceholder(std::string& message)
{
    constexpr const char* kPlaceholder = "{player}";
    constexpr size_t kPlaceholderLength = 8;
    if (message.find(kPlaceholder) == std::string::npos)
        return true;

    std::vector<std::string> candidates;
    for (const Actor& actor : Game::get().remotePlayersSnapshot())
    {
        if (!actor.valid() || !actor.isRemotePlayer())
            continue;
        std::string name = actor.name();
        if (!name.empty())
            candidates.push_back(std::move(name));
    }
    if (candidates.empty())
        return false;

    const size_t choice = (size_t)(rand01(rng_) * (float)candidates.size()) % candidates.size();
    const std::string& player = candidates[choice];
    size_t at = 0;
    while ((at = message.find(kPlaceholder, at)) != std::string::npos)
    {
        message.replace(at, kPlaceholderLength, player);
        at += player.size();
    }
    return true;
}

std::string Spammer::pickMessage()
{
    if (useWordlist_)
    {
        if (wordlistMessages_.empty())
            return {};

        if (order_ == 0)
        {
            std::string s = wordlistMessages_[seq_ % (int)wordlistMessages_.size()];
            seq_ = (seq_ + 1) % (int)wordlistMessages_.size();
            return s;
        }
        return wordlistMessages_[(int)(rand01(rng_) * (float)wordlistMessages_.size()) %
                                 (int)wordlistMessages_.size()];
    }

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

void Spammer::refreshWordlists()
{
    wordlistFiles_.clear();
    const std::string base = config::baseDir();
    if (base.empty())
        return;

    const std::filesystem::path directory = std::filesystem::path(base) / "Wordlists";
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec)
        return;

    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end;
         it.increment(ec))
    {
        std::error_code entryError;
        if (!it->is_regular_file(entryError) || entryError)
            continue;

        const std::string extension = it->path().extension().string();
        if (extension.size() != 4 || std::tolower((unsigned char)extension[1]) != 't' ||
            std::tolower((unsigned char)extension[2]) != 'x' ||
            std::tolower((unsigned char)extension[3]) != 't')
            continue;
        wordlistFiles_.push_back(it->path().filename().string());
    }

    std::sort(wordlistFiles_.begin(), wordlistFiles_.end(), [](const std::string& a,
                                                               const std::string& b) {
        std::string al = a, bl = b;
        std::transform(al.begin(), al.end(), al.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });
        std::transform(bl.begin(), bl.end(), bl.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });
        return al == bl ? a < b : al < bl;
    });
}

bool Spammer::loadSelectedWordlist()
{
    wordlistMessages_.clear();
    loadedWordlist_.clear();
    if (selectedWordlist_.empty())
        return false;

    // The selection comes from enumerated filenames. Revalidate persisted
    // settings so a config cannot escape the Wordlists directory.
    const auto found = std::find(wordlistFiles_.begin(), wordlistFiles_.end(), selectedWordlist_);
    if (found == wordlistFiles_.end())
        return false;

    const std::string base = config::baseDir();
    if (base.empty())
        return false;
    const std::filesystem::path path = std::filesystem::path(base) / "Wordlists" /
                                       selectedWordlist_;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    std::string line;
    bool firstLine = true;
    while (std::getline(file, line))
    {
        if (firstLine)
        {
            firstLine = false;
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
                (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                line.erase(0, 3);
        }

        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto first = std::find_if_not(line.begin(), line.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        });
        const auto last = std::find_if_not(line.rbegin(), line.rend(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }).base();
        if (first >= last || *first == '#')
            continue;

        wordlistMessages_.emplace_back(first, last);
    }

    if (file.bad())
    {
        wordlistMessages_.clear();
        return false;
    }
    loadedWordlist_ = selectedWordlist_;
    seq_ = 0;
    return !wordlistMessages_.empty();
}

void Spammer::onTick()
{
    if (!noDelay_)
        sendNext();
}

void Spammer::onFrame(float /*dt*/)
{
    if (noDelay_)
        sendNext();
}

void Spammer::sendNext()
{
    ULONGLONG now = GetTickCount64();
    if (!noDelay_ && nextSendMs_ == 0)
    {
        nextSendMs_ = now + (ULONGLONG)(minDelay_ * 1000.0f);
        return;
    }
    // With noDelay enabled, successful sends clear the timer and the next
    // message can go on the next module tick. Nonzero timers remain active for
    // failure/empty-wordlist backoff so the module does not spin on errors.
    if (nextSendMs_ != 0 && now < nextSendMs_)
        return;

    if (useWordlist_ && loadedWordlist_ != selectedWordlist_)
        loadSelectedWordlist();

    std::string message = pickMessage();
    if (message.empty())
    {
        if (!warnedWordlist_)
        {
            warnedWordlist_ = true;
            displayDebug("wordlist is missing or has no messages; sending paused");
        }
        nextSendMs_ = now + 1000;
        return;
    }
    warnedWordlist_ = false;

    // Wordlist commands are wrapped in double quotes to distinguish them from
    // ordinary entries, e.g. "/tell {player} test". Remove only that wrapper.
    if (message.size() >= 3 && message.front() == '"' && message[1] == '/' &&
        message.back() == '"')
        message = message.substr(1, message.size() - 2);

    const bool command = !message.empty() && message[0] == '/';
    if (command && !expandPlayerPlaceholder(message))
    {
        if (!warnedNoPlayer_)
        {
            warnedNoPlayer_ = true;
            displayDebug("{player} needs at least one remote player in range");
        }
        nextSendMs_ = now + 1000;
        return;
    }
    warnedNoPlayer_ = false;

    std::string text = command ? message : chatsend::obfuscate(message, obfMode_);
    bool ok = command ? chatsend::sendCommand(text) : chatsend::send(text);
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

    displayDebug(std::string(command ? "command sent: " : "sent: ") + text);

    if (noDelay_)
    {
        nextSendMs_ = 0;
        return;
    }

    float span = maxDelay_ > minDelay_ ? (maxDelay_ - minDelay_) : 0.0f;
    float wait = minDelay_ + span * rand01(rng_);
    nextSendMs_ = now + (ULONGLONG)(wait * 1000.0f);
}

void Spammer::drawSettings()
{
    if (widgets::Checkbox("No delay##spm", &noDelay_) && noDelay_)
        nextSendMs_ = 0;

    ImGui::BeginDisabled(noDelay_);
    widgets::CSlider("Min delay##spm", &minDelay_, 0.5f, 120.0f, "%.1f s");
    widgets::CSlider("Max delay##spm", &maxDelay_, 0.5f, 300.0f, "%.1f s");
    ImGui::EndDisabled();
    if (maxDelay_ < minDelay_)
        maxDelay_ = minDelay_;

    const char* obf[] = {"None", "Invisible", "Suffix", "Full"};
    widgets::CCombo("Obfuscation##spm", &obfMode_, obf, 4);
    const char* orders[] = {"Sequential", "Random"};
    widgets::CCombo("Order##spm", &order_, orders, 2);

    widgets::Checkbox("Use wordlist##spm", &useWordlist_);
    if (useWordlist_)
    {
        if (wordlistFiles_.empty())
        {
            selectedWordlist_.clear();
            wordlistMessages_.clear();
            loadedWordlist_.clear();
            ImGui::TextDisabled("No .txt wordlists found in Azyre\\Wordlists");
            if (ImGui::Button("Refresh wordlists##spm_empty"))
                refreshWordlists();
        }
        else
        {
            auto selected = std::find(wordlistFiles_.begin(), wordlistFiles_.end(), selectedWordlist_);
            if (selected == wordlistFiles_.end())
            {
                selectedWordlist_ = wordlistFiles_.front();
                loadSelectedWordlist();
                selected = wordlistFiles_.begin();
            }
            int selectedIndex = (int)std::distance(wordlistFiles_.begin(), selected);
            if (widgets::CCombo("Wordlist##spm", &selectedIndex, wordlistFiles_))
            {
                selectedWordlist_ = wordlistFiles_[selectedIndex];
                loadSelectedWordlist();
                warnedWordlist_ = false;
                nextSendMs_ = 0;
            }
            if (selected != wordlistFiles_.end() && loadedWordlist_ != selectedWordlist_)
                loadSelectedWordlist();

            if (ImGui::Button("Refresh wordlists##spm"))
            {
                refreshWordlists();
                loadSelectedWordlist();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%d messages", (int)wordlistMessages_.size());
            if (wordlistMessages_.empty())
                ImGui::TextDisabled("Empty lines and lines starting with # are ignored");
        }
    }
    ImGui::TextDisabled("Entries starting with / are sent as commands without obfuscation");
    ImGui::TextDisabled("Wordlist example: \"/tell {player} test\" (random remote player in range)");

    if (!useWordlist_)
    {
        ImGui::TextDisabled("Empty custom lines fall back to the built-in pool");
        for (int i = 0; i < kLines; ++i)
        {
            char label[24];
            std::snprintf(label, sizeof(label), "Line %d##spm", i + 1);
            widgets::InputText(label, lines_[i], sizeof(lines_[i]));
        }
    }
    widgets::Checkbox("Debug##spm", &debug_);
    ImGui::TextDisabled("Invisible: zero-width chars / Suffix: random tag");
}

MC_REGISTER_MODULE(Spammer);

}
