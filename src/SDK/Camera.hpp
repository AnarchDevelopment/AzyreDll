#pragma once

#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Classes/Client.hpp"
#include "SDK/Classes/LocalPlayer.hpp"
#include "SDK/Offsets.hpp"

#include <windows.h>
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace mc {

struct CameraView
{
    Vec3 pos;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

inline CameraView computeView(const LocalPlayer& local, const Client& client, float eyeHeight)
{
    CameraView cam;
    Vec3 p = local.pos();
    cam.pos = {p.x, p.y + eyeHeight, p.z};
    cam.yaw = local.yaw();
    cam.pitch = local.pitch();

    if (!client.valid())
        return cam;

    uintptr_t lr = client.rendererAddr();
    if (!lr)
        return cam;

    Vec4 v{};
    if (!mem::rawRead(&v, lr + off::chain::RendererCamera, sizeof(Vec4)))
        return cam;

    float pullBack = v.w;
    if (pullBack < 0.5f || pullBack > 10.0f)
        return cam;

    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 0.5f)
        return cam;

    float yR = deg2rad(cam.yaw);
    float pR = deg2rad(cam.pitch);
    float cosY = std::cos(yR), sinY = std::sin(yR);
    float cosP = std::cos(pR), sinP = std::sin(pR);
    float pfx = -sinY * cosP;
    float pfy = -sinP;
    float pfz = cosY * cosP;

    float vx = v.x / len, vy = v.y / len, vz = v.z / len;
    float dot = vx * pfx + vy * pfy + vz * pfz;

    if (dot >= 0.0f)
    {
        cam.pos = {p.x - pfx * pullBack, p.y + eyeHeight - pfy * pullBack, p.z - pfz * pullBack};
    }
    else
    {
        cam.pos = {p.x + pfx * pullBack, p.y + eyeHeight + pfy * pullBack, p.z + pfz * pullBack};
        cam.yaw += 180.0f;
        cam.pitch = -cam.pitch;
    }
    return cam;
}

inline CameraView getCameraView(const LocalPlayer& local, const Client& client,
                                float eyeHeight = off::gameconst::EyeHeight)
{
    static int s_frame = -1;
    static bool s_init = false;
    static Vec3 s_prevRaw{};
    static Vec3 s_lastRaw{};
    static ULONGLONG s_changeMs = 0;
    static CameraView s_cached{};

    int frame = ImGui::GetFrameCount();
    if (frame == s_frame)
        return s_cached;

    CameraView cam = computeView(local, client, eyeHeight);
    Vec3 raw = cam.pos;
    ULONGLONG now = GetTickCount64();

    if (!s_init)
    {
        s_prevRaw = raw;
        s_lastRaw = raw;
        s_changeMs = now;
        s_init = true;
    }
    else if (raw.x != s_lastRaw.x || raw.y != s_lastRaw.y || raw.z != s_lastRaw.z)
    {
        s_prevRaw = s_lastRaw;
        s_lastRaw = raw;
        s_changeMs = now;
    }

    float t = (float)(now - s_changeMs) / 50.0f;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;

    cam.pos.x = s_prevRaw.x + (s_lastRaw.x - s_prevRaw.x) * t;
    cam.pos.y = s_prevRaw.y + (s_lastRaw.y - s_prevRaw.y) * t;
    cam.pos.z = s_prevRaw.z + (s_lastRaw.z - s_prevRaw.z) * t;

    s_cached = cam;
    s_frame = frame;
    return cam;
}

inline float readGameFov(float fallback = 110.0f)
{
    static float cached = 0.0f;
    static ULONGLONG lastCheck = 0;
    ULONGLONG now = GetTickCount64();
    if (cached != 0.0f && (now - lastCheck) < 3000)
        return cached;
    lastCheck = now;

    auto tryFile = [](const char* path) -> float {
        FILE* fp = nullptr;
        if (fopen_s(&fp, path, "r") != 0 || !fp)
            return 0.0f;
        char line[256];
        float found = 0.0f;
        while (fgets(line, sizeof(line), fp))
        {
            if (strncmp(line, "gfx_field_of_view:", 18) == 0)
            {
                float f = (float)atof(line + 18);
                if (f >= 30.0f && f <= 130.0f)
                {
                    found = f;
                    break;
                }
            }
        }
        fclose(fp);
        return found;
    };

    float result = 0.0f;

    char appdata[MAX_PATH] = {};
    if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH))
    {
        std::string base = std::string(appdata) + "\\.minecraft_bedrock\\installations";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA((base + "\\*").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.')
                {
                    std::string inst = base + "\\" + fd.cFileName;
                    WIN32_FIND_DATAA fd2;
                    HANDLE hFind2 = FindFirstFileA((inst + "\\*").c_str(), &fd2);
                    if (hFind2 != INVALID_HANDLE_VALUE)
                    {
                        do
                        {
                            if ((fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd2.cFileName[0] != '.')
                            {
                                std::string optPath = inst + "\\" + fd2.cFileName +
                                                      "\\packageData\\minecraftpe\\options.txt";
                                result = tryFile(optPath.c_str());
                                if (result)
                                {
                                    FindClose(hFind2);
                                    FindClose(hFind);
                                    cached = result;
                                    return cached;
                                }
                            }
                        } while (FindNextFileA(hFind2, &fd2));
                        FindClose(hFind2);
                    }
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }

    char localAppData[MAX_PATH] = {};
    if (GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH))
    {
        std::string uwpOpt = std::string(localAppData) +
                             "\\Packages\\Microsoft.MinecraftUWP_8wekyb3d8bbwe\\LocalState\\games\\com.mojang\\minecraftpe\\options.txt";
        result = tryFile(uwpOpt.c_str());
        if (result)
        {
            cached = result;
            return cached;
        }
    }

    return cached != 0.0f ? cached : fallback;
}

}
