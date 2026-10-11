#pragma once

#include "BlurDetail.hpp"

namespace mc::blur::passes {

// Copia fullscreen via pipeline propio (sin ImGui ad-hoc draw data).
void copy(ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst, UINT w, UINT h);

// Gaussiano separable de 9 taps: dirX/dirY (1,0) horizontal, (0,1) vertical.
void gauss(ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst,
           UINT w, UINT h, float dirX, float dirY);

// Composite Mica (puerto del legacy): pinta la escena blureada OPACA sobre el
// target con desaturacion + tint + gradiente + grain. w/h = tamaño del TARGET.
void composite(ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst,
                UINT w, UINT h, float tintR, float tintG, float tintB, float opacity);

// Variante REGION (patron legacy RenderBlurRegion): scissor al rect indicado
// (pixeles) para el frost fuerte de la GUI — el resto de la pantalla queda
// intacto para el composite claro.
void compositeRegion(ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst,
                     UINT w, UINT h, float rx1, float ry1, float rx2, float ry2,
                     float tintR, float tintG, float tintB, float opacity);

// Glass Fluent (Liquid Glass): compone el blur SOLO dentro de un rounded-rect
// SDF (radius px), con refraccion/croma en el borde y luz de borde, usando
// blending alpha para que las esquinas queden perfectamente redondeadas.
void compositeGlass(ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst,
                    UINT w, UINT h, float rx1, float ry1, float rx2, float ry2,
                    float radius, float softness, float refractPx, float chromaPx,
                    float tintR, float tintG, float tintB, float tintStrength,
                    float opacity);

// Save/restore del estado D3D: el pipeline del blur corre a mitad del
// Present y no debe contaminar el estado del juego.
struct StateBag
{
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11Buffer* vb = nullptr;
    UINT vbStride = 0;
    UINT vbOffset = 0;
    ID3D11InputLayout* il = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11SamplerState* samp = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11BlendState* bs = nullptr;
    float bf[4] = {};
    UINT sm = 0;
    ID3D11DepthStencilState* dss = nullptr;
    UINT dssRef = 0;
    ID3D11RasterizerState* rs = nullptr;
    UINT vpCount = 8;
    D3D11_VIEWPORT vp[8] = {};
};

void save(StateBag& s);
void restore(StateBag& s);

}
