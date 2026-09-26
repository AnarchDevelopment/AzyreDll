#include "Sound.hpp"

#include "Framework/Log.hpp"
#include "resource.h"

#include <soloud.h>
#include <soloud_wav.h>

#include <windows.h>

namespace mc::sound {

namespace {

HMODULE g_module = nullptr;
bool g_ready = false;
SoLoud::Soloud g_soloud;
SoLoud::Wav g_wavOn;
SoLoud::Wav g_wavOff;

bool loadResourceSound(SoLoud::Wav& wav, int resourceId, const char* name)
{
    if (!g_module)
    {
        MC_LOG_ERROR("[Sound] Module handle not set");
        return false;
    }

    HRSRC res = FindResourceW(g_module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!res)
    {
        MC_LOG_ERROR("[Sound] %s resource not found", name);
        return false;
    }

    HGLOBAL hg = LoadResource(g_module, res);
    DWORD size = SizeofResource(g_module, res);
    void* data = LockResource(hg);
    if (!data || !size)
    {
        MC_LOG_ERROR("[Sound] Failed to lock %s", name);
        return false;
    }

    // aCopy=false: la memoria del recurso vive mientras la DLL este cargada.
    if (wav.loadMem((unsigned char*)data, (unsigned int)size, false, false) !=
        SoLoud::SO_NO_ERROR)
    {
        MC_LOG_ERROR("[Sound] %s: loadMem failed", name);
        return false;
    }

    MC_LOG("[Sound] %s loaded (%lu bytes)", name, size);
    return true;
}

}

void setModule(void* moduleHandle)
{
    g_module = static_cast<HMODULE>(moduleHandle);
}

bool init()
{
    if (g_ready)
        return true;

    int err = g_soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF);
    if (err != SoLoud::SO_NO_ERROR)
    {
        MC_LOG_ERROR("[Sound] Soloud init failed: %d", err);
        return false;
    }

    bool on = loadResourceSound(g_wavOn, IDR_CELESTIAL_ON, "celestial_on");
    bool off = loadResourceSound(g_wavOff, IDR_CELESTIAL_OFF, "celestial_off");

    if (!on && !off)
    {
        g_soloud.deinit();
        MC_LOG_ERROR("[Sound] No sounds loaded - sound system disabled");
        return false;
    }

    g_ready = true;
    MC_LOG("[SUCCESS] SoLoud ready (winmm backend)");
    return true;
}

void shutdown()
{
    if (!g_ready)
        return;
    g_soloud.deinit();
    g_ready = false;
    MC_LOG("[Sound] Shut down");
}

void playOn()
{
    if (!g_ready || !g_wavOn.mSampleCount)
        return;
    g_soloud.setVolume(g_soloud.play(g_wavOn), 0.6f);
}

void playOff()
{
    if (!g_ready || !g_wavOff.mSampleCount)
        return;
    g_soloud.setVolume(g_soloud.play(g_wavOff), 0.6f);
}

}
