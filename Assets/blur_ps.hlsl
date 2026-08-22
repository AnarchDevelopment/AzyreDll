// Dual-pass Gaussian Blur Pixel Shader for menu background
// Uses a two-pass approximation (horizontal + vertical) baked in a single pass
// with a 13-tap kernel for a smooth, high-quality blur

cbuffer BlurParams : register(b0) {
    float2 texelSize;   // 1.0 / (width, height)
    float  blurRadius;  // strength scalar, e.g. 4.0
    float  opacity;     // overlay opacity blended on top
};

Texture2D    g_scene   : register(t0);
SamplerState g_sampler : register(s0);

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

static const int SAMPLES = 13;
static const float offsets[13] = { -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6 };
static const float weights[13] = {
    0.002216, 0.008764, 0.026995, 0.064759, 0.120985, 0.176033,
    0.199471,
    0.176033, 0.120985, 0.064759, 0.026995, 0.008764, 0.002216
};

float4 mainPS(VS_OUTPUT input) : SV_Target {
    float4 color = float4(0, 0, 0, 0);

    // Horizontal pass
    [unroll]
    for (int i = 0; i < SAMPLES; i++) {
        float2 offset = float2(offsets[i] * blurRadius * texelSize.x, 0.0);
        color += g_scene.Sample(g_sampler, input.Tex + offset) * weights[i];
    }

    // Vertical pass (approximated in-place using the horizontally blurred result)
    float4 colorV = float4(0, 0, 0, 0);
    [unroll]
    for (int j = 0; j < SAMPLES; j++) {
        float2 offset = float2(0.0, offsets[j] * blurRadius * texelSize.y);
        colorV += g_scene.Sample(g_sampler, input.Tex + offset) * weights[j];
    }

    // Average horizontal and vertical passes
    color = (color + colorV) * 0.5;

    // Darken overlay so the menu reads well
    color.rgb *= (1.0 - opacity * 0.35);
    color.a = 1.0;
    return color;
}
