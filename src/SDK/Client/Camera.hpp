#pragma once

#include "Framework/Math.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "Features/Smoothing.hpp"
#include "SDK/Client/Client.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <windows.h>
#include <imgui.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace mc {

// Definida mas abajo (lee options.txt). Se declara aqui porque computeView
// construye la proyeccion.
inline float readGameFov(float fallback = 110.0f);

// ============================================================================
// Camara del SDK — view/proj con GLM.
//
// Convencion del juego (la misma de worldToScreen): yaw 0 mira a +Z, pitch
// positivo mira hacia abajo.
//   forward = (-sinY*cosP, -sinP,  cosY*cosP)
//   right   = (-cosY,       0,    -sinY)
//   up      = (-sinY*sinP,  cosP,  cosY*sinP)
// La view se monta en convencion OpenGL (row2 = -forward) para poder usar
// glm::perspective y proyectar con clip = proj * view * (p,1).
// ============================================================================

// Estado de la camara compartida. Es LOGICA, no un modulo: todos los
// overlays (ESP, NameTags, Tracers, Backtrack, ...) leen la misma camara y
// quedan pegados al modelo del jugador sin configuracion por modulo.
namespace cam {

// Offset de camara de primera persona desde LevelRenderer+0x2548 (view
// bobbing). ON por defecto: sin el, la camara de los overlays queda fija
// mientras el render se mueve al caminar/correr y las cajas flotan. La
// magnitud se clampea (kMaxFirstPersonOffset) para que un Vec4 con basura
// del build no pueda desplazar la vista mas de un cuarto de bloque.
inline std::atomic<bool> g_useFirstPersonOffset{true};
// Escala del offset (1.0 = tal cual lo reporta el renderer).
inline std::atomic<float> g_firstPersonScale{1.0f};
constexpr float kMaxFirstPersonOffset = 0.35f;

// Shake de camara (bloques). API para cualquier logica que quiera sacudir
// la proyeccion (dano, impacto, landing):
//   cam::shake(0.10f, 250.0);
// Solo afecta a los overlays; no escribe la camara del render.
inline std::atomic<float> g_shakeAmp{0.0f};
inline std::atomic<double> g_shakeUntilMs{0.0};

// Lineas de diagnostico [CamDbg]. Se auto-arman una vez por sesion para
// dejar evidencia del Vec4 en el log sin UI.
inline std::atomic<int> g_probeLines{60};

inline void probe(int lines)
{
    g_probeLines.store(lines, std::memory_order_relaxed);
}

// Sacude la proyeccion de los overlays durante `ms`. Es la unica entrada
// publica: ningun modulo necesita configurar nada para que la camara calce.
inline void shake(float ampBlocks, double ms)
{
    g_shakeAmp.store(ampBlocks, std::memory_order_relaxed);
    g_shakeUntilMs.store((double)GetTickCount64() + ms, std::memory_order_relaxed);
}

} // namespace cam

struct CameraView
{
    Vec3 pos;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fovDeg = 70.0f;
    float aspect = 16.0f / 9.0f;
    float screenW = 1920.0f;
    float screenH = 1080.0f;

    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};

    // Proyeccion mundo -> pantalla. `out.z` = profundidad en view space
    // (delante > 0), igual que el worldToScreen clasico.
    bool project(const Vec3& world, Vec3& out) const
    {
        const glm::vec4 clip = proj * (view * glm::vec4(world.x, world.y, world.z, 1.0f));
        if (!(clip.w > 0.1f) || !std::isfinite(clip.w))
            return false;

        const float invW = 1.0f / clip.w;
        const float ndcX = clip.x * invW;
        const float ndcY = clip.y * invW;
        if (!std::isfinite(ndcX) || !std::isfinite(ndcY))
            return false;

        out.x = (ndcX * 0.5f + 0.5f) * screenW;
        out.y = (0.5f - ndcY * 0.5f) * screenH;
        out.z = clip.w; // profundidad delante de la camara
        return true;
    }
};

namespace cam::detail {

inline double nowMs()
{
    static LARGE_INTEGER freq{};
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

// View matrix en convencion GL (row2 = -forward). Ver cabecera.
inline glm::mat4 buildView(const Vec3& eye, float yawDeg, float pitchDeg)
{
    const float yR = deg2rad(yawDeg);
    const float pR = deg2rad(pitchDeg);
    const float cy = std::cos(yR), sy = std::sin(yR);
    const float cp = std::cos(pR), sp = std::sin(pR);

    const glm::vec3 f{-sy * cp, -sp, cy * cp};
    const glm::vec3 r{-cy, 0.0f, -sy};
    const glm::vec3 u{-sy * sp, cp, cy * sp};
    const glm::vec3 e{eye.x, eye.y, eye.z};

    glm::mat4 m(1.0f);
    // glm es column-major: m[columna][fila].
    m[0][0] = r.x; m[1][0] = r.y; m[2][0] = r.z; m[3][0] = -glm::dot(r, e);
    m[0][1] = u.x; m[1][1] = u.y; m[2][1] = u.z; m[3][1] = -glm::dot(u, e);
    m[0][2] = -f.x; m[1][2] = -f.y; m[2][2] = -f.z; m[3][2] = glm::dot(f, e);
    return m;
}

inline void probeLog(const CameraView& cv, const Vec4& raw, float rawLen, bool thirdPerson)
{
    if (cam::g_probeLines.load(std::memory_order_relaxed) <= 0)
        return;
    cam::g_probeLines.fetch_sub(1, std::memory_order_relaxed);
    MC_LOG("[CamDbg] raw=(%.4f, %.4f, %.4f, %.4f) len=%.4f mode=%s "
           "cam=(%.3f,%.3f,%.3f) rot=(%.2f,%.2f)",
           raw.x, raw.y, raw.z, raw.w, rawLen, thirdPerson ? "3p" : "1p",
           cv.pos.x, cv.pos.y, cv.pos.z, cv.yaw, cv.pitch);
}

} // namespace cam::detail

inline CameraView computeView(const LocalPlayer& local, const Client& client,
                              float eyeHeight, float screenW = 1920.0f,
                              float screenH = 1080.0f)
{
    CameraView cam;
    cam.screenW = screenW;
    cam.screenH = screenH;

    // El ojo esta sobre el CENTRO horizontal del player (mPosition +0x0C) y
    // a la altura de los PIES (AABB min +0xB4.y) + eyeHeight.
    const Vec3 feet = local.pos();
    const Vec3 exact = local.posExact();
    Vec3 base{exact.x, feet.y, exact.z};
    base = smooth::interp(smooth::channel::Camera, base);

    cam.pos = {base.x, base.y + eyeHeight, base.z};
    cam.yaw = local.yaw();
    cam.pitch = local.pitch();

    // --- Offset de camara del renderer (3ª persona + bob) ---
    // s_bias = media movil del Vec4 en 1ª persona. Solo la componente
    // oscilante es bob de vista; lo constante es ruido/estructura y se anula.
    static Vec3 s_bias{0.0f, 0.0f, 0.0f};
    static bool s_biasInit = false;
    Vec4 raw{};
    float rawLen = 0.0f;
    bool thirdPerson = false;
    if (client.valid())
    {
        const uintptr_t lr = client.rendererAddr();
        if (lr && mem::rawRead(&raw, lr + off::chain::RendererCamera, sizeof(Vec4)))
        {
            const float len = std::sqrt(raw.x * raw.x + raw.y * raw.y + raw.z * raw.z);
            rawLen = len;
            const float pullBack = raw.w;

            if (pullBack >= 0.5f && pullBack <= 10.0f && len >= 0.5f)
            {
                // 3ª persona: el renderer reporta direccion + distancia de
                // orbita. Se reconstruye la posicion de la camara.
                thirdPerson = true;
                s_biasInit = false;
                const float yR = deg2rad(cam.yaw), pR = deg2rad(cam.pitch);
                const float cy = std::cos(yR), sy = std::sin(yR);
                const float cp = std::cos(pR), sp = std::sin(pR);
                const float fx = -sy * cp, fy = -sp, fz = cy * cp;
                const float vx = raw.x / len, vy = raw.y / len, vz = raw.z / len;
                const float dt = vx * fx + vy * fy + vz * fz;

                if (dt >= 0.0f)
                {
                    cam.pos = {cam.pos.x - fx * pullBack, cam.pos.y - fy * pullBack,
                               cam.pos.z - fz * pullBack};
                }
                else
                {
                    cam.pos = {cam.pos.x + fx * pullBack, cam.pos.y + fy * pullBack,
                               cam.pos.z + fz * pullBack};
                    cam.yaw += 180.0f;
                    cam.pitch = -cam.pitch;
                }
            }
            else if (cam::g_useFirstPersonOffset.load(std::memory_order_relaxed) &&
                     len > 0.0005f && len <= 0.5f)
            {
                // 1ª persona. Sin este offset la camara de los overlays queda
                // fija mientras el render se mueve (bob/sprint) y las cajas
                // flotan respecto al modelo.
                //
                // Un offset de vista de 1ª persona nunca mide ~1.0 (eso seria
                // una direccion normalizada): con len <= 0.5 el campo es un
                // desplazamiento. De ese desplazamiento se aplica solo la
                // componente oscilante (raw - media movil): si el campo trae
                // un termino estructural constante, el resultado es ~0 y la
                // vista no se mueve. El clamp es la ultima red de seguridad.
                if (!s_biasInit)
                {
                    s_bias = {raw.x, raw.y, raw.z};
                    s_biasInit = true;
                }
                else
                {
                    constexpr float kAlpha = 0.02f;
                    s_bias.x += (raw.x - s_bias.x) * kAlpha;
                    s_bias.y += (raw.y - s_bias.y) * kAlpha;
                    s_bias.z += (raw.z - s_bias.z) * kAlpha;
                }

                const float k = cam::g_firstPersonScale.load(std::memory_order_relaxed);
                float ox = (raw.x - s_bias.x) * k;
                float oy = (raw.y - s_bias.y) * k;
                float oz = (raw.z - s_bias.z) * k;
                const float mag = std::sqrt(ox * ox + oy * oy + oz * oz);
                if (mag > cam::kMaxFirstPersonOffset)
                {
                    const float s = cam::kMaxFirstPersonOffset / mag;
                    ox *= s; oy *= s; oz *= s;
                }
                cam.pos = {cam.pos.x + ox, cam.pos.y + oy, cam.pos.z + oz};
            }
            else
            {
                s_biasInit = false;
            }
        }
    }

    // --- Shake de camara (solo proyeccion de los overlays) ---
    const float shakeAmp = cam::g_shakeAmp.load(std::memory_order_relaxed);
    const double until = cam::g_shakeUntilMs.load(std::memory_order_relaxed);
    const double t = (double)GetTickCount64();
    if (shakeAmp > 0.0f && t < until)
    {
        const double secs = t * 0.001;
        const float k = clampf((float)((until - t) / 250.0), 0.0f, 1.0f);
        cam.pos.x += std::sin((float)secs * 61.0f) * shakeAmp * k;
        cam.pos.y += std::sin((float)secs * 47.0f) * shakeAmp * k;
        cam.pos.z += std::cos((float)secs * 53.0f) * shakeAmp * k;
    }

    // --- Matrices ---
    float fov = readGameFov();
    cam.fovDeg = fov;
    cam.aspect = (screenH > 1.0f) ? (screenW / screenH) : (16.0f / 9.0f);

    const float effectiveFov = clampf(fov * zoomFovFactor(), 1.0f, 179.0f);
    cam.view = cam::detail::buildView(cam.pos, cam.yaw, cam.pitch);
    cam.proj = glm::perspective(deg2rad(effectiveFov), cam.aspect, 0.05f, 2048.0f);

    cam::detail::probeLog(cam, raw, rawLen, thirdPerson);
    return cam;
}

inline CameraView getCameraView(const LocalPlayer& local, const Client& client,
                                float eyeHeight = off::gameconst::EyeHeight)
{
    static int s_frame = -1;
    static CameraView s_cached{};
    static ULONGLONG s_lastGc = 0;

    const ImGuiIO& io = ImGui::GetIO();
    const int frame = ImGui::GetFrameCount();
    if (frame == s_frame)
        return s_cached;

    CameraView cam = computeView(local, client, eyeHeight, io.DisplaySize.x, io.DisplaySize.y);

    ULONGLONG now = GetTickCount64();
    if (now - s_lastGc > 2000)
    {
        s_lastGc = now;
        smooth::gc();
    }

    s_cached = cam;
    s_frame = frame;
    return cam;
}

inline float readGameFov(float fallback)
{
    static float cached = 0.0f;
    static ULONGLONG lastCheck = 0;
    ULONGLONG now = GetTickCount64();
    if (cached != 0.0f && (now - lastCheck) < 3000)
        return cached;
    lastCheck = now;

    auto tryFile = [](const char* path) -> float {
        FILE* fp = nullptr;
#ifdef _MSC_VER
        if (fopen_s(&fp, path, "r") != 0 || !fp)
            return 0.0f;
#else
        fp = std::fopen(path, "r");
        if (!fp)
            return 0.0f;
#endif
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
