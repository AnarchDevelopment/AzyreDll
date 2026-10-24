#include "BlurShaders.hpp"

#include <d3dcompiler.h>
#include <cstring>

namespace mc::blur::detail {

ID3D11VertexShader* g_blurVS = nullptr;
ID3D11PixelShader* g_blurPS = nullptr;
ID3D11PixelShader* g_copyPS = nullptr;
ID3D11PixelShader* g_compositePS = nullptr;
ID3D11PixelShader* g_glassPS = nullptr;
ID3D11PixelShader* g_motionBlurPS = nullptr;
ID3D11BlendState* g_blurBlend = nullptr;
ID3D11Buffer* g_blurCB = nullptr;
ID3D11SamplerState* g_blurSampler = nullptr;
ID3D11RasterizerState* g_blurRS = nullptr;
ID3D11RasterizerState* g_blurScissorRS = nullptr;
bool g_shadersOk = false;

} // namespace mc::blur::detail

namespace mc::blur::shaders {

using namespace detail;

// Triangulo fullscreen generado desde SV_VertexID: sin vertex buffer ni
// input layout.
static const char* kBlurVS = R"(
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    o.uv = uv;
    return o;
}
)";

// Gaussiano separable de 9 taps: dir (1,0)/(0,1) llega por cbuffer.
static const char* kBlurPS = R"(
Texture2D srcTex : register(t0);
SamplerState srcSamp : register(s0);
cbuffer BlurParams : register(b0)
{
    float4 TexelDir; // (1/w, 1/h, dirX, dirY)
}
static const float kWeights[5] =
{
    0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216
};
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
float4 PSMain(VSOut input) : SV_Target
{
    float2 off = TexelDir.zw * TexelDir.xy;
    float4 c = srcTex.Sample(srcSamp, input.uv) * kWeights[0];
    [unroll]
    for (int i = 1; i < 5; ++i)
    {
        float2 d = off * (float)i;
        c += srcTex.Sample(srcSamp, input.uv + d) * kWeights[i];
        c += srcTex.Sample(srcSamp, input.uv - d) * kWeights[i];
    }
    return c;
}
)";

// Copia fullscreen (downscales de la cadena).
static const char* kCopyPS = R"(
Texture2D srcTex : register(t0);
SamplerState srcSamp : register(s0);
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
float4 PSMain(VSOut input) : SV_Target
{
    return srcTex.Sample(srcSamp, input.uv);
}
)";

// Composite Mica (puerto del legacy ClickGUI::mainCompositePS): pinta la
// escena blureada DIRECTO al target de forma OPACA — desaturacion + tint
// neutro + gradiente de elevacion + grain de vidrio escarchado.
static const char* kCompositePS = R"(
Texture2D srcTex : register(t0);
SamplerState srcSamp : register(s0);
cbuffer BlurParams : register(b0)
{
    float4 TexelDir;     // (1/w, 1/h, dirX, dirY) — gauss
    float4 TintOpacity;  // (tintR, tintG, tintB, micaOpacity) — composite
}
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
static float hash2(float2 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}
static float valueNoise(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash2(i);
    float b = hash2(i + float2(1.0, 0.0));
    float c = hash2(i + float2(0.0, 1.0));
    float d = hash2(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
float4 PSComposite(VSOut input) : SV_Target
{
    float3 col = srcTex.Sample(srcSamp, input.uv).rgb;

    // Mica frosted desaturation: neutro con un toque del color original
    float luma = dot(col, float3(0.299, 0.587, 0.114));
    col = lerp(float3(luma, luma, luma), col, 0.82);

    // Base Mica neutra oscurecida por el tint
    col = lerp(col, TintOpacity.rgb, TintOpacity.a);

    // Gradiente de elevacion sutil: leve brillo hacia arriba
    col += float3(0.004, 0.004, 0.005) * (1.0 - input.uv.y);

    // Grain de vidrio escarchado, fino y apenas perceptible
    float2 grain = input.pos.xy / 192.0;
    col += (valueNoise(grain) - 0.5) * 0.006;

    return float4(col, 1.0);
}
)";

// Glass "liquido" (Fluent / Liquid Glass): compone la escena blureada SOLO
// dentro de un rounded-rect SDF, con refraccion en el borde, aberracion
// cromatica, boost de saturacion, grain y luz de borde/specular. La mascara
// SDF se emite como alpha (blending alpha), asi el frost queda con las
// esquinas redondeadas exactas del panel — sin el rect cuadrado de antes.
static const char* kGlassPS = R"(
Texture2D srcTex : register(t0);
SamplerState srcSamp : register(s0);
cbuffer BlurParams : register(b0)
{
    float4 TexelDir;     // (1/w, 1/h, dirX, dirY)
    float4 TintOpacity;  // (tintR, tintG, tintB, tintStrength)
    float4 RectBounds;   // (x1, y1, x2, y2) en px de pantalla
    float4 GlassParams;  // (radius, softness, refractPx, chromaPx)
    float4 GlassAlpha;   // (opacity, 0, 0, 0)
};
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
static float hash2(float2 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}
static float valueNoise(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash2(i);
    float b = hash2(i + float2(1.0, 0.0));
    float c = hash2(i + float2(0.0, 1.0));
    float d = hash2(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
static float sdRoundBox(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}
float4 PSGlass(VSOut input) : SV_Target
{
    float2 px = input.pos.xy;
    float2 lo = RectBounds.xy;
    float2 hi = RectBounds.zw;
    float2 c = (lo + hi) * 0.5;
    float2 half = max((hi - lo) * 0.5, float2(1.0, 1.0));
    float r = min(GlassParams.x, min(half.x, half.y));
    float soft = max(GlassParams.y, 0.5);

    float2 p = px - c;
    float d = sdRoundBox(p, half, r);
    float mask = 1.0 - smoothstep(-soft, soft, d);
    if (mask <= 0.0009)
        discard;

    // 1 en el borde, 0 hacia el centro: gobierna refraccion/croma/rim.
    float edge = saturate(1.0 - (-d) / (min(r, 30.0) + soft + 1.0));

    // Gradiente del SDF (apunta hacia afuera del panel).
    float e = 1.0;
    float2 grad = float2(
        sdRoundBox(p + float2(e, 0.0), half, r) - sdRoundBox(p - float2(e, 0.0), half, r),
        sdRoundBox(p + float2(0.0, e), half, r) - sdRoundBox(p - float2(0.0, e), half, r));
    float2 n = grad / (length(grad) + 1e-5);

    // Refraccion hacia dentro + aberracion cromatica en el borde.
    float2 texel = TexelDir.xy;
    float2 baseUv = input.uv - n * (GlassParams.z * edge) * texel;
    float chroma = GlassParams.w * edge;
    float3 col;
    col.r = srcTex.Sample(srcSamp, baseUv + n * chroma * texel).r;
    col.g = srcTex.Sample(srcSamp, baseUv).g;
    col.b = srcTex.Sample(srcSamp, baseUv - n * chroma * texel).b;

    // Vidrio: boost de saturacion + tint + grain escarchado.
    float luma = dot(col, float3(0.299, 0.587, 0.114));
    col = lerp(float3(luma, luma, luma), col, 1.18);
    col = lerp(col, TintOpacity.rgb, TintOpacity.a);
    col += (valueNoise(px / 148.0) - 0.5) * 0.014;

    // Luz de borde + especular superior (highlight de vidrio).
    float rimW = max(min(r, 22.0), 5.0);
    float rim = pow(saturate(1.0 - (-d) / rimW), 2.2);
    float topSpec = (1.0 - smoothstep(0.0, rimW * 2.4, px.y - lo.y)) * 0.6;
    col += rim * 0.13;
    col += topSpec * 0.05;

    return float4(col, mask * GlassAlpha.x);
}
)";

// Motion blur shader (lógica exacta de arst-motion-blur:
// https://github.com/arstnei0/arst-motion-blur):
// fragColor = mix(texture(DiffuseSampler, texCoord), texture(PrevSampler, texCoord), BlendFactor);
// fragColor.w = 1.0;
static const char* kMotionBlurPS = R"(
Texture2D currentTex : register(t0);
Texture2D prevTex : register(t1);
SamplerState srcSamp : register(s0);
cbuffer BlurParams : register(b0)
{
    float4 TexelDir;
    float4 TintOpacity;
    float4 RectBounds;
    float4 GlassParams;
    float4 GlassAlpha;
    float4 MotionBlurParams; // (BlendFactor, 0, 0, 0)
};
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
float4 PSMotionBlur(VSOut input) : SV_Target
{
    float3 cur = currentTex.Sample(srcSamp, input.uv).rgb;
    float3 prev = prevTex.Sample(srcSamp, input.uv).rgb;
    float3 col = lerp(cur, prev, MotionBlurParams.x);
    return float4(col, 1.0f);
}
)";

static bool g_shadersTried = false;

bool ok()
{
    return g_shadersOk;
}

void release()
{
    if (g_blurScissorRS) { g_blurScissorRS->Release(); g_blurScissorRS = nullptr; }
    if (g_blurRS) { g_blurRS->Release(); g_blurRS = nullptr; }
    if (g_blurBlend) { g_blurBlend->Release(); g_blurBlend = nullptr; }
    if (g_blurSampler) { g_blurSampler->Release(); g_blurSampler = nullptr; }
    if (g_blurCB) { g_blurCB->Release(); g_blurCB = nullptr; }
    if (g_blurPS) { g_blurPS->Release(); g_blurPS = nullptr; }
    if (g_glassPS) { g_glassPS->Release(); g_glassPS = nullptr; }
    if (g_compositePS) { g_compositePS->Release(); g_compositePS = nullptr; }
    if (g_copyPS) { g_copyPS->Release(); g_copyPS = nullptr; }
    if (g_motionBlurPS) { g_motionBlurPS->Release(); g_motionBlurPS = nullptr; }
    if (g_blurVS) { g_blurVS->Release(); g_blurVS = nullptr; }
    g_shadersTried = false;
    g_shadersOk = false;
}

bool ensure()
{
    if (g_shadersOk)
        return true;
    if (g_shadersTried)
        return false;
    if (!g_device)
        return false;
    g_shadersTried = true;

    bool ok = true;
    HRESULT hr = E_FAIL;
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;

    static bool s_logVs = false;
    static bool s_logPs = false;
    static bool s_logCb = false;
    static bool s_logSamp = false;

    if (FAILED(D3DCompile(kBlurVS, (SIZE_T)strlen(kBlurVS), nullptr, nullptr, nullptr,
                          "VSMain", "vs_4_0", 0, 0, &blob, &err)))
    {
        MC_LOG_ERROR("[Backdrop] VS compile failed%s%s", err ? " - " : "",
                     err ? (const char*)err->GetBufferPointer() : "");
        if (err) err->Release();
        if (blob) blob->Release();
        return false;
    }
    hr = g_device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                      nullptr, &g_blurVS);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logVs, "[Backdrop] CreateVertexShader failed: 0x%08X", hr);
        ok = false;
    }
    blob->Release();
    if (!ok)
        return false;

    if (FAILED(D3DCompile(kBlurPS, (SIZE_T)strlen(kBlurPS), nullptr, nullptr, nullptr,
                          "PSMain", "ps_4_0", 0, 0, &blob, &err)))
    {
        MC_LOG_ERROR("[Backdrop] PS compile failed%s%s", err ? " - " : "",
                     err ? (const char*)err->GetBufferPointer() : "");
        if (err) err->Release();
        if (blob) blob->Release();
        return false;
    }
    hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                     nullptr, &g_blurPS);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logPs, "[Backdrop] CreatePixelShader failed: 0x%08X", hr);
        ok = false;
    }
    blob->Release();
    if (!ok)
        return false;

    if (FAILED(D3DCompile(kCopyPS, (SIZE_T)strlen(kCopyPS), nullptr, nullptr, nullptr,
                          "PSMain", "ps_4_0", 0, 0, &blob, &err)))
    {
        MC_LOG_ERROR("[Backdrop] Copy PS compile failed%s%s", err ? " - " : "",
                     err ? (const char*)err->GetBufferPointer() : "");
        if (err) err->Release();
        if (blob) blob->Release();
        return false;
    }
    hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                     nullptr, &g_copyPS);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logPs, "[Backdrop] CreatePixelShader (copy) failed: 0x%08X", hr);
        ok = false;
    }
    blob->Release();
    if (!ok)
        return false;

    if (FAILED(D3DCompile(kCompositePS, (SIZE_T)strlen(kCompositePS), nullptr, nullptr, nullptr,
                          "PSComposite", "ps_4_0", 0, 0, &blob, &err)))
    {
        MC_LOG_ERROR("[Backdrop] Composite PS compile failed%s%s", err ? " - " : "",
                     err ? (const char*)err->GetBufferPointer() : "");
        if (err) err->Release();
        if (blob) blob->Release();
        return false;
    }
    hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                     nullptr, &g_compositePS);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logPs, "[Backdrop] CreatePixelShader (composite) failed: 0x%08X", hr);
        ok = false;
    }
    blob->Release();
    if (!ok)
        return false;

    if (FAILED(D3DCompile(kGlassPS, (SIZE_T)strlen(kGlassPS), nullptr, nullptr, nullptr,
                          "PSGlass", "ps_4_0", 0, 0, &blob, &err)))
    {
        MC_LOG_ERROR("[Backdrop] Glass PS compile failed%s%s", err ? " - " : "",
                     err ? (const char*)err->GetBufferPointer() : "");
        if (err) err->Release();
        if (blob) blob->Release();
        return false;
    }
    hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                     nullptr, &g_glassPS);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logPs, "[Backdrop] CreatePixelShader (glass) failed: 0x%08X", hr);
        ok = false;
    }
    blob->Release();
    if (!ok)
        return false;

    if (FAILED(D3DCompile(kMotionBlurPS, (SIZE_T)strlen(kMotionBlurPS), nullptr, nullptr, nullptr,
                          "PSMotionBlur", "ps_4_0", 0, 0, &blob, &err)))
    {
        MC_LOG_ERROR("[Backdrop] MotionBlur PS compile failed%s%s", err ? " - " : "",
                     err ? (const char*)err->GetBufferPointer() : "");
        if (err) err->Release();
        if (blob) blob->Release();
        return false;
    }
    hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                     nullptr, &g_motionBlurPS);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logPs, "[Backdrop] CreatePixelShader (motion blur) failed: 0x%08X", hr);
        ok = false;
    }
    blob->Release();
    if (!ok)
        return false;

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = 96; // 6 x float4 (24 floats)
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_device->CreateBuffer(&cbd, nullptr, &g_blurCB);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logCb, "[Backdrop] CreateBuffer failed: 0x%08X", hr);
        return false;
    }

    // Blend alpha para el composite glass: la mascara SDF redondeada se emite
    // como alpha, de modo que fuera del panel redondeado no se toca el fondo
    // (sin el rect cuadrado del scissor).
    {
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = g_device->CreateBlendState(&bd, &g_blurBlend);
        if (FAILED(hr))
        {
            BD_LOG_ONCE(s_logCb, "[Backdrop] CreateBlendState failed: 0x%08X", hr);
            return false;
        }
    }

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = g_device->CreateSamplerState(&sd, &g_blurSampler);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logSamp, "[Backdrop] CreateSamplerState failed: 0x%08X", hr);
        return false;
    }

    // Rasterizer PROPIO (patron legacy): sin el, los pases heredan el estado
    // del juego — que en Present puede tener SCISSOR activo con un rect
    // degenerado (Minecraft scissorea su UI) y todos los draws del blur
    // quedan recortados a nada. ImGui funciona porque su backend setea su
    // propio rasterizer; el blur debe hacer lo mismo.
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.ScissorEnable = FALSE;
    rd.DepthClipEnable = FALSE;
    hr = g_device->CreateRasterizerState(&rd, &g_blurRS);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logPs, "[Backdrop] CreateRasterizerState failed: 0x%08X", hr);
        return false;
    }

    g_shadersOk = true;
    MC_LOG("[Backdrop] Gaussian blur shader compiled");
    return true;
}

} // namespace mc::blur::shaders
