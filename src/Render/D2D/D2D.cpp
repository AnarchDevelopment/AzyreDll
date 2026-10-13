#ifdef _MSC_VER
#define INITGUID
//
// Created by vastrakai on 6/29/2024.
// (Solstice blur — portado al SDK de Azyre)
//

#include "D2D.hpp"

#include "Framework/Log.hpp"

#include <winrt/base.h>
#include <d2d1_3.h>
#include <d3d11.h>

struct BlurCallbackData;
static winrt::com_ptr<ID2D1Factory3> d2dFactory = nullptr;
static winrt::com_ptr<ID2D1Device> d2dDevice = nullptr;

static winrt::com_ptr<ID2D1Effect> blurEffect = nullptr;
static winrt::com_ptr<ID2D1Bitmap1> sourceBitmap = nullptr;

static winrt::com_ptr<ID2D1DeviceContext> d2dDeviceContext = nullptr;
static winrt::com_ptr<ID2D1SolidColorBrush> brush = nullptr;

bool initD2D = false;
// used for optimized blur
static ID2D1Bitmap* cachedBitmap = nullptr;
static ID2D1ImageBrush* cachedBrush = nullptr;
static ID2D1RoundedRectangleGeometry* cachedClipRectGeo = nullptr;
static bool requestFlush = false;

float dpi = 0.0f;

void D2D::init(IDXGISwapChain* pSwapChain, ID3D11Device* pDevice)
{
    (void)pSwapChain;
    if (initD2D) return;

    if (initD2D == false && SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.put()))) {
        winrt::com_ptr<IDXGIDevice> dxgiDevice;
        pDevice->QueryInterface<IDXGIDevice>(dxgiDevice.put());
        if (dxgiDevice == nullptr) {
            return;
        }

        d2dFactory->CreateDevice(dxgiDevice.get(), d2dDevice.put());
        if (d2dDevice == nullptr) {
            return;
        }

        d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dDeviceContext.put());
        if (d2dDeviceContext == nullptr) {
            return;
        }

        //Create blur
        d2dDeviceContext->CreateEffect(CLSID_D2D1GaussianBlur, blurEffect.put());
        if (blurEffect == nullptr) {
            return;
        }
    }

    initD2D = true;
    MC_LOG("[D2D] initialized");
}

void D2D::shutdown()
{
    if (!initD2D) {
        return;
    }

    d2dFactory = nullptr;
    d2dDevice = nullptr;
    d2dDeviceContext = nullptr;
    brush = nullptr;
    blurEffect = nullptr;
    sourceBitmap = nullptr;
    initD2D = false;
    MC_LOG("[D2D] shutdown");
}

void D2D::beginRender(IDXGISurface* surface, float fxdpi)
{
    if (!initD2D || !d2dDeviceContext)
        return;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                 D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hr = d2dDeviceContext->CreateBitmapFromDxgiSurface(surface, &bitmapProperties, sourceBitmap.put());

    if (FAILED(hr)) {
        MC_LOG_ERROR("[D2D] failed to create bitmap from DXGI surface (0x%08X)", (unsigned)hr);
        return;
    }

    dpi = fxdpi;
    d2dDeviceContext->SetDpi(fxdpi, fxdpi);
    d2dDeviceContext->SetTarget(sourceBitmap.get());
    d2dDeviceContext->BeginDraw();
}

void D2D::ghostFrameCallback(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    (void)parent_list;
    auto data = (GhostCallbackData*)cmd->UserCallbackData;
    if (data == nullptr) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    auto displaySize = io.DisplaySize;
    auto size = sourceBitmap->GetPixelSize();
    auto rect = D2D1::RectU(0, 0, size.width, size.height);
    auto destPoint = D2D1::Point2U(0, 0);

    static std::vector<ID2D1Bitmap*> ghostBitmaps;
    static ID2D1Bitmap* targetBitmap = nullptr;
    static D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(sourceBitmap->GetPixelFormat());

    if (targetBitmap == nullptr) {
        d2dDeviceContext->CreateBitmap(size, props, &targetBitmap);
    }

    targetBitmap->CopyFromBitmap(&destPoint, sourceBitmap.get(), &rect);

    int maxFrames = data->maxFrames;

    while (ghostBitmaps.size() >= (size_t)maxFrames) {
        auto bitmap = ghostBitmaps.front();
        ghostBitmaps.erase(ghostBitmaps.begin());
        bitmap->Release();
    }

    ID2D1Bitmap* ghostBitmap = nullptr;
    d2dDeviceContext->CreateBitmap(size, props, &ghostBitmap);
    ghostBitmap->CopyFromBitmap(&destPoint, sourceBitmap.get(), &rect);
    ghostBitmaps.push_back(ghostBitmap);

    float alpha = 0.3f * data->strength;
    for (auto& gb : ghostBitmaps) {
        d2dDeviceContext->DrawBitmap(gb, D2D1::RectF(0, 0, displaySize.x, displaySize.y), alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        alpha *= data->strength;
    }

    for (auto it = ghostCallbacks.begin(); it != ghostCallbacks.end(); it++) {
        if (it->get() == data) {
            ghostCallbacks.erase(it);
            break;
        }
    }
}

void D2D::addGhostFrame(ImDrawList* drawList, int maxFrames, float strength)
{
    if (!initD2D) {
        return;
    }

    auto uniqueData = std::make_shared<GhostCallbackData>(strength, maxFrames);
    auto data = uniqueData.get();
    ghostCallbacks.push_back(uniqueData);
    drawList->AddCallback(ghostFrameCallback, data);
}

void D2D::endRender()
{
    if (!initD2D || !d2dDeviceContext || !sourceBitmap)
        return;

    d2dDeviceContext->EndDraw();
    d2dDeviceContext->SetTarget(nullptr);
    sourceBitmap = nullptr;
    if (cachedBitmap != nullptr) {
        cachedBitmap->Release();
        cachedBitmap = nullptr;
    }
    if (cachedBrush != nullptr) {
        cachedBrush->Release();
        cachedBrush = nullptr;
    }
    if (cachedClipRectGeo != nullptr) {
        cachedClipRectGeo->Release();
        cachedClipRectGeo = nullptr;
    }
}

void D2D::blurCallback(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    (void)parent_list;
    auto data = (BlurCallbackData*)cmd->UserCallbackData;
    if (data == nullptr || !sourceBitmap) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec4 clipRect = data->clipRect.has_value() ? *data->clipRect : cmd->ClipRect;

    ID2D1Bitmap* targetBitmap = nullptr;
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(sourceBitmap->GetPixelFormat());
    d2dDeviceContext->CreateBitmap(sourceBitmap->GetPixelSize(), props, &targetBitmap);
    auto destPoint = D2D1::Point2U(0, 0);
    auto size = sourceBitmap->GetPixelSize();
    auto rect = D2D1::RectU(0, 0, size.width, size.height);
    targetBitmap->CopyFromBitmap(&destPoint, sourceBitmap.get(), &rect);

    D2D1_RECT_F screenRectF = D2D1::RectF(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    D2D1_RECT_F clipRectD2D = D2D1::RectF(clipRect.x, clipRect.y, clipRect.z, clipRect.w);
    D2D1_ROUNDED_RECT clipRectRounded = D2D1::RoundedRect(clipRectD2D, data->rounding, data->rounding);

    blurEffect->SetInput(0, targetBitmap);
    blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, data->strength);
    blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_QUALITY);

    ID2D1Image* outImage = nullptr;
    blurEffect->GetOutput(&outImage);

    ID2D1ImageBrush* outImageBrush = nullptr;
    D2D1_IMAGE_BRUSH_PROPERTIES outImage_props = D2D1::ImageBrushProperties(screenRectF);
    d2dDeviceContext->CreateImageBrush(outImage, outImage_props, &outImageBrush);

    ID2D1RoundedRectangleGeometry* clipRectGeo = nullptr;
    d2dFactory->CreateRoundedRectangleGeometry(clipRectRounded, &clipRectGeo);
    d2dDeviceContext->FillGeometry(clipRectGeo, outImageBrush);
    clipRectGeo->Release();

    d2dDeviceContext->Flush();
    outImageBrush->Release();
    outImage->Release();
    targetBitmap->Release();

    for (auto it = blurCallbacks.begin(); it != blurCallbacks.end(); it++) {
        if (it->get() == data) {
            blurCallbacks.erase(it);
            break;
        }
    }
}

bool D2D::addBlur(ImDrawList* drawList, float strength, std::optional<ImVec4> clipRect, float rounding)
{
    if (!initD2D) {
        return false;
    }

    if (strength == 0)
        return false;

    auto uniqueData = std::make_shared<BlurCallbackData>(strength, rounding, clipRect);
    auto data = uniqueData.get();
    blurCallbacks.push_back(uniqueData);
    drawList->AddCallback(blurCallback, data);
    return true;
}

template <typename T>
void SafeRelease(T** ptr) {
    if (*ptr) {
        (*ptr)->Release();
        *ptr = nullptr;
    }
}

void D2D::blurCallbackOptimized(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    (void)parent_list;
    auto data = (BlurCallbackData*)cmd->UserCallbackData;
    if (data == nullptr || !sourceBitmap) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec4 clipRect = data->clipRect.has_value() ? *data->clipRect : cmd->ClipRect;

    D2D1_SIZE_U bitmapSize = sourceBitmap->GetPixelSize();

    if (cachedBitmap == nullptr || cachedBitmap->GetPixelSize().width != bitmapSize.width || cachedBitmap->GetPixelSize().height != bitmapSize.height) {
        SafeRelease(&cachedBitmap);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(sourceBitmap->GetPixelFormat());
        d2dDeviceContext->CreateBitmap(bitmapSize, props, &cachedBitmap);
        auto destPoint = D2D1::Point2U(0, 0);
        auto rect = D2D1::RectU(0, 0, io.DisplaySize.x, io.DisplaySize.y);
        cachedBitmap->CopyFromBitmap(&destPoint, sourceBitmap.get(), &rect);
    }

    static ImVec4 cachedClipRect;
    static float cachedRounding = -1.0f;
    if (cachedClipRectGeo == nullptr || cachedClipRect != clipRect || cachedRounding != data->rounding) {
        SafeRelease(&cachedClipRectGeo);
        cachedClipRect = clipRect;
        cachedRounding = data->rounding;

        D2D1_RECT_F clipRectD2D = D2D1::RectF(clipRect.x, clipRect.y, clipRect.z, clipRect.w);
        D2D1_ROUNDED_RECT clipRectRounded = D2D1::RoundedRect(clipRectD2D, data->rounding, data->rounding);
        d2dFactory->CreateRoundedRectangleGeometry(clipRectRounded, &cachedClipRectGeo);
    }

    if (cachedBrush == nullptr) {
        ID2D1Image* outImage = nullptr;
        blurEffect->SetInput(0, cachedBitmap);
        blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, data->strength);
        blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
        blurEffect->GetOutput(&outImage);

        D2D1_IMAGE_BRUSH_PROPERTIES brushProps = D2D1::ImageBrushProperties(D2D1::RectF(0, 0, io.DisplaySize.x, io.DisplaySize.y));
        d2dDeviceContext->CreateImageBrush(outImage, brushProps, &cachedBrush);

        SafeRelease(&outImage);
    }

    ID2D1Image* originalTarget = nullptr;
    d2dDeviceContext->GetTarget(&originalTarget);

    d2dDeviceContext->SetTarget(sourceBitmap.get());
    d2dDeviceContext->FillGeometry(cachedClipRectGeo, cachedBrush);
    d2dDeviceContext->SetTarget(originalTarget);
    SafeRelease(&originalTarget);
    d2dDeviceContext->Flush();
}

bool D2D::addBlurOptimized(ImDrawList* drawList, float strength, std::optional<ImVec4> clipRect, float rounding)
{
    if (!initD2D) {
        return false;
    }

    if (strength == 0) {
        return false;
    }

    auto uniqueData = std::make_shared<BlurCallbackData>(strength, rounding, clipRect);
    auto data = uniqueData.get();
    blurCallbacks.push_back(uniqueData);
    drawList->AddCallback(blurCallbackOptimized, data);
    return true;
}

#endif // _MSC_VER
