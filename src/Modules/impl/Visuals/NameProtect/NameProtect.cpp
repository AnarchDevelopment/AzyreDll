#include "NameProtect.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Memory.hpp"
#include "SDK/Game.hpp"
#include "SDK/Offsets.hpp"

#include <imgui.h>

#include <cctype>
#include <cfloat>
#include <cstring>
#include <vector>

namespace mc {

namespace {

// Paleta de Minecraft (codes 0-f).
struct MCColor
{
    const char* name;
    unsigned char r, g, b;
};

constexpr MCColor kColors[16] = {
    {"Black", 0x00, 0x00, 0x00},   {"Dark Blue", 0x00, 0x00, 0xAA},
    {"Dark Green", 0x00, 0xAA, 0x00}, {"Dark Aqua", 0x00, 0xAA, 0xAA},
    {"Dark Red", 0xAA, 0x00, 0x00},  {"Purple", 0xAA, 0x00, 0xAA},
    {"Gold", 0xFF, 0xAA, 0x00},      {"Gray", 0xAA, 0xAA, 0xAA},
    {"Dark Gray", 0x55, 0x55, 0x55}, {"Blue", 0x55, 0x55, 0xFF},
    {"Green", 0x55, 0xFF, 0x55},     {"Aqua", 0x55, 0xFF, 0xFF},
    {"Red", 0xFF, 0x55, 0x55},       {"Pink", 0xFF, 0x55, 0xFF},
    {"Yellow", 0xFF, 0xFF, 0x55},    {"White", 0xFF, 0xFF, 0xFF},
};

char codeFor(int idx)
{
    return "0123456789abcdef"[idx < 0 ? 0 : (idx > 15 ? 15 : idx)];
}

int codeIdx(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    char l = (char)tolower((unsigned char)c);
    if (l >= 'a' && l <= 'f')
        return l - 'a' + 10;
    return 15;
}

int clampColorIdx(int i)
{
    return i < 0 ? 0 : (i > 15 ? 15 : i);
}

// Cuantiza un RGB arbitrario al color MC mas cercano (distancia euclidea).
int nearestColor(float r, float g, float b)
{
    int best = 0;
    float bestD = FLT_MAX;
    for (int i = 0; i < 16; ++i)
    {
        float dr = r - (float)kColors[i].r;
        float dg = g - (float)kColors[i].g;
        float db = b - (float)kColors[i].b;
        float d = dr * dr + dg * dg + db * db;
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

// Escribe un std::string del juego respetando su estado SSO/heap. Nunca
// crece la capacidad: no se puede asignar memoria con el CRT del juego
// (el liberaria el puntero con otro heap). Si no cabe, devuelve false
// sin tocar nada.
bool writeGameString(uintptr_t strAddr, const std::string& v)
{
    if (!mem::isReadable(strAddr, 0x20))
        return false;

    uint64_t cap = mem::read<uint64_t>(strAddr + 0x18);
    if (cap >= 16)
    {
        // Heap: reutiliza el buffer existente (la capacidad no cambia, el
        // juego lo libera con normalidad).
        if (v.size() > cap)
            return false;
        uintptr_t buf = mem::read<uintptr_t>(strAddr);
        if (!buf || !mem::isReadable(buf, v.size() + 1))
            return false;
        mem::rawWrite(buf, v.c_str(), v.size() + 1);
        mem::write<uint64_t>(strAddr + 0x10, v.size());
        return true;
    }

    // SSO: escritura inline sobre el buffer de 16 bytes.
    if (v.size() > 15)
        return false;
    char buf[16] = {};
    memcpy(buf, v.c_str(), v.size());
    mem::rawWrite(strAddr, buf, 16);
    mem::write<uint64_t>(strAddr + 0x10, v.size());
    return true;
}

// Reemplaza todas las apariciones de `from` por `to` dentro de un
// std::string del juego (solo si el resultado cabe).
bool replaceInGameString(uintptr_t strAddr, const std::string& from, const std::string& to)
{
    if (from.empty() || !mem::isReadable(strAddr, 0x20))
        return false;

    std::string text = mem::readString(strAddr);
    size_t pos = text.find(from);
    if (pos == std::string::npos)
        return false;

    std::string out;
    out.reserve(text.size());
    size_t last = 0;
    while (pos != std::string::npos)
    {
        out.append(text, last, pos - last);
        out += to;
        last = pos + from.size();
        pos = text.find(from, last);
    }
    out.append(text, last, text.size() - last);
    return writeGameString(strAddr, out);
}

// Nombre legible: quita los color codes (§x) igual que Actor::name().
// Acepta § en UTF-8 (C2 A7, como lo emite el juego) y legacy de 1 byte.
std::string plainName(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        unsigned char c = (unsigned char)raw[i];
        if (c == 0xA7)
        {
            if (i + 1 < raw.size())
                ++i;
            continue;
        }
        if (c == 0x0D || c == 0x0A)
            break;
        if (c < 32 || c > 126)
            continue;
        out.push_back((char)c);
    }
    return out;
}

// § en UTF-8: el juego valida el encoding y un 0xA7 suelto es invalido
// (el renderer descarta el string completo). Cada salto de color son
// 3 bytes: C2 A7 + code.
constexpr char kColorPrefix[3] = {'\xC2', '\xA7', '\0'};

// Longitud de una secuencia UTF-8 a partir del byte lider.
size_t utf8SeqLen(unsigned char lead)
{
    if (lead < 0x80)
        return 1;
    if ((lead & 0xE0) == 0xC0)
        return 2;
    if ((lead & 0xF0) == 0xE0)
        return 3;
    if ((lead & 0xF8) == 0xF0)
        return 4;
    return 1;
}

// Trunca por bytes sin cortar una secuencia UTF-8 a la mitad (un §
// truncado dejaria el nombre invisible otra vez).
std::string utf8SafeTruncate(std::string s, size_t budget)
{
    if (s.size() <= budget)
        return s;
    size_t i = 0;
    while (i < s.size())
    {
        size_t step = utf8SeqLen((unsigned char)s[i]);
        if (i + step > budget || i + step > s.size())
            break;
        i += step;
    }
    s.resize(i);
    return s;
}

// Letras visibles: sin secuencias de color (§ UTF-8 o legacy), sin '&' que
// pueda iniciar un code y sin bytes UTF-8 huerfanos.
std::string visibleLetters(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        unsigned char c = (unsigned char)raw[i];
        if (c == 0xC2)
        {
            if (i + 1 < raw.size() && (unsigned char)raw[i + 1] == 0xA7)
            {
                i += 2; // § + code
                continue;
            }
            if (i + 1 < raw.size() && (unsigned char)raw[i + 1] >= 0x80)
            {
                out.push_back((char)c);
                out.push_back((char)raw[i + 1]);
                ++i;
                continue;
            }
            continue; // C2 huerfano
        }
        if (c == 0xA7)
        {
            ++i; // legacy + code
            continue;
        }
        if (c == '&' || (c >= 0x80 && c < 0xC0))
            continue;
        out.push_back((char)c);
    }
    return out;
}

const std::vector<std::string>& colorNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v;
        v.reserve(16);
        for (const MCColor& c : kColors)
            v.emplace_back(c.name);
        return v;
    }();
    return names;
}

} // namespace

NameProtect::NameProtect()
    : Module("NameProtect", "Shows a fake name instead of your real one (streaming friendly)",
             Category::Visuals, 0)
{
    markHasSettings();
}

void NameProtect::resetState()
{
    actor_ = 0;
    realName_.clear();
    realPlain_.clear();
    appliedFake_.clear();
}

void NameProtect::restore()
{
    if (actor_ && !realName_.empty() && Game::get().localAddr() == actor_)
        writeGameString(actor_ + off::player::Name, realName_);
    resetState();
}

void NameProtect::onDisable()
{
    restore();
}

void NameProtect::onShutdown()
{
    restore();
}

std::string NameProtect::generateStyled(size_t budget) const
{
    if (budget < 1)
        budget = 15;

    if (style_ == kCustom)
    {
        std::string out;
        out.reserve(custom_.size() + 4);
        for (char c : custom_)
        {
            if (c == '&')
                out.append(kColorPrefix, 2);
            else
                out.push_back(c);
        }
        return utf8SafeTruncate(std::move(out), budget);
    }

    std::string letters = visibleLetters(name_);

    if (style_ == kPlain || letters.empty())
        return utf8SafeTruncate(std::move(letters), budget);

    if (style_ == kSolid)
    {
        letters = utf8SafeTruncate(std::move(letters), budget > 3 ? budget - 3 : 0);
        std::string out(kColorPrefix, 2);
        out += codeFor(color_);
        out += letters;
        return out;
    }

    // Gradient: cada salto de color son 3 bytes (§ UTF-8 + code). Con el
    // presupuesto disponible se calcula cuantos saltos caben y se degrada
    // con elegancia (menos segmentos -> solid -> plano).
    size_t len = letters.size();
    size_t maxSeg = (len + 3 <= budget) ? (budget - len) / 3 : 0;
    if (maxSeg == 0)
        return utf8SafeTruncate(std::move(letters), budget);

    // Glifos UTF-8 completos: nunca se colorea a mitad de un caracter.
    std::vector<std::string> glyphs;
    for (size_t i = 0; i < letters.size();)
    {
        size_t step = utf8SeqLen((unsigned char)letters[i]);
        if (step > letters.size() - i)
            step = 1;
        glyphs.push_back(letters.substr(i, step));
        i += step;
    }

    const MCColor& from = kColors[clampColorIdx(gradFrom_)];
    const MCColor& to = kColors[clampColorIdx(gradTo_)];

    // Color por glifo: interpolacion RGB de from a to, cuantizada a la
    // paleta MC.
    size_t n = glyphs.size();
    std::vector<char> codes(n);
    for (size_t i = 0; i < n; ++i)
    {
        float t = (n == 1) ? 0.0f : (float)i / (float)(n - 1);
        float r = (float)from.r + ((float)to.r - (float)from.r) * t;
        float g = (float)from.g + ((float)to.g - (float)from.g) * t;
        float b = (float)from.b + ((float)to.b - (float)from.b) * t;
        codes[i] = codeFor(nearestColor(r, g, b));
    }

    // Fusiona glifos consecutivos del mismo color en segmentos.
    std::vector<std::pair<char, std::string>> runs;
    for (size_t i = 0; i < n; ++i)
    {
        if (!runs.empty() && runs.back().first == codes[i])
            runs.back().second += glyphs[i];
        else
            runs.push_back({codes[i], glyphs[i]});
    }

    // Si sobran segmentos, fusiona el par adyacente con colores mas
    // cercanos (el cambio visual menos perceptible primero).
    while (runs.size() > maxSeg)
    {
        size_t best = 0;
        float bestD = FLT_MAX;
        for (size_t i = 0; i + 1 < runs.size(); ++i)
        {
            const MCColor& a = kColors[codeIdx(runs[i].first)];
            const MCColor& b = kColors[codeIdx(runs[i + 1].first)];
            float dr = (float)a.r - (float)b.r;
            float dg = (float)a.g - (float)b.g;
            float db = (float)a.b - (float)b.b;
            float d = dr * dr + dg * dg + db * db;
            if (d < bestD)
            {
                bestD = d;
                best = i;
            }
        }
        runs[best].second += runs[best + 1].second;
        runs.erase(runs.begin() + best + 1);
    }

    std::string out;
    out.reserve(len + runs.size() * 3);
    for (const auto& run : runs)
    {
        out.append(kColorPrefix, 2);
        out += run.first;
        out += run.second;
    }
    return out;
}

void NameProtect::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
    {
        resetState();
        return;
    }

    LocalPlayer local = game.localPlayer();
    uintptr_t nameAddr = local.address() + off::player::Name;
    if (!mem::isReadable(nameAddr, 0x20))
        return;

    // Presupuesto real: 15 en SSO o la capacidad del buffer heap actual
    // (nombres largos del servidor dan room extra para el gradient).
    uint64_t cap = mem::read<uint64_t>(nameAddr + 0x18);
    size_t budget = (cap >= 16 && cap < 0x1000) ? (size_t)cap : 15;

    std::string fake = generateStyled(budget);
    if (fake.empty())
        return;
    std::string fakePlain = plainName(fake);

    std::string current = mem::readString(nameAddr);
    if (!current.empty() && current != fake && current != appliedFake_ &&
        plainName(current) != fakePlain)
    {
        actor_ = local.address();
        realName_ = current;
        realPlain_ = plainName(current);
        appliedFake_.clear();
    }

    if (realName_.empty())
        return;

    if (current != fake)
    {
        if (writeGameString(nameAddr, fake))
            appliedFake_ = fake;
    }

    if (protectChat_ && !realPlain_.empty() && realPlain_ != fakePlain)
    {
        // Escaneo cada 2 ticks: el VirtualQuery de isReadable sobre ~128
        // entradas no es gratis y 100ms de latencia en el reemplazo es
        // imperceptible frente a la velocidad del chat.
        if (++chatSkip_ >= 2)
        {
            chatSkip_ = 0;
            protectChat(fake);
        }
    }
    else
    {
        chatSkip_ = 0;
    }
}

void NameProtect::protectChat(const std::string& fake)
{
    Chat chat = Game::get().chat();
    if (!chat.valid())
        return;

    uintptr_t begin = mem::read<uintptr_t>(chat.address() + off::chain::ChatMessages);
    uintptr_t end = mem::read<uintptr_t>(chat.address() + off::chain::ChatMessages + 8);
    if (!begin || end <= begin)
        return;

    size_t count = (end - begin) / off::chat::MessageSize;
    if (count > 128)
        count = 128;

    for (size_t i = 0; i < count; ++i)
    {
        uintptr_t entry = begin + i * off::chat::MessageSize;
        replaceInGameString(entry + off::chat::MsgSourceName, realPlain_, fake);
        replaceInGameString(entry + off::chat::MsgVariant, realPlain_, fake);
        replaceInGameString(entry + off::chat::MsgText, realPlain_, fake);
    }
}

void NameProtect::drawSettings()
{
    char buf[16] = {};
    size_t n = name_.size() < 15 ? name_.size() : 15;
    memcpy(buf, name_.c_str(), n);
    ImGui::SetNextItemWidth(180.0f);
    if (widgets::InputText("Name##np", buf, sizeof(buf)))
        name_.assign(buf);

    static const std::vector<std::string> kStyles = {"Plain", "Solid", "Gradient", "Custom"};
    widgets::CCombo("Style##np", &style_, kStyles);

    if (style_ == kSolid)
    {
        widgets::CCombo("Color##np", &color_, colorNames());
    }
    else if (style_ == kGradient)
    {
        widgets::CCombo("From##np", &gradFrom_, colorNames());
        ImGui::SameLine();
        widgets::CCombo("To##np", &gradTo_, colorNames());
    }
    else if (style_ == kCustom)
    {
        char cbuf[16] = {};
        size_t cn = custom_.size() < 15 ? custom_.size() : 15;
        memcpy(cbuf, custom_.c_str(), cn);
        ImGui::SetNextItemWidth(180.0f);
        if (widgets::InputText("Codes##np", cbuf, sizeof(cbuf)))
            custom_.assign(cbuf);
        ImGui::TextDisabled("Use & as color code (e.g. &c&lName)");
    }

    widgets::Checkbox("Replace in chat##np", &protectChat_);

    // Preview coloreada del resultado final (respeta el recorte).
    std::string styled = generateStyled(15);
    if (styled.empty())
        styled = name_;
    ImGui::TextDisabled("Preview:");
    ImGui::SameLine();
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float x = p.x;
        ImU32 col = IM_COL32(255, 255, 255, 255);
        std::string run;
        auto flush = [&]()
        {
            if (run.empty())
                return;
            dl->AddText(ImVec2(x, p.y), col, run.c_str());
            x += ImGui::CalcTextSize(run.c_str()).x;
            run.clear();
        };
        for (size_t i = 0; i < styled.size(); ++i)
        {
            unsigned char c = (unsigned char)styled[i];
            if (c == 0xC2 && i + 2 < styled.size() && (unsigned char)styled[i + 1] == 0xA7)
            {
                flush();
                const MCColor& mc = kColors[codeIdx(styled[i + 2])];
                col = IM_COL32(mc.r, mc.g, mc.b, 255);
                i += 2;
            }
            else if (c == 0xA7 && i + 1 < styled.size())
            {
                // Legacy de 1 byte (por si un config viejo lo trae).
                flush();
                const MCColor& mc = kColors[codeIdx(styled[i + 1])];
                col = IM_COL32(mc.r, mc.g, mc.b, 255);
                ++i;
            }
            else
            {
                run.push_back((char)c);
            }
        }
        flush();
        float w = x - p.x;
        if (w < 4.0f)
            w = ImGui::CalcTextSize(" ").x;
        ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
    }
    ImGui::TextDisabled("Budget: 15 bytes total, each color change costs 3");
}

nlohmann::json NameProtect::saveSettings() const
{
    return nlohmann::json{
        {"name", name_},
        {"style", style_},
        {"color", color_},
        {"gradFrom", gradFrom_},
        {"gradTo", gradTo_},
        {"custom", custom_},
        {"protectChat", protectChat_},
    };
}

void NameProtect::loadSettings(const nlohmann::json& j)
{
    // "fakeName" era la unica clave antes de los estilos.
    if (j.contains("fakeName") && !j.contains("name"))
        name_ = plainName(j.value("fakeName", name_));
    name_ = j.value("name", name_);
    if (name_.size() > 15)
        name_.resize(15);

    style_ = j.value("style", style_);
    if (style_ < kPlain || style_ > kCustom)
        style_ = kPlain;
    color_ = clampColorIdx(j.value("color", color_));
    gradFrom_ = clampColorIdx(j.value("gradFrom", gradFrom_));
    gradTo_ = clampColorIdx(j.value("gradTo", gradTo_));

    custom_ = j.value("custom", custom_);
    if (custom_.size() > 15)
        custom_.resize(15);

    protectChat_ = j.value("protectChat", protectChat_);
}

MC_REGISTER_MODULE(NameProtect);

}
