/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <d3d11.h>
#include "miniaudio/miniaudio.h"

// Global variables extern declarations
extern HMODULE g_hModule;
extern ID3D11Device* pDevice;
extern ID3D11DeviceContext* pContext;
extern ID3D11RenderTargetView* mainRenderTargetView;
extern ma_engine g_audioEngine;