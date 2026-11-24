#include "CustomChat.hpp"

#include "Features/ChatSender.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Widgets.hpp"
#include "Input/InputSystem.hpp"
#include "Render/DX11.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"
#include "SDK/UI/Hooks/KeyGateHook.hpp"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mc {
namespace {

constexpr ImU32 kColors[16] = {
    IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 170, 255), IM_COL32(0, 170, 0, 255),
    IM_COL32(0, 170, 170, 255), IM_COL32(170, 0, 0, 255), IM_COL32(170, 0, 170, 255),
    IM_COL32(255, 170, 0, 255), IM_COL32(170, 170, 170, 255), IM_COL32(85, 85, 85, 255),
    IM_COL32(85, 85, 255, 255), IM_COL32(85, 255, 85, 255), IM_COL32(85, 255, 255, 255),
    IM_COL32(255, 85, 85, 255), IM_COL32(255, 85, 255, 255), IM_COL32(255, 255, 85, 255),
    IM_COL32(255, 255, 255, 255),
};

constexpr ImU32 kText = IM_COL32(238, 240, 245, 255);
constexpr ImU32 kName = IM_COL32(120, 190, 255, 255);
constexpr ImU32 kRepeat = IM_COL32(165, 170, 180, 255);
constexpr float kLeft = 10.0f;
constexpr float kBottom = 30.0f;

float easeOutExpo(float t)
{
    return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}

float easeOutElastic(float t)
{
    if (t <= 0.0f || t >= 1.0f)
        return t;
    constexpr float c4 = (2.0f * 3.14159265358979323846f) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

std::string stripFormatting(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC2 && i + 2 < text.size() &&
            static_cast<unsigned char>(text[i + 1]) == 0xA7)
        {
            i += 2;
            continue;
        }
        if (c == 0xA7 && i + 1 < text.size())
        {
            ++i;
            continue;
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string normalize(const std::string& text)
{
    std::string out = stripFormatting(text);
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front())))
        out.erase(out.begin());
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())))
        out.pop_back();
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

template<class RunT>
std::vector<RunT> parseFormatting(const std::string& text)
{
    std::vector<RunT> runs;
    ImU32 color = kText;
    bool bold = false;
    std::string chunk;
    auto flush = [&]() {
        if (!chunk.empty())
        {
            runs.push_back(RunT{chunk, static_cast<uint32_t>(color), bold});
            chunk.clear();
        }
    };

    for (size_t i = 0; i < text.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(text[i]);
        char code = 0;
        if (c == 0xC2 && i + 2 < text.size() &&
            static_cast<unsigned char>(text[i + 1]) == 0xA7)
        {
            code = text[i + 2];
            i += 2;
        }
        else if (c == 0xA7 && i + 1 < text.size())
            code = text[++i];

        if (code)
        {
            flush();
            code = static_cast<char>(std::tolower(static_cast<unsigned char>(code)));
            if (code >= '0' && code <= '9')
                color = kColors[code - '0'];
            else if (code >= 'a' && code <= 'f')
                color = kColors[10 + code - 'a'];
            else if (code == 'l')
                bold = true;
            else if (code == 'r')
            {
                color = kText;
                bold = false;
            }
            continue;
        }
        chunk.push_back(static_cast<char>(c));
    }
    flush();
    return runs;
}

float textWidth(ImFont* regular, ImFont* bold, float size, const std::string& text, bool isBold)
{
    ImFont* font = isBold && bold ? bold : regular;
    if (!font)
        font = ImGui::GetFont();
    return text.empty() || !font ? 0.0f
        : font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str(), text.c_str() + text.size()).x;
}

int filterChatChar(ImGuiInputTextCallbackData* data)
{
    const ImWchar c = data->EventChar;
    return (c == 0 || c == 0xA7 || c == 0x7F || (c < 0x20 && c != '\t')) ? 1 : 0;
}

const char* keyName(int key)
{
    static char name[16];
    if (key == 0)
        return "None";
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
    {
        name[0] = static_cast<char>(key);
        name[1] = '\0';
        return name;
    }
    if (key >= VK_F1 && key <= VK_F12)
    {
        std::snprintf(name, sizeof(name), "F%d", key - VK_F1 + 1);
        return name;
    }
    if (key == VK_SPACE) return "Space";
    if (key == VK_RETURN) return "Enter";
    if (key == VK_MENU) return "Alt";
    if (key == VK_CONTROL) return "Ctrl";
    if (key == VK_SHIFT) return "Shift";
    std::snprintf(name, sizeof(name), "0x%02X", key);
    return name;
}

} // namespace

CustomChat::CustomChat()
    : Module("Custom Chat", "Replaces the vanilla chat with a Rise-style panel",
             Category::Interface, 0)
{
    markHasSettings();
}

std::string CustomChat::localName()
{
    return Game::get().localFound() ? Game::get().localPlayer().name() : std::string{};
}

void CustomChat::openPanel(bool command)
{
    open_ = true;
    closingPanel_ = false;
    openAnimTime_ = 0.0f;
    command_ = command;
    focusPending_ = true;
    input_[0] = command ? '/' : '\0';
    input_[command ? 1 : 0] = '\0';
    sentCursor_ = -1;
    scrollDirty_ = true;
    lastAtBottom_ = true;
    scrollY_ = scrollMax_;
    dbgFrames_ = 0;

    // Capture keyboard only. Owning general UI input makes WheelHook strip the
    // game's raw mouse movement/buttons; that leaves native cursor screens
    // frozen if they become active while the overlay is open. The game's mouse
    // and wheel paths stay intact; KeyGate and the raw keyboard gate isolate
    // typing from gameplay.
    keygate::setArmed(true);
    MC_LOG("[CustomChat] opened command=%d; keyboard captured, mouse untouched",
           command ? 1 : 0);
}

void CustomChat::closePanel()
{
    if (!open_)
    {
        keygate::setArmed(false);
        return;
    }
    closingPanel_ = true;
    closeAnimTime_ = 0.0f;
    closeStartScale_ = std::max(panelScale_, 0.01f);
    open_ = false;
    command_ = false;
    focusPending_ = false;
    input_[0] = '\0';
    keygate::setArmed(false);
    MC_LOG("[CustomChat] closed; native mouse input was never captured");
}

void CustomChat::sendCurrent()
{
    std::string text(input_);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    if (text.empty())
    {
        closePanel();
        return;
    }

    if (chatsend::send(text))
    {
        if (sentHistory_.empty() || sentHistory_.back() != text)
            sentHistory_.push_back(text);
        while (sentHistory_.size() > 64)
            sentHistory_.pop_front();

        const std::string source = localName();
        addMessage(source, text, true);
        echoes_.push_back(Echo{normalize(text), ImGui::GetTime()});
        if (echoes_.size() > 32)
            echoes_.erase(echoes_.begin());
    }
    else
        MC_LOG_ERROR("[CustomChat] send failed");

    closePanel();
}

bool CustomChat::consumeEcho(const std::string& text)
{
    const double now = ImGui::GetTime();
    for (size_t i = 0; i < echoes_.size(); ++i)
    {
        if (echoes_[i].text == text && now - echoes_[i].time <= 6.0)
        {
            echoes_.erase(echoes_.begin() + static_cast<ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

void CustomChat::addMessage(const std::string& source, const std::string& text, bool self)
{
    const std::string plain = normalize(text);
    if (plain.empty())
        return;
    const std::string key = normalize(source) + "\x1f" + plain;
    if (std::find(frameSeen_.begin(), frameSeen_.end(), key) != frameSeen_.end())
        return;
    frameSeen_.push_back(key);

    if (!self && consumeEcho(plain))
        return;

    const double now = ImGui::GetTime();
    if (!history_.empty())
    {
        Entry& previous = history_.back();
        if (previous.source == source && previous.plain == plain && previous.self == self &&
            now - previous.lastSeen < 60.0)
        {
            ++previous.repeat;
            previous.lastSeen = now;
            lastMessageAt_ = now;
            previous.wrapRepeat = 0;
            scrollDirty_ = true;
            return;
        }
    }

    Entry item;
    item.source = source;
    item.plain = plain;
    item.self = self;
    item.lastSeen = now;
    lastMessageAt_ = now;
    if (!self && !source.empty())
        item.runs.push_back(Run{"<" + source + "> ", static_cast<uint32_t>(kName), false});
    auto content = parseFormatting<Run>(text);
    item.runs.insert(item.runs.end(), content.begin(), content.end());
    history_.push_back(std::move(item));
    while (static_cast<int>(history_.size()) > historySize_)
        history_.pop_front();
    scrollDirty_ = true;
}

void CustomChat::drainList(uintptr_t helper, uintptr_t offset)
{
    const uintptr_t vector = helper + offset;
    uintptr_t begin = 0, end = 0;
    if (!mem::rawRead(&begin, vector, sizeof(begin)) ||
        !mem::rawRead(&end, vector + sizeof(uintptr_t), sizeof(end)) ||
        !begin || end <= begin)
        return;

    const size_t stride = off::chat::MessageSize;
    const size_t bytes = static_cast<size_t>(end - begin);
    if (!stride || bytes % stride || bytes > 0x8000)
        return;
    const size_t count = std::min<size_t>(bytes / stride, 64);

    // ChatHelper is newest-first; display chronologically like Rise's chat.
    for (size_t i = count; i-- > 0;)
    {
        const uintptr_t row = begin + i * stride;
        const std::string source = mem::readString(row + off::chat::MsgSourceName);
        const std::string text = mem::readString(row + off::chat::MsgText);
        if (!text.empty())
            addMessage(source, text, false);
    }

    // Clearing both ChatHelper vectors removes the native HUD copy after the
    // message has been copied into our history.
    mem::rawWrite(vector + sizeof(uintptr_t), &begin, sizeof(begin));
}

void CustomChat::drainChat()
{
    if (!Game::get().localFound())
        return;
    Client client = Game::get().client();
    if (!client.valid())
        return;
    const uintptr_t helper = client.chatAddr();
    if (!helper || !mem::isReadable(helper, off::chain::ChatHudMessages + 0x10))
        return;

    frameSeen_.clear();
    drainList(helper, off::chain::ChatMessages);
    drainList(helper, off::chain::ChatHudMessages);
}

void CustomChat::rebuildLines(Entry& entry, float width)
{
    width = std::max(16.0f, width);
    if (entry.wrapWidth == width && entry.wrapSize == fontSize_ &&
        entry.wrapRepeat == entry.repeat && !entry.lines.empty())
        return;

    ImFont* regular = dx11::gameFont();
    ImFont* bold = dx11::gameFontBold();
    if (!regular)
        regular = ImGui::GetFont();

    std::vector<Run> runs = entry.runs;
    if (entry.repeat > 1)
    {
        char suffix[24];
        std::snprintf(suffix, sizeof(suffix), " [x%d]", entry.repeat);
        runs.push_back(Run{suffix, static_cast<uint32_t>(kRepeat), false});
    }

    entry.lines.clear();
    std::vector<Run> line;
    float lineWidth = 0.0f;
    for (const Run& run : runs)
    {
        size_t start = 0;
        while (start < run.text.size())
        {
            const size_t spaceAt = run.text.find(' ', start);
            const size_t stop = spaceAt == std::string::npos ? run.text.size() : spaceAt + 1;
            const std::string word = run.text.substr(start, stop - start);
            const float wordWidth = textWidth(regular, bold, fontSize_, word, run.bold);
            const bool space = !word.empty() && word.front() == ' ';
            if (!space && lineWidth > 0.0f && lineWidth + wordWidth > width)
            {
                entry.lines.push_back(std::move(line));
                line.clear();
                lineWidth = 0.0f;
            }
            if (!(space && line.empty()))
            {
                line.push_back(Run{word, run.color, run.bold});
                lineWidth += wordWidth;
            }
            start = stop;
        }
    }
    if (!line.empty() || entry.lines.empty())
        entry.lines.push_back(std::move(line));

    entry.wrapWidth = width;
    entry.wrapSize = fontSize_;
    entry.wrapRepeat = entry.repeat;
}

void CustomChat::drawRuns(ImDrawList* list, ImVec2 pos, const std::vector<Run>& runs,
                          float alpha, bool shadow)
{
    if (!list)
        return;
    ImFont* regular = dx11::gameFont();
    ImFont* bold = dx11::gameFontBold();
    if (!regular)
        regular = ImGui::GetFont();
    float x = pos.x;
    for (const Run& run : runs)
    {
        if (run.text.empty())
            continue;
        ImFont* font = run.bold && bold ? bold : regular;
        if (!font)
            continue;
        ImU32 color = static_cast<ImU32>(run.color);
        const int a = static_cast<int>(((color >> IM_COL32_A_SHIFT) & 0xff) * clampf(alpha, 0, 1));
        color = (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
        if (shadow)
            list->AddText(font, fontSize_ * renderScale_, ImVec2(x + 1, pos.y + 1),
                          IM_COL32(0, 0, 0, static_cast<int>(150 * alpha)),
                          run.text.c_str(), run.text.c_str() + run.text.size());
        list->AddText(font, fontSize_ * renderScale_, ImVec2(x, pos.y), color,
                      run.text.c_str(), run.text.c_str() + run.text.size());
        x += textWidth(regular, bold, fontSize_ * renderScale_, run.text, run.bold);
    }
}

void CustomChat::renderHistory(bool panelOpen, float alpha)
{
    ImGuiIO& io = ImGui::GetIO();
    const float lineHeight = fontSize_ * renderScale_ * 1.2f + 2.0f * renderScale_;
    const float width = std::min(width_, io.DisplaySize.x - 20.0f);
    std::vector<std::pair<const std::vector<Run>*, float>> lines;
    const double now = ImGui::GetTime();
    for (Entry& entry : history_)
    {
        rebuildLines(entry, width - 10.0f);
        float fade = 1.0f;
        if (!panelOpen && disappearMs_ > 0)
        {
            const double ageMs = (now - entry.lastSeen) * 1000.0;
            if (ageMs > disappearMs_)
                fade = clampf(1.0f - static_cast<float>(ageMs - disappearMs_) / 900.0f, 0.0f, 1.0f);
        }
        fade *= alpha;
        for (const auto& line : entry.lines)
            lines.emplace_back(&line, fade);
    }

    if (!panelOpen)
    {
        int first = static_cast<int>(lines.size()) - static_cast<int>(closedHeight_ / lineHeight);
        first = std::max(first, 0);
        float y = io.DisplaySize.y - kBottom - lineHeight * static_cast<float>(lines.size() - first);
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        for (size_t i = static_cast<size_t>(first); i < lines.size(); ++i, y += lineHeight)
        {
            if (lines[i].second <= 0.01f)
                continue;
            if (background_)
                draw->AddRectFilled(ImVec2(kLeft - 3, y - 1), ImVec2(kLeft + width, y + lineHeight),
                                    IM_COL32(0, 0, 0, static_cast<int>(145 * lines[i].second)), 2.0f);
            drawRuns(draw, ImVec2(kLeft, y), *lines[i].first, lines[i].second, shadow_);
        }
        return;
    }

    const float inputHeight = lineHeight + 9.0f;
    const float bodyHeight = std::max(30.0f, panelHeight_ * panelScale_ - inputHeight - 10.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##rise_chat_history", ImVec2(0, bodyHeight), false,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoScrollbar);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const float contentHeight = std::max(lineHeight, lineHeight * static_cast<float>(lines.size()));
    scrollMax_ = std::max(0.0f, contentHeight - bodyHeight);
    if (scrollDirty_ || lastAtBottom_)
        scrollY_ = scrollMax_;

    if (io.MouseWheel != 0.0f)
    {
        scrollY_ = clampf(scrollY_ - io.MouseWheel * lineHeight * 3.0f, 0.0f, scrollMax_);
        lastAtBottom_ = scrollMax_ - scrollY_ <= 1.0f;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
        scrollY_ = clampf(scrollY_ - bodyHeight * 0.85f, 0.0f, scrollMax_);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
        scrollY_ = clampf(scrollY_ + bodyHeight * 0.85f, 0.0f, scrollMax_);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        scrollY_ = 0.0f;
    if (ImGui::IsKeyPressed(ImGuiKey_End, false))
        scrollY_ = scrollMax_;
    scrollY_ = clampf(scrollY_, 0.0f, scrollMax_);
    const float scrollBlend = easeOutExpo(clampf(io.DeltaTime * 2.0f, 0.0f, 1.0f));
    scrollDrawY_ += (scrollY_ - scrollDrawY_) * scrollBlend;

    draw->PushClipRect(start, ImVec2(start.x + avail.x, start.y + bodyHeight), true);
    if (lines.empty())
        draw->AddText(ImVec2(start.x, start.y - scrollDrawY_), IM_COL32(220, 220, 220, 150), "Chat is empty");
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const auto& item = lines[i];
        const float y = start.y + static_cast<float>(i) * lineHeight - scrollDrawY_;
        if (y + lineHeight < start.y || y > start.y + bodyHeight)
            continue;
        if (background_)
            draw->AddRectFilled(ImVec2(start.x - 1, y - 1),
                                ImVec2(start.x + avail.x + 1, y + lineHeight),
                                IM_COL32(0, 0, 0, static_cast<int>(48 * item.second)));
        drawRuns(draw, ImVec2(start.x, y), *item.first, item.second, shadow_);
    }
    draw->PopClipRect();
    ImGui::Dummy(ImVec2(1.0f, bodyHeight));
    lastAtBottom_ = scrollMax_ - scrollY_ <= 1.0f;
    scrollDirty_ = false;
    if (dbgFrames_ < 3)
    {
        MC_LOG("[CustomChat] scroll frame=%d messages=%d max=%.0f wheel=%.1f hover=%d",
            ++dbgFrames_, static_cast<int>(history_.size()), scrollMax_, io.MouseWheel, hovered ? 1 : 0);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void CustomChat::renderPanel(float alpha)
{
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        closePanel();
        return;
    }

    const float width = std::min(width_, io.DisplaySize.x - 20.0f);
    const float height = std::max(30.0f, panelHeight_ * panelScale_);
    const float scaledWidth = width * panelScale_;
    const float x = kLeft - 5.0f + (width + 10.0f) * (1.0f - panelScale_) * 0.5f;
    const float y = io.DisplaySize.y - kBottom - height;
    renderScale_ = panelScale_;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(scaledWidth + 10.0f * panelScale_, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5 * panelScale_, 5 * panelScale_));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("##custom_chat_rise", nullptr, flags))
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetWindowPos();
        const ImVec2 max(min.x + scaledWidth + 10.0f * panelScale_, min.y + height);
        if (background_)
            draw->AddRectFilled(min, max, IM_COL32(0, 0, 0, static_cast<int>(165 * alpha)), 4.0f);

        renderHistory(true, alpha);
        if (open_)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ImGui::SetCursorPosY(std::max(ImGui::GetStyle().WindowPadding.y,
                height - ImGui::GetStyle().WindowPadding.y - ImGui::GetFrameHeight() - 2.0f * panelScale_));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5 * panelScale_, 4 * panelScale_));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0.35f * alpha));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.07f * alpha));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.10f * alpha));
            ImFont* font = dx11::gameFont();
            if (font)
                ImGui::PushFont(font, fontSize_ * panelScale_);
            if (focusPending_)
            {
                ImGui::SetKeyboardFocusHere();
                focusPending_ = false;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool send = ImGui::InputText("##custom_chat_input", input_, sizeof(input_),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter,
                filterChatChar);
            if (font)
                ImGui::PopFont();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            ImGui::PopStyleVar();
            if (send)
                sendCurrent();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    renderScale_ = 1.0f;
}

void CustomChat::renderHud(float alpha)
{
    if (!history_.empty())
        renderHistory(false, alpha);
}

void CustomChat::onEnable()
{
    if (!keygate::install())
        MC_LOG_ERROR("[CustomChat] keyboard gate unavailable");
    keygate::setArmed(false);
    keygate::setTBlockArmed(false);
    tWasDown_ = (GetAsyncKeyState('T') & 0x8000) != 0;
    MC_LOG("[CustomChat] enabled; T is reserved for CustomChat while LocalPlayer exists");
}

void CustomChat::onDisable()
{
    closePanel();
    keygate::setTBlockArmed(false);
    keygate::setArmed(false);
    closingPanel_ = false;
    panelScale_ = 0.0f;
    echoes_.clear();
    frameSeen_.clear();
}

void CustomChat::onFrame(float)
{
    const bool hasLocalPlayer = Game::get().localFound();
    keygate::setTBlockArmed(hasLocalPlayer);

    // GetAsyncKeyState is the DLL-side key path; the raw-input and message
    // gates remove T only from Bedrock. Never route through ChatScreen.
    const bool tDown = (GetAsyncKeyState('T') & 0x8000) != 0;
    if (hasLocalPlayer && input::windowActive() && !menu::visible() &&
        tDown && !tWasDown_ && !open_ && !pickKey_)
        openPanel(false);
    tWasDown_ = tDown;

    // Configurable secondary key. T remains reserved for the default chat.
    if (openKey_ > 0 && openKey_ < 256 && openKey_ != 'T')
    {
        static bool wasDown = false;
        const bool down = (GetAsyncKeyState(openKey_) & 0x8000) != 0;
        if (down && !wasDown && !open_ && !pickKey_ && !menu::visible())
            openPanel(false);
        wasDown = down;
    }

    // A native inventory/pause screen keeps its pointer and wheel path because
    // Custom Chat never claims mouse capture.
    if (open_ && menu::visible())
        closePanel();
    if (open_ && !hasLocalPlayer)
        closePanel();

    drainChat();
    for (size_t i = 0; i < echoes_.size();)
    {
        if (ImGui::GetTime() - echoes_[i].time > 6.0)
            echoes_.erase(echoes_.begin() + static_cast<ptrdiff_t>(i));
        else
            ++i;
    }
}

void CustomChat::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 1 || io.DisplaySize.y <= 1)
        return;
    const float hudTarget = lastMessageAt_ > 0.0 &&
        (disappearMs_ == 0 || (ImGui::GetTime() - lastMessageAt_) * 1000.0 < disappearMs_)
        ? 1.0f : 0.0f;
    const float target = open_ ? 1.0f : hudTarget;
    if (animations_)
    {
        const float alphaStep = clampf(io.DeltaTime / 0.40f, 0.0f, 1.0f);
        if (anim_ < target)
            anim_ = std::min(target, anim_ + alphaStep);
        else if (anim_ > target)
            anim_ = std::max(target, anim_ - alphaStep);
        if (open_)
        {
            openAnimTime_ = std::min(1.0f, openAnimTime_ + io.DeltaTime / 0.85f);
            panelScale_ = easeOutElastic(openAnimTime_);
            const float lineHeight = fontSize_ * 1.2f + 2.0f;
            const float inputHeight = lineHeight + 9.0f;
            size_t messageLines = 0;
            for (Entry& entry : history_)
            {
                rebuildLines(entry, width_ - 10.0f);
                messageLines += entry.lines.size();
            }
            const float naturalHeight = static_cast<float>(messageLines) * lineHeight + inputHeight + 10.0f;
            const float heightTarget = clampf(naturalHeight, inputHeight + 40.0f, openHeight_);
            const float heightBlend = easeOutExpo(clampf(io.DeltaTime / 0.50f, 0.0f, 1.0f));
            panelHeight_ += (heightTarget - panelHeight_) * heightBlend;
        }
        else
        {
            if (closingPanel_)
            {
                closeAnimTime_ = std::min(1.0f, closeAnimTime_ + io.DeltaTime / 0.30f);
                const float easeIn = closeAnimTime_ >= 1.0f ? 1.0f
                    : std::pow(2.0f, 10.0f * (closeAnimTime_ - 1.0f));
                panelScale_ = closeStartScale_ * (1.0f - easeIn);
                if (closeAnimTime_ >= 1.0f)
                {
                    closingPanel_ = false;
                    panelScale_ = 0.0f;
                }
            }
            else
            {
                openAnimTime_ = 0.0f;
                panelScale_ = 0.0f;
            }
        }
    }
    else
    {
        anim_ = target;
        closingPanel_ = false;
        panelScale_ = target;
        panelHeight_ = openHeight_;
        openAnimTime_ = target;
    }
    if (!open_ && !closingPanel_ && anim_ <= 0.005f)
    {
        anim_ = 0.0f;
        return;
    }
    if (!open_ && (menu::visible() || menu::hudEditing()))
        return;
    const float alpha = clampf(anim_, 0.0f, 1.0f);
    if (open_)
        renderPanel(alpha);
    else if (closingPanel_)
        renderPanel(1.0f);
    else
        renderHud(alpha);
}

void CustomChat::drawSettings()
{
    widgets::CSlider("Font Size##customchat", &fontSize_, 10.0f, 26.0f, "%.0f");
    widgets::CSlider("Width##customchat", &width_, 200.0f, 600.0f, "%.0f");
    widgets::CSlider("Open Height##customchat", &openHeight_, 70.0f, 220.0f, "%.0f");
    widgets::CSlider("Max Closed Height##customchat", &closedHeight_, 30.0f, 300.0f, "%.0f");
    widgets::CSlider("History##customchat", &historySize_, 50, 1000, "%d");
    widgets::CSlider("Disappearance (ms)##customchat", &disappearMs_, 0, 10000, "%d");
    widgets::Checkbox("Animations##customchat", &animations_);
    widgets::Checkbox("Background##customchat", &background_);
    widgets::Checkbox("Text Shadow##customchat", &shadow_);
    ImGui::Text("Open Key: %s", keyName(openKey_));
    ImGui::SameLine();
    if (ImGui::Button(pickKey_ ? "Press a key...##customchat" : "Set##customchat"))
        pickKey_ = true;
    if (pickKey_)
    {
        for (int key = 1; key < 256; ++key)
        {
            if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON || key == VK_INSERT)
                continue;
            if (GetAsyncKeyState(key) & 0x8000)
            {
                openKey_ = key == VK_ESCAPE ? 0 : key;
                pickKey_ = false;
                break;
            }
        }
    }
    if (ImGui::Button("Clear History##customchat"))
    {
        history_.clear();
        echoes_.clear();
        frameSeen_.clear();
    }
    ImGui::TextDisabled("T opens CustomChat only while LocalPlayer exists. Native ChatScreen is untouched.");
    ImGui::TextDisabled("Text: Mojangles. §l uses Mojangles Bold; §0-§f set colors.");
}

nlohmann::json CustomChat::saveSettings() const
{
    return {
        {"fontSize", fontSize_}, {"width", width_}, {"openHeight", openHeight_},
        {"closedHeight", closedHeight_}, {"historySize", historySize_},
        {"disappearMs", disappearMs_}, {"animations", animations_},
        {"background", background_}, {"shadow", shadow_}, {"openKey", openKey_},
        {"openKeyVersion", 3}
    };
}

void CustomChat::loadSettings(const nlohmann::json& j)
{
    fontSize_ = j.value("fontSize", fontSize_);
    width_ = j.value("width", j.value("panelWidth", width_));
    openHeight_ = j.value("openHeight", openHeight_);
    closedHeight_ = j.value("closedHeight", closedHeight_);
    historySize_ = j.value("historySize", historySize_);
    disappearMs_ = j.value("disappearMs", disappearMs_);
    animations_ = j.value("animations", animations_);
    background_ = j.value("background", background_);
    shadow_ = j.value("shadow", shadow_);
    openKey_ = j.value("openKey", openKey_);
    if (j.value("openKeyVersion", 0) < 3 && openKey_ == 'Y')
        openKey_ = 'T';
    fontSize_ = clampf(fontSize_, 10.0f, 26.0f);
    width_ = clampf(width_, 200.0f, 600.0f);
    openHeight_ = clampf(openHeight_, 70.0f, 220.0f);
    closedHeight_ = clampf(closedHeight_, 30.0f, 300.0f);
    historySize_ = std::clamp(historySize_, 50, 1000);
    disappearMs_ = std::clamp(disappearMs_, 0, 10000);
    openKey_ = (openKey_ > 0 && openKey_ < 256) ? openKey_ : 0;
}

MC_REGISTER_MODULE(CustomChat);

} // namespace mc
