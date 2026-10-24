#pragma once

namespace mc {

// Estilo Rise: puerto del ClickGui de Rise 6.9.5 — panel oscuro con sidebar
// lateral (categorias + search), tarjetas de módulos con expander de settings,
// animaciones suaves de escala/fade, badges de keybinds y acentos de color.
namespace gui::sRise {

void render(float anim);

// Escala efectiva del chrome de Rise (base x2 * UI Scale). La usan los
// widgets de settings (RiseWidgets) para escalar su geometria.
float uiScale();

}

}
