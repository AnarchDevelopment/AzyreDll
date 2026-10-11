#include "ColorUtils.hpp"

#include <windows.h>

#include <cmath>

namespace mc::colors {

namespace {

int g_theme = 4;          // Sunset: el look clasico de Solstice
float g_speed = 3.0f;
float g_saturation = 1.0f;

struct RGB
{
    float r, g, b;
};

// Paletas exactas de Interface.hpp (Solstice-Source).
const RGB kTrans[] = {
    {91.0f / 255.0f, 206.0f / 255.0f, 250.0f / 255.0f},
    {245.0f / 255.0f, 169.0f / 255.0f, 184.0f / 255.0f},
    {1.0f, 1.0f, 1.0f},
    {245.0f / 255.0f, 169.0f / 255.0f, 184.0f / 255.0f},
};
const RGB kBubblegum[] = {
    {255.0f / 255.0f, 99.0f / 255.0f, 202.0f / 255.0f},
    {255.0f / 255.0f, 195.0f / 255.0f, 195.0f / 255.0f},
    {146.0f / 255.0f, 245.0f / 255.0f, 255.0f / 255.0f},
    {249.0f / 255.0f, 255.0f / 255.0f, 148.0f / 255.0f},
    {135.0f / 255.0f, 255.0f / 255.0f, 176.0f / 255.0f},
};
const RGB kWatermelon[] = {
    {255.0f / 255.0f, 70.0f / 255.0f, 70.0f / 255.0f},
    {139.0f / 255.0f, 0.0f, 0.0f},
    {144.0f / 255.0f, 238.0f / 255.0f, 144.0f / 255.0f},
    {34.0f / 255.0f, 139.0f / 255.0f, 34.0f / 255.0f},
    {204.0f / 255.0f, 255.0f / 255.0f, 204.0f / 255.0f},
};
const RGB kSunset[] = {
    {213.0f / 255.0f, 32.0f / 255.0f, 0.0f},
    {239.0f / 255.0f, 118.0f / 255.0f, 39.0f / 255.0f},
    {255.0f / 255.0f, 154.0f / 255.0f, 86.0f / 255.0f},
    {1.0f, 1.0f, 1.0f},
    {209.0f / 255.0f, 98.0f / 255.0f, 164.0f / 255.0f},
    {181.0f / 255.0f, 86.0f / 255.0f, 144.0f / 255.0f},
};
const RGB kPoison[] = {
    {115.0f / 255.0f, 222.0f / 255.0f, 70.0f / 255.0f},
    {67.0f / 255.0f, 201.0f / 255.0f, 89.0f / 255.0f},
    {41.0f / 255.0f, 230.0f / 255.0f, 94.0f / 255.0f},
    {12.0f / 255.0f, 210.0f / 255.0f, 83.0f / 255.0f},
    {87.0f / 255.0f, 211.0f / 255.0f, 72.0f / 255.0f},
    {57.0f / 255.0f, 210.0f / 255.0f, 124.0f / 255.0f},
};

const RGB* palette(int theme, int& count)
{
    switch (theme)
    {
    case 0: count = 4; return kTrans;
    case 2: count = 5; return kBubblegum;
    case 3: count = 5; return kWatermelon;
    case 5: count = 6; return kPoison;
    case 4:
    default: count = 6; return kSunset;
    }
}

// Tiempo de sesion en ms (precision float estable, como NOW relativo).
float sessionMs()
{
    static ULONGLONG t0 = GetTickCount64();
    return (float)(GetTickCount64() - t0);
}

void hsvToRgb(float h, float s, float v, float& r, float& g, float& b)
{
    int hi = (int)(h * 6.0f) % 6;
    float f = h * 6.0f - (float)(int)(h * 6.0f);
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (hi)
    {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
}

} // namespace

int theme() { return g_theme; }
void setTheme(int t) { g_theme = (t < 0 || t > 5) ? 4 : t; }
float colorSpeed() { return g_speed; }
void setColorSpeed(float v) { g_speed = v; }
float saturation() { return g_saturation; }
void setSaturation(float v) { g_saturation = v; }

ImU32 getThemedColor(float index, float alpha)
{
    float speed = g_speed;
    if (speed < 0.01f)
        speed = 0.01f;
    float now = sessionMs();

    float r, g, b;

    if (g_theme == 1)
    {
        // ColorUtils::Rainbow: hue = ((NOW + index) % (speed*1000)) / (speed*1000)
        float cycle = speed * 1000.0f;
        float hue = fmodf(now + index, cycle);
        if (hue < 0.0f)
            hue += cycle;
        hue /= cycle;
        hsvToRgb(hue, g_saturation, 1.0f, r, g, b);
    }
    else
    {
        // ColorUtils::LerpColors: ciclo de 10000/speed ms sobre la paleta,
        // fase desplazada por el index.
        int count = 0;
        const RGB* pal = palette(g_theme, count);
        float time = 10000.0f / speed;
        float angle = fmodf(now + index, time);
        if (angle < 0.0f)
            angle += time;
        float segTime = time / (float)count;
        int seg = (int)(angle / segTime);
        if (seg >= count)
            seg = count - 1;
        float f = angle / segTime - (float)seg;
        const RGB& a = pal[seg];
        const RGB& b2 = pal[(seg + 1) % count];
        r = a.r + (b2.r - a.r) * f;
        g = a.g + (b2.g - a.g) * f;
        b = a.b + (b2.b - a.b) * f;
    }

    return IM_COL32((int)(r * 255.0f), (int)(g * 255.0f), (int)(b * 255.0f),
                    (int)(alpha * 255.0f));
}

} // namespace
