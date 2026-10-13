#pragma once
//
// Created by vastrakai on 6/29/2024.
// (Solstice blur — portado al SDK de Azyre)
//
#ifdef _MSC_VER
#include <d2d1_3.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <memory>
#include <optional>
#include <vector>

struct BlurCallbackData {
    float strength = 0.0f;
    float rounding = 0.0f;
    std::optional<ImVec4> clipRect;

    BlurCallbackData(float strength, float rounding, std::optional<ImVec4> clipRect = std::optional<ImVec4>()) {
        this->strength = strength;
        this->rounding = rounding;
        this->clipRect = clipRect;
    }
};

struct GhostCallbackData
{
    float strength = 0.0f;
    int maxFrames = 0;

    GhostCallbackData(float strength, int maxFrames) {
        this->strength = strength;
        this->maxFrames = maxFrames;
    }
};

class D2D {
public:

    static inline std::vector<std::shared_ptr<BlurCallbackData>> blurCallbacks = {};
    static inline std::vector<std::shared_ptr<GhostCallbackData>> ghostCallbacks = {};
    static void init(IDXGISwapChain* pSwapChain, ID3D11Device* pDevice);
    static void shutdown();

    static void beginRender(IDXGISurface* surface, float fxdpi);
    static void ghostFrameCallback(const ImDrawList* parent_list, const ImDrawCmd* cmd);
    static void addGhostFrame(ImDrawList* drawList, int maxFrames, float strength);
    static void ghostFrame(IDXGISurface* daSurface);
    static void endRender();
    static void blurCallback(const ImDrawList* parent_list, const ImDrawCmd* cmd);
    static bool addBlur(ImDrawList* drawList, float strength, std::optional<ImVec4> clipRect, float rounding);
    static void blurCallbackOptimized(const ImDrawList* parent_list, const ImDrawCmd* cmd);
    static bool addBlurOptimized(ImDrawList* drawList, float strength, std::optional<ImVec4> clipRect, float rounding);
};

#endif // _MSC_VER
