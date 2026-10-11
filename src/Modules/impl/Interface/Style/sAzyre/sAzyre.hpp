#pragma once

namespace mc {

// Estilo Azyre: el menu clasico del cliente (acrilico, categorias laterales,
// rows con keybinds y search). El body vive en GUI/Menu.cpp; este punto de
// entrada existe para que el selector de estilos tenga una interfaz
// simetrica por estilo (ver gui::style()).
namespace gui::sAzyre {

void render(float anim);

}

}
