#include "Config.hpp"

#include "Framework/Log.hpp"
#include "Modules/ModuleManager.hpp"

#ifdef _MSC_VER
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#endif

#include <nlohmann/json.hpp>
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <iterator>

namespace mc::config {

static bool g_bootSuppress = true;

static bool localAppDataPath(char* out, size_t outSize)
{
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)) && path)
    {
        WideCharToMultiByte(CP_ACP, 0, path, -1, out, (int)outSize, nullptr, nullptr);
        CoTaskMemFree(path);
        if (out[0])
            return true;
    }
    return GetEnvironmentVariableA("LOCALAPPDATA", out, (DWORD)outSize) > 0;
}

static std::string wideToNarrow(const wchar_t* w)
{
    if (!w || !*w)
        return {};
    int need = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1)
        return {};
    std::string out((size_t)need - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, w, -1, &out[0], need, nullptr, nullptr);
    return out;
}

// Base con el LocalState del paquete REAL del juego via WinRT: funciona con
// cualquier familia de paquete (release, Preview, betas) y esta garantizada
// como escribible desde el AppContainer. La ruta hardcodeada anterior fallaba
// silenciosamente si la familia del paquete no coincidia (nada se guardaba).
// Base con el LocalState del paquete REAL del juego via WinRT. IMPORTANTE:
// se computa UNA vez (config::init en el Bootstrap, hilo propio MTA) y se
// cachea. Llamar a WinRT desde el hilo de Present del juego corrompe su
// apartment COM y crashea al activar modulos.
static std::string g_baseDirCache;
static bool g_baseDirReady = false;

static std::string computeBaseDir()
{
#ifdef _MSC_VER
    try
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        catch (...)
        {
        }
        auto path = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
        std::string s = wideToNarrow(path.c_str());
        if (!s.empty())
        {
            MC_LOG("[Config] Base: %s", (s + "\\Azyre").c_str());
            return s + "\\Azyre";
        }
    }
    catch (...)
    {
        MC_LOG_ERROR("[Config] ApplicationData::LocalFolder is not available, using fallback...");
    }
#endif

    char local[512] = {};
    if (localAppDataPath(local, sizeof(local)))
        return std::string(local) +
               "\\Packages\\Microsoft.MinecraftUWP_8wekyb3d8bbwe\\RoamingState\\Azyre";
    return {};
}

static std::string configBaseDir()
{
    if (!g_baseDirReady)
    {
        g_baseDirCache = computeBaseDir();
        g_baseDirReady = true;
    }
    return g_baseDirCache;
}

void init()
{
    // Precalcular la ruta desde el hilo del Bootstrap (MTA seguro) y crear
    // la carpeta base.
    std::string base = configBaseDir();
    if (!base.empty())
        CreateDirectoryA(base.c_str(), nullptr);
}

static std::string configsDir()
{
    std::string base = configBaseDir();
    if (base.empty())
        return {};
    std::string configs = base + "\\Configs";
    CreateDirectoryA(base.c_str(), nullptr);
    CreateDirectoryA(configs.c_str(), nullptr);
    return configs;
}

static bool writeJsonFile(const std::string& path, const nlohmann::json& j)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out)
        return false;
    out << j.dump(4);
    return true;
}

static nlohmann::json readJsonFile(const std::string& path, bool& ok)
{
    ok = false;
    std::ifstream in(path);
    if (!in)
        return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    ok = j.is_object();
    return j;
}

// Todo el estado de todos los modulos en un solo objeto json.
static nlohmann::json captureAllModules()
{
    nlohmann::json root;
    for (auto& m : ModuleManager::get().all())
    {
        nlohmann::json e;
        e["enabled"] = m->enabled();
        e["keybind"] = m->keybind();
        nlohmann::json s = m->saveSettings();
        if (!s.is_null() && !s.empty())
            e["settings"] = s;
        root[m->name()] = e;
    }
    return root;
}

// SEH por módulo: aisla el crash de UN modulo para que los demas
// sigan cargando sus settings. Sin esto, un solo onEnable/loadSettings
// que explota tumba TODO el config.
static bool sehLoadModuleSettings(Module* m, const nlohmann::json& e)
{
    __try
    {
        m->setKeybind(e.value("keybind", m->keybind()));
        if (e.contains("settings") && e["settings"].is_object())
            m->loadSettings(e["settings"]);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MC_LOG_ERROR("[Config] CRASHED loading settings for %s - skipped", m->name().c_str());
        return false;
    }
}

static bool sehEnableModule(Module* m, const nlohmann::json& e)
{
    __try
    {
        bool en = e.value("enabled", false);
        if (en != m->enabled())
            m->setEnabled(en);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MC_LOG_ERROR("[Config] CRASHED enabling %s - skipped", m->name().c_str());
        return false;
    }
}

static size_t applyAllModules(const nlohmann::json& root)
{
    ModuleManager& mgr = ModuleManager::get();
    mgr.setQuiet(true);
    size_t applied = 0;

    // PASADA 1: settings de todos (aislado por modulo)
    for (auto& m : mgr.all())
    {
        if (!root.contains(m->name()))
            continue;
        const auto& e = root[m->name()];
        if (sehLoadModuleSettings(m.get(), e))
            ++applied;
    }

    // PASADA 2: habilitar (aislado por modulo)
    for (auto& m : mgr.all())
    {
        if (!root.contains(m->name()))
            continue;
        const auto& e = root[m->name()];
        sehEnableModule(m.get(), e);
    }

    mgr.setQuiet(false);
    return applied;
}

// Config base: UN solo archivo con todos los modulos (estilo clientes modernos).
static std::string defaultConfigPath()
{
    std::string dir = configsDir();
    if (dir.empty())
        return {};
    return dir + "\\default.json";
}

static std::string sanitizeName(const std::string& name)
{
    std::string out;
    for (char c : name)
    {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|')
            continue;
        out.push_back(c);
    }
    return out;
}

static std::string presetsDirPath()
{
    std::string base = configsDir();
    if (base.empty())
        return {};
    std::string presets = base + "\\Presets";
    CreateDirectoryA(presets.c_str(), nullptr);
    return presets;
}

std::vector<std::string> listPresets()
{
    std::vector<std::string> out;
    std::string dir = presetsDirPath();
    if (dir.empty())
        return out;

    std::string pattern = dir + "\\*.json";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::string fname = fd.cFileName;
            if (fname.size() > 5 && fname.compare(fname.size() - 5, 5, ".json") == 0)
                out.push_back(fname.substr(0, fname.size() - 5));
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return out;
}

bool savePreset(const std::string& name)
{
    std::string dir = presetsDirPath();
    std::string clean = sanitizeName(name);
    if (dir.empty() || clean.empty())
        return false;

    if (!writeJsonFile(dir + "\\" + clean + ".json", captureAllModules()))
        return false;
    MC_LOG("[Config] Config saved: %s", clean.c_str());
    return true;
}

bool loadPreset(const std::string& name)
{
    std::string dir = presetsDirPath();
    std::string clean = sanitizeName(name);
    if (dir.empty() || clean.empty())
        return false;

    bool ok = false;
    nlohmann::json root = readJsonFile(dir + "\\" + clean + ".json", ok);
    if (!ok)
        return false;

    size_t applied = applyAllModules(root);
    MC_LOG("[Config] Config loaded: %s (%zu modules)", clean.c_str(), applied);
    saveAll();
    return true;
}

bool deletePreset(const std::string& name)
{
    std::string dir = presetsDirPath();
    std::string clean = sanitizeName(name);
    if (dir.empty() || clean.empty())
        return false;

    std::string path = dir + "\\" + clean + ".json";
    BOOL ok = DeleteFileA(path.c_str());
    if (ok)
        MC_LOG("[Config] Preset deleted: %s", clean.c_str());
    return ok != 0;
}

std::string baseDir()
{
    return configBaseDir();
}

static std::string settingsFilePath()
{
    std::string dir = configsDir();
    if (dir.empty())
        return {};
    return dir + "\\settings.json";
}

std::string globalSetting(const std::string& key, const std::string& fallback)
{
    std::string path = settingsFilePath();
    if (path.empty())
        return fallback;

    bool ok = false;
    nlohmann::json root = readJsonFile(path, ok);
    if (!ok || !root.contains(key) || !root[key].is_string())
        return fallback;
    return root[key].get<std::string>();
}

void setGlobalSetting(const std::string& key, const std::string& value)
{
    std::string path = settingsFilePath();
    if (path.empty())
        return;

    bool ok = false;
    nlohmann::json root = readJsonFile(path, ok);
    if (!ok)
        root = nlohmann::json::object();
    root[key] = value;
    writeJsonFile(path, root);
}

void saveModule(const std::string& moduleName)
{
    (void)moduleName;
    saveAll();
}

void saveAll()
{
    if (g_bootSuppress || ModuleManager::get().isQuiet())
        return;

    std::string path = defaultConfigPath();
    if (path.empty())
        return;

    if (writeJsonFile(path, captureAllModules()))
        MC_LOG("[Config] Default config saved to %s", path.c_str());
}

void loadAll()
{
    std::string path = defaultConfigPath();
    if (!path.empty())
    {
        bool ok = false;
        nlohmann::json root = readJsonFile(path, ok);
        if (ok)
        {
            size_t loaded = applyAllModules(root);
            MC_LOG("[Config] Loaded %zu modules from %s", loaded, path.c_str());
        }
        else
        {
            MC_LOG("[Config] No default config found at %s", path.c_str());
        }
    }

    g_bootSuppress = false;
}

}
