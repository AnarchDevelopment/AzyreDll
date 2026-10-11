#include "BlurPasses.hpp"

namespace mc::blur::passes {

using namespace detail;

void copy(ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst, UINT w, UINT h)
{
    if (!g_copyPS)
        return;

    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)w;
    vp.Height = (FLOAT)h;
    vp.MaxDepth = 1.0f;

    g_context->OMSetRenderTargets(1, &dst, nullptr);
    g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(nullptr, 0);
    g_context->RSSetViewports(1, &vp);
    g_context->RSSetState(g_blurRS);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_blurVS, nullptr, 0);
    g_context->PSSetShader(g_copyPS, nullptr, 0);
    g_context->PSSetShaderResources(0, 1, &src);
    g_context->PSSetSamplers(0, 1, &g_blurSampler);
    g_context->Draw(3, 0);
}

void gauss(ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst,
           UINT w, UINT h, float dirX, float dirY)
{
    float params[4] = {1.0f / (float)w, 1.0f / (float)h, dirX, dirY};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(g_context->Map(g_blurCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    memcpy(mapped.pData, params, sizeof(params));
    g_context->Unmap(g_blurCB, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)w;
    vp.Height = (FLOAT)h;
    vp.MaxDepth = 1.0f;

    g_context->OMSetRenderTargets(1, &dst, nullptr);
    g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(nullptr, 0);
    g_context->RSSetViewports(1, &vp);
    g_context->RSSetState(g_blurRS);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_blurVS, nullptr, 0);
    g_context->PSSetShader(g_blurPS, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_blurCB);
    g_context->PSSetShaderResources(0, 1, &src);
    g_context->PSSetSamplers(0, 1, &g_blurSampler);
    g_context->Draw(3, 0);
}

void composite(ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst,
               UINT w, UINT h, float tintR, float tintG, float tintB, float opacity)
{
    if (!g_compositePS)
        return;

    float params[8] = {1.0f / (float)w, 1.0f / (float)h, 0.0f, 0.0f,
                       tintR, tintG, tintB, opacity};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(g_context->Map(g_blurCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    memcpy(mapped.pData, params, sizeof(params));
    g_context->Unmap(g_blurCB, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)w;
    vp.Height = (FLOAT)h;
    vp.MaxDepth = 1.0f;

    g_context->OMSetRenderTargets(1, &dst, nullptr);
    g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(nullptr, 0);
    g_context->RSSetViewports(1, &vp);
    g_context->RSSetState(g_blurRS);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_blurVS, nullptr, 0);
    g_context->PSSetShader(g_compositePS, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_blurCB);
    g_context->PSSetShaderResources(0, 1, &src);
    g_context->PSSetSamplers(0, 1, &g_blurSampler);
    g_context->Draw(3, 0);
}

void compositeRegion(ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst,
                     UINT w, UINT h, float rx1, float ry1, float rx2, float ry2,
                     float tintR, float tintG, float tintB, float opacity)
{
    if (!g_compositePS)
        return;
    if (rx2 <= rx1 || ry2 <= ry1)
        return;

    float params[8] = {1.0f / (float)w, 1.0f / (float)h, 0.0f, 0.0f,
                       tintR, tintG, tintB, opacity};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(g_context->Map(g_blurCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    memcpy(mapped.pData, params, sizeof(params));
    g_context->Unmap(g_blurCB, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)w;
    vp.Height = (FLOAT)h;
    vp.MaxDepth = 1.0f;

    D3D11_RECT sc = {};
    sc.left = (LONG)rx1;
    sc.top = (LONG)ry1;
    sc.right = (LONG)rx2;
    sc.bottom = (LONG)ry2;

    g_context->OMSetRenderTargets(1, &dst, nullptr);
    g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(nullptr, 0);
    g_context->RSSetViewports(1, &vp);
    g_context->RSSetScissorRects(1, &sc);
    g_context->RSSetState(g_blurScissorRS);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_blurVS, nullptr, 0);
    g_context->PSSetShader(g_compositePS, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_blurCB);
    g_context->PSSetShaderResources(0, 1, &src);
    g_context->PSSetSamplers(0, 1, &g_blurSampler);
    g_context->Draw(3, 0);

    // Limpiar el scissor que dejamos activo (patron legacy): dejar el
    // fullscreen como scissor por defecto para el siguiente draw.
    D3D11_RECT full = {};
    full.right = (LONG)w;
    full.bottom = (LONG)h;
    g_context->RSSetScissorRects(1, &full);
}

void compositeGlass(ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst,
                    UINT w, UINT h, float rx1, float ry1, float rx2, float ry2,
                    float radius, float softness, float refractPx, float chromaPx,
                    float tintR, float tintG, float tintB, float tintStrength,
                    float opacity)
{
    if (!g_glassPS)
        return;
    if (rx2 <= rx1 || ry2 <= ry1)
        return;

    float params[20] = {
        1.0f / (float)w, 1.0f / (float)h, 0.0f, 0.0f,
        tintR, tintG, tintB, tintStrength,
        rx1, ry1, rx2, ry2,
        radius, softness, refractPx, chromaPx,
        opacity, 0.0f, 0.0f, 0.0f};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(g_context->Map(g_blurCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    memcpy(mapped.pData, params, sizeof(params));
    g_context->Unmap(g_blurCB, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)w;
    vp.Height = (FLOAT)h;
    vp.MaxDepth = 1.0f;

    g_context->OMSetRenderTargets(1, &dst, nullptr);
    g_context->OMSetBlendState(g_blurBlend, nullptr, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(nullptr, 0);
    g_context->RSSetViewports(1, &vp);
    g_context->RSSetState(g_blurRS);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_blurVS, nullptr, 0);
    g_context->PSSetShader(g_glassPS, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_blurCB);
    g_context->PSSetShaderResources(0, 1, &src);
    g_context->PSSetSamplers(0, 1, &g_blurSampler);
    g_context->Draw(3, 0);
}

void save(StateBag& s)
{
    g_context->OMGetRenderTargets(1, &s.rtv, &s.dsv);
    g_context->IAGetVertexBuffers(0, 1, &s.vb, &s.vbStride, &s.vbOffset);
    g_context->IAGetInputLayout(&s.il);
    g_context->IAGetPrimitiveTopology(&s.topo);
    g_context->VSGetShader(&s.vs, nullptr, nullptr);
    g_context->PSGetShader(&s.ps, nullptr, nullptr);
    g_context->PSGetConstantBuffers(0, 1, &s.cb);
    g_context->PSGetSamplers(0, 1, &s.samp);
    g_context->PSGetShaderResources(0, 1, &s.srv);
    g_context->OMGetBlendState(&s.bs, s.bf, &s.sm);
    g_context->OMGetDepthStencilState(&s.dss, &s.dssRef);
    g_context->RSGetState(&s.rs);
    g_context->RSGetViewports(&s.vpCount, s.vp);
}

void restore(StateBag& s)
{
    ID3D11ShaderResourceView* nullSrv = nullptr;
    ID3D11Buffer* nullCb = nullptr;
    ID3D11SamplerState* nullSamp = nullptr;
    g_context->PSSetShaderResources(0, 1, &nullSrv);
    g_context->PSSetConstantBuffers(0, 1, &nullCb);
    g_context->PSSetSamplers(0, 1, &nullSamp);
    g_context->PSSetShader(nullptr, nullptr, 0);
    g_context->VSSetShader(nullptr, nullptr, 0);

    g_context->OMSetRenderTargets(1, &s.rtv, s.dsv);
    UINT stride = s.vbStride;
    UINT offset = s.vbOffset;
    g_context->IASetVertexBuffers(0, 1, &s.vb, &stride, &offset);
    g_context->IASetInputLayout(s.il);
    g_context->IASetPrimitiveTopology(s.topo);
    g_context->VSSetShader(s.vs, nullptr, 0);
    g_context->PSSetShader(s.ps, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &s.cb);
    g_context->PSSetSamplers(0, 1, &s.samp);
    g_context->PSSetShaderResources(0, 1, &s.srv);
    g_context->OMSetBlendState(s.bs, s.bf, s.sm);
    g_context->OMSetDepthStencilState(s.dss, s.dssRef);
    g_context->RSSetState(s.rs);
    g_context->RSSetViewports(s.vpCount, s.vp);

    auto rel = [](auto* p) { if (p) p->Release(); };
    rel(s.rtv); rel(s.dsv); rel(s.vb); rel(s.il); rel(s.vs); rel(s.ps);
    rel(s.cb); rel(s.samp); rel(s.srv); rel(s.bs); rel(s.dss); rel(s.rs);
}

} // namespace mc::blur::passes
