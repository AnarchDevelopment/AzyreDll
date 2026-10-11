#pragma once

#include "BlurDetail.hpp"

namespace mc::blur::textures {

bool ensure();
void release();

// Formatos typeless no permiten SRV sin formato explicito (rama legacy:
// MotionBlurMakeSrvFormat). La textura conserva el formato del backbuffer
// (CopyResource lo exige) pero el SRV debe ser fully-typed.
DXGI_FORMAT makeSrvFormat(DXGI_FORMAT f);

}
