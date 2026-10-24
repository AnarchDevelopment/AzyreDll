#pragma once

namespace mc {

// Factor de FOV aplicado por el modulo Zoom (1.0 = sin zoom). Lo actualiza
// Zoom::onRender cada frame; worldToScreen lo multiplica para que TODOS los
// overlays (ESP, NameTags, Tracers, Waypoints, BlockESP, Trail, etc.)
// coincidan con el FOV real de la camara mientras se zoomea.
float zoomFovFactor();

}
