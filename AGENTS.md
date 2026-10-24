# AGENTS.md — Guía de Sesión: Azyre SDK (Bedrock Win10 DX11)

## Contexto del Proyecto

Cliente interno para Minecraft Bedrock Edition (Win10 DX11), protocolo pre-1.16 (UseItemPacket, no InventoryTransactionPacket). Compilado con MSVC (VS 2026, Release x64). El ejecutable objetivo es `Minecraft.Win10.DX11.exe`. Los offsets RVAs cambian entre builds del juego — todo lo documentado aquí fue confirmado con dumps empíricos del build actual.

**Flujo de trabajo probado**: dump empírico en el log ([MPPDump], [RPDbg], [SPDbg]) → identificar layout → implementar → verificar con el log. Los logs viven en:
```
%LOCALAPPDATA%\Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe\LocalState\Azyre\azyre_sdk.log
```

## Arquitectura

- **Hilo de render**: TODO corre en el Present hook (DX11.cpp), a la tasa de refresco del monitor (~144Hz) para evitar stuttering. ModuleManager tiene guardias SEH por módulo (fault → disable, no crash).
- **Hooks instalados** (MinHook): Present/ResizeBuffers (vtable), sendToServer (0x38B940), PacketSender/BatchedPacketSender send (sender hooks).
- **Módulos registran** con `MC_REGISTER_MODULE`. Config en JSON con claves por nombre de módulo.
- **CMakeLists.txt** usa `GLOB_RECURSE CONFIGURE_DEPENDS` — archivos nuevos se detectan solos, pero renombrar archivos con el mismo basename en dos directorios distintos produce LNK4042 (por eso Features/Blink.* → PacketHelper.*).

## Offsets y Firmas Confirmados (con evidencia)

### UseItemPacket (vtable 0xE6A020)
Layout (confirmado por decompile del ctor 0x5100F0):
```
+0x00 vtable | +0x08 Type=2 | +0x0C Field=1 | +0x10 X | +0x14 Y | +0x18 Z
+0x1C Face(byte) | +0x20 PLAYERPOS Vec3 (param 5 del ctor) | +0x2C CLICK Vec3 RELATIVO (param 6) | +0x38 ItemStack | +0x58 Flag = hotbar slot
```

### MovePlayerPacket (vtable 0xE644F8)
Layout (confirmado por [MPPDump]):
```
+0x08 entityID(2) | +0x18 X | +0x1C Y | +0x20 Z | +0x24 Pitch | +0x28 Yaw | +0x2C HeadYaw | +0x30 mode(16)
```

### Actor (LocalPlayer/Player)
```
+0x0C mPosition (pos lógica, la escribe Actor::setPos)
+0x18 mPrevPosition (la del update anterior — confirmado por MPPDump en movimiento)
+0x24 copia de +0x18
+0x30 Velocity X | +0x34 Velocity Y | +0x38 Velocity Z
+0x0C también ES el yaw de render del modelo (confirmado por decompile de sub_1404D4560, vtable LocalPlayer slot 211 = RenderOrchestrator) — ver nota abajo
+0x14 también ES el pitch de render (mismo decompile: +0x14 = Z de mPosition)
+0x3C Pitch | +0x40 Yaw (cámara lee estos) | +0x44 render Pitch | +0x48 render Yaw (ambos = +0x3C/+0x40; cámara Y modelo comparten)
+0xB4 AABB min | +0xC0 AABB max
+0xD08 network move target (lo que escribe moveTo)
+0xD14 network Pitch | +0xD18 network Yaw (CONFIRMADO por decompile de moveTo) | +0xD1C flags
```

### RenderOrchestrator (vtable LocalPlayer slot 211 = sub_1404D4560)

Confirmado por decompile en el build actual. **No** arma el MovePlayerPacket. Lee
`*((_DWORD*)a1 + 3)` = **+0x0C** (X de mPosition) como yaw y `*((_DWORD*)a1 + 5)` =
**+0x14** (Z de mPosition) como pitch, arma dos Vec3 con el offset de altura del actor,
y llama:

```
sub_1405EAE30(this, renderPos, posAlt, posFeet, 0, 0, 200)  →  devuelve {2,3}
```

`sub_1405EAE30` **ya esta decompilado** y es un **VOXEL CAST** (ClipHelper estilo
Minecraft Java), no una funcion de rotacion. Sus parametros 2 y 3 son los puntos Vec3
del rayo: los convierte a BlockPos con `floor()` (el clásico `if (x < (int)x) --i`),
avanza al bloque mas lejano en cada paso de la rejilla, y por cada celda consulta el
block source (sub_1405E7620), el collision shape (vtable+56), canCollide (vtable+320)
y arma el HitResult (vtable+456). El `200` que le pasa el orquestador es el **MAXIMO DE
CELDAS** del cast, no un alcance en bloques. El retorno es el HitResult: el caller
hace `(ret-2) <= 1` para aceptar solo los tipos 2 y 3.

**Descartado como via de bodyYaw/headYaw**: no hay rotacion que escribir en esta cadena.

### Actor::moveTo (vtable LocalPlayer slot 14 = sub_1404D4FB0) — CONFIRMADO por decompile

Es un setter plano, sin logica:

```
*(Vec3*)(this + 0xD08) = pos      // a2
*(float*)(this + 0xD14) = rot.x   // a3[0]  -> PITCH
*(float*)(this + 0xD18) = rot.y   // a3[1]  -> YAW
*(int*)  (this + 0xD1C) = flags   // a4
```

**Hallazgo clave**: el `Vec2& rot` del `MovePlayerPacket` son DOS floats consecutivos.
O sea, en el paquete `+0x24` = pitch y `+0x28` = yaw. El **headYaw (`+0x2C`) NO se guarda**
— no hay campo de headYaw en esta cadena. La hipotesis de que podia haber un par
pitch/yaw separado en la zona de red queda DESCARTADA: es un solo par de dos floats.

**Que queda para rotaciones silenciosas de render**:
1. `+0x0C` (X de mPosition) es el yaw que lee el renderorquestador, pero ES el centro
   x del AABB (bug 13) — escribirlo desplaza el modelo y desfasa el ESP.
2. `+0x44/+0x48` mueven la camara (ya probado).
3. `+0xD14/+0xD18` son la rot de RED que escribe moveTo — son el TARGET de red, no el
   render. Escribirlos no gira el modelo, cambia el objetivo al que se va a interpolar.
4. No existe `setYBodyRot`/`setYHeadRot`/`lerpTo` en la vtable.

**Conclusion**: en este build NO hay campo de rotacion de render separado del AABB.
La rotacion silenciosa que gire el modelo sin tocar la camara ni el AABB no es viable
sin escribir memoria a ciegas. Registrado para no repetir la busqueda.

**Lectura**: no hay un par de render separado en +0x44/+0x48 (eso ya se habia probado
y movia la camara). El render lee **mPosition directo**. Por eso +0x0C es a la vez el
centro x del AABB (bug 13) y el yaw de render.

**Consecuencia para rotaciones silenciosas de render**: escribir +0x0C giraria el modelo
hacia el objetivo, pero +0x0C ES el centro del AABB — no gira, lo DESPLAZA. En 1ª persona
no se ve; en 2ª/3ª persona el modelo se corre y el ESP (anclado en ese mismo +0x0C)
se desfasa del jugador real. El offset "correcto" para girar sin desplazar seria
+0x44/+0x48, que mueve la camara. **Pendiente: identificar el par de render real antes
de escribir** (caller de sub_1405EAE30, o caller de moveTo).

### GameMode (vtable 0xE8A1A0)
- **Slot 12 = useItemOn** (0x5113F0), firma confirmada por decompile:
  ```
  useItemOn(this, Player*, ItemStack*, BlockPos const&, uchar face, Vec3 const& clickAbs)
  ```
  Hace TODO un right-click: arma UseItemPacket con flag=hotbar slot, envía por PacketSender interno, interactúa con el bloque, place local via 0x55D960, consume item.
- Cadena `gameModeAddr()`: `level+0x1648 → SNH+0x20` solo si valida contra las 3 vtables reales (CreativeMode 0xE8A0C8, SurvivalMode 0xE8A240, GameMode 0xE8A1A0). En servidores la cadena resuelve otra cosa — fallback: instancia del scanner de vtables (Game.cpp `scannedGm_`).

### BlockSource::getBlock (0x5E73F0)
Firma real (confirmada por decompile): `getBlock(this, void* out, BlockPos const* pos)`. El **out contiene el LEGACY ID del bloque** (el orquestador usa `out[0]` como índice del registry y lo compara con el "aire" del singleton qword_140FABC88). El ABI anterior `(this, x, y, z)` causaba los FATAL "READ @ 0x5/0x4A/0xB" (la coordenada X como puntero). Ver [BSdbg] en el log para verificar.

## Módulos — Estado y Hallazgos

### Backtrack (Combat)
- **Freeze**: usa `placeBlockFull` (orquestador 0x5113F0) con posición paquete-exacto (target de red +0xD08 muestreado antes del rewrite).
- **Render Real Location**: ghost con **encuadre del ESP** — `realPos_` es centro +0x0C (donde el motor ancla el modelo) y solo se mapea la Y a pies (`yOff = shownActor.pos().y − posExact().y`), `height()`, suavizado `smooth::interp` (sin delay). El buffer de 150ms desfasaba la caja 150ms respecto al modelo. Caja estilo Rise 6.9.5 (GL_QUADS rellenas, alpha 50/255).
- **Modo de operación**: solo arranca con cursor OCULTO + LMB (los clicks en el inventario del juego no cuentan).

### Scaffold (Movement)
- **Lógica Solstice**: mStartY con lock-Y, extend en dirección de vista (getRotBasedPos), getBlockPlaceFace (6 vecinos, abajo primero), getClosestPlacePos (espiral por anillos), tower Velocity/Clip.
- **Lecturas de bloques**: `bs.getBlockId()` (BlockSource real, no chunks cacheados — los chunks stale causaban intermitencia).
- **Slot fallback v6**: heldOk = valid() && Item* no-null (el isBlockItem por vtable rechazaba items válidos); block items verificados ganan, sino cualquier Item* (el orquestador decide).
- **Place fallido**: return false (SIN fallback de paquete manual — mandar un place rechazado por el cliente crea ghost blocks). failStreak (2 fallos) → swap.
- **Silent rotation (server + client)**: se quitó el checkbox `Client-Side Rotations`; al elegir una rotación de Scaffold, el mismo objetivo se envía por `rot::request()` al hook del MovePlayerPacket y a `rotation_helper::requestClientRotation()` para el modelo local. El helper interpola body yaw, head yaw relativo al torso y head pitch por frame; no cambia la cámara ni deja campos del Actor modificados fuera del setup del modelo. Telly también solicita ambas rutas. Scaffold limpia su request `PrioMovement` y el estado visual al activarse/desactivarse; los modos Normal/Down/Backwards/Reverse siguen disponibles.

### Step (Movement) — NUEVO
- **Detección**: onGround + WASD + speed2D < "Blocked speed" + getBlockId frontal sólido (sondeos a 0.8/1.35 desde posExact con wishDir) — sin jurar por la velocidad real, que vale ~0 contra la pared.
- **Modo Motion**: las celdas frontales/propias con aire confirmado (id==0 exacto, -1 = desconocido NO cuenta) → vel.y = liftSpeed hasta alcanzar el target de pies; la física del juego resuelve el barrido.
- **Modo Clip**: subida instantánea con el patrón Tower Clip del Scaffold (mPosition + AABB trasladado + move target +0xD08, 3 escrituras juntas, isReadable 0xD8 + rawRead/rawWrite). Cooldown 200ms. Nunca sube si el despeje no es aire confirmado en ambas columnas.
- **Puertas**: sneaking/liquidos/menús/jumping abortan el step; settings en inglés (Mode/Height/Lift speed/Blocked speed/Debug).

### FakeLag (Movement) — NUEVO
- **Reusa el buffer del Blink** (PacketHelper) cíclicamente: retiene MovePlayerPacket hasta cumplir hold time (ms) O depth (paquetes), suelta en ráfaga FIFO y reinicia el ciclo. Sin hooks propios.
- **Conflicto resuelto**: el buffer es compartido — si el Blink está activo, FakeLag se desactiva solo con aviso al chat (un solo dueño). Ambos límites a 0 se clampan a depth=6 (evita un "Blink duplicado" infinito).
- **Release on damage**: onHurt del manager suelta el burst al recibir daño (anti-kite). Gap natural: entre deactivate→activate pasa a lo sumo 1 paquete en vuelo.

### Blink (Movement)
- PacketHelper.cpp (renombrado para evitar LNK4042 por colisión de basename).
- Captura MovePlayerPacket en hkSendToServer (copia bytes, el original muere con el caller).
- Libera con `chathook::sendRawPacket` (los handles capturados del propio hook).
- Caja en lastSent con **encuadre del ESP**: `lastSent` ya es centro +0x0C, solo se mapea la Y a pies (`yOff = local.pos().y − posExact().y`), `height()`, suavizado `smooth::interp` (sin delay). Contador de paquetes.

### ESP/NameTags (Visuals)
- **Desfase del NameTags resuelto** (era el mismo bug 13): se anclaba en `pos()` (esquina +0xB4) → etiqueta flotando −0.3 en x/z del modelo. Ahora ancla centro `posExact()` x/z + pies `pos().y`, igual que el ESP.
- **Modo Smooth (A/B in-game)**: ambos módulos tienen toggle "Smooth" — ON = interp prev→cur (lag ≈ medio intervalo, C0 continuo), OFF = raw directo (encaja exacto si +0x0C está frame-suavizado por el motor; salta si viene a ~20Hz). El toggle es el discriminante: calibrar contra el modelo y fijar el default según lo que se vea.
- La sesión siguiente definirá si el desfase temporal restante del ESP en Smooth es el lag v·τ del interpolador o un mismatch de alpha con el motor (si persiste, replicar el par prev/cur del propio actor: +0x18/+0x0C).

### Smoothing (Features/Smoothing.hpp + .cpp)
- **Reloj QPC compartido** (`detail::nowMs()` en Smoothing.hpp): TODO el smoothing usa QPC. `GetTickCount64` tiene granularidad ~15.6ms → a 60/144 fps el dt/`t` por frame sale cuantizado (0/15.6/31.2ms) y el suavizado avanza a saltos — el "trabado" que se ve en TODOS los módulos que proyectan a pantalla (comparten la cámara vía `getCameraView`).
- `value(id, raw, delayMs > 0)`: buffer de historia para los casos con delay intencional. **Ya sin llamadores**: cámara, Backtrack ghost y Blink pasaron todos a `interp` (el delay desfasaba las cajas del modelo).
- `interp(id, raw)`: **interpolador per-frame con hand-off seamless** (factor principal de `renderPos`, expuesto). Lo usa la **cámara 1ª persona** (Camera.hpp): la base del player es tick-based (~20Hz) → interpolarla por-frame la lleva a la tasa de refresco ("144Hz") y calza con el mundo que el motor renderiza interpolado. (Antes: buffer de 30ms con reloj cuantizado → escalonado compartido por todos los módulos.)
- `entityPos(actor, raw)` → `renderPos()`: **interpolación prev→cur con hand-off seamless** (mejora del `entityPos` de GitHub). Intervalo **EMA por señal** (no 50ms fijo); el segmento nuevo arranca en la **posición ya mostrada** (cero salto en updates tempranos/tardíos). Teleport guard (>10 bloques re-ancla). Filtra el raw directo (esquina AABB +0xB4 del caller).
- `clear(id)`: ahora limpia tanto `value()` como `renderPos()`/`interp()` (implementada en Smoothing.cpp).
- **Diagnóstico [REMPos]** (Smoothing.cpp, `kTraceRemote`): traza el primer remoto, 250 líneas — raw/+0x0C/salida/intervalo. Flip a false cuando no haga falta.

## Sistema Global de Rotaciones (Features/Rotation.hpp)

```
rot::request(pitch, yaw, ms, prioridad)   ← cualquier módulo pide
rot::current(pitch, yaw)                   ← expira sola
```

Aplicación server-side: el hook del MovePlayerPacket saliente (`ChatHook::applyRotation`) sobrescribe pitch/yaw/headYaw cuando hay una request vigente. Prioridades: PrioCombat=90 > PrioMovement=50 > PrioVisual=10. Esto por sí solo no gira el modelo local.

Aplicación client-side compartida: `Features/RotationHelper.hpp|.cpp` mantiene un objetivo visual suavizado y lo sustituye solo durante `PlayerRendererModelSetup` (hook de setup del modelo). Escribe temporalmente los campos de body/head yaw y pitch para esa llamada y los restaura incluso si el original sale por excepción; no modifica la cámara ni persiste cambios en Actor. **`updateClientRotation()` se ejecuta globalmente tras los callbacks `onFrame` de módulos en cada Present**, e internamente actualiza el body yaw por-frame e instala el hook si existe una request. No volver a atar ese avance a `Scaffold::onFrame/onTick`: KillAura y otros módulos necesitan el helper aunque Scaffold esté apagado. El helper visual respeta prioridad igual que `rot::request`: Combat > Movement. Cada módulo debe solicitar y limpiar su propia prioridad (`PrioCombat`/`PrioMovement`) para no borrar la rotación activa de otro módulo. Scaffold alimenta ambos caminos con el mismo pitch/yaw, sin checkbox de selección; al desactivarlo limpia solo `PrioMovement`.

## Herramientas de Diagnóstico (en el código)

| Herramienta | Qué hace | Dónde |
|---|---|---|
| `[SC]/[SCDBG]` | Estado del Scaffold, aborts con throttling 1s | Scaffold.cpp |
| `[MPPDump]` | Auto-dump primeros N MovePlayerPacket con actor resuelto y en movimiento (rotaciones, posición, velocidad) | ChatHook.cpp |
| `[RPDbg]/[SPDbg]/[BSdbg]` | One-shots de renderPos/BlockSource (desactivados por ahora) | Features/ |
| `[REMPos]` | Traza el primer remoto (raw +0xB4/+0x0C crudo/salida/intervalo), 250 líneas | Smoothing.cpp |
| `[PktTrace]` | Trace de paquetes salientes (cap por vtable, etiqueta por hook [SS]/[S0]/[S1]) | ChatHook.cpp |
| `[GMTrace]` | Hooks en slots de vtable GameMode (9/10/11/12/15), max 3 logs por slot | GameMode.cpp |

## Bugs Resueltos (lecciones aprendidas)

1. **Vtable patching**: en servidores el slot 59 no existe (vtable solo 19 slots); RemotePlayerAlt (0xE64588) solapa la región (slots 9/18 de Alt = 6/15 de Remote).
2. **Cámara en 2ª/3ª persona**: el lerp-por-cambio con la cámara orbitando (cada frame) quedaba pegado al frame anterior → desfase creciente. Fix: 3ª persona = raw directo.
3. **Camera smoothing**: getCameraView usa smooth::value con channel::Camera. **Nota: la cámara en 1ª persona usa smooth (delay 30ms) — si hay quejas de "lag" en 1ª, probar con 0 o menos delay.**
4. **Backtrack crash**: vtable patching → detours en game thread sin SEH. Solución: placeBlockFull (orquestador directo).
5. **getBlock firma**: (this, out, &pos) — no (this, x, y, z).
6. **MinecraftClient::tick NO corre a 20Hz** — medir el ritmo real por actor (EMA del intervalo entre updates), nunca asumir el tick rate del hook.
7. **raw (AABB +0xB4) y cur (mPosition +0x0C) saltan en momentos distintos** — física vs red. Anclar el render siempre al reloj de mPosition, offset AABB suavizado.
8. **isBlockItem por vtable**: lista incompleta — gate fuerte es Item* no-null; el orquestador valida el item de verdad.
9. **count() en ItemInstance (+0x18)**: offset no confirmado — solo como desempate suave, nunca exclusión.
10. **GLOB_RECURSE y renombres**: dos .cpp con el mismo basename en dirs distintos produce LNK4042 — renombrar Features/* para no chocar con Modules/impl/*/*.
11. **Suavizado de ESP: extrapolación Y lerp prev→cur saltan bajo jitter** — la extrapolación (`cur+vel·t`) se adelanta y la corrige con snap en cada update; el lerp con intervalo EMA salta cuando un update llega ANTES de lo esperado (frac<1 al resetear el segmento — BatchedPacketSender entrega en ráfagas). El buffer de retraso tiene el problema simétrico (salta con updates tardíos). Bajo jitter arbitrario el único esquema continuo es el **seguidor exponencial** (chase, C0 por construcción); lag≈ v·tau aceptable para visuals.
12. **Reloj del smoothing: SIEMPRE QPC, nunca GetTickCount64** — GetTickCount64 tiene granularidad ~15.6ms (timer del sistema). A 60/144 fps el dt por frame sale cuantizado (0/15.6/31.2) y cualquier suavizado por-frame avanza a saltos. Como la cámara (`smooth::value(channel::Camera)`) es compartida por TODOS los módulos que proyectan a pantalla, el escalonado se ve en cajas estáticas (PlaceHighlights) igual que en el ESP: la señal de que el problema es el reloj/cámara, no la posición de la entidad. Fix: `detail::nowMs()` (QPC) en el header, usado por `value()`, `gc()` y `renderPos()`.
13. **Encuadre de cajas (ESP/Backtrack/Blink): anclar en el CENTRO (+0x0C) y sin delay** — el motor ancla el modelo en el **centro horizontal** `mPosition` (+0x0C) ±halfW de ancho, y los pies en `+0xB4.y`. La cámara (`computeView`) y las cajas se anclaban en `+0xB4` (esquina min) → offset −0.3 en x/z. Entre cámaras y cajas de actor se CANCELABA (ambas −0.3), pero las cajas de **bloques** (coordenadas reales del mundo) quedaban desfasadas del render del juego. Fix: cámara y cajas de actor usan **centro x/z = `posExact().x/z` (+0x0C)** y **pies y = `pos().y` (+0xB4.y)**; Backtrack/Blink (cuya posición real/servidor ya ES centro +0x0C) solo mapean la Y a pies (`yOff = pos().y − posExact().y`). Alto = `height()` (+0xD4). Suavizado `interp` sin delay. Regla: toda caja 3D = centro +0x0C ± halfW, pies +0xB4.y .. +height, cámara en centro +0x0C.

## Pendientes / Ideas para la próxima sesión

1. **KillAura**: implementado en `Modules/impl/Combat/KillAura/`. REA confirmó que `GameMode` slot 14 (`attack`) apunta a `sub_1405103D0`; su wrapper llama a `sub_140500320`, que aplica el daño/hurt local. Para compatibilidad server-side, `GameMode::attack` envía primero `VInteractPacket` (acción 2 = left-click/attack, runtime ID objetivo) por la ruta del clic natural: `MinecraftClient+0xE8 → connector+0x98 → PacketSender vslot 2` (`sub_14004B9D0`); `sendToServer` no es la ruta del paquete gameplay. Después aplica el daño local predicho, pero el retorno solo marca éxito si se llamó al PacketSender. Layout del paquete del build actual y serializer `sub_140043B40`; no usar el layout de Solstice 1.21. Configs portadas desde Solstice que sí aplican a este build: modos Single/Multi/Switch, Rotate Mode None/Normal/Flick, Head Yaw (offset de 90° solo en packet headYaw), Strafe (alinea el modelo local al objetivo sin mover cámara; rotación server-side independiente), Dynamic Range, Randomize APS, Swing/Swing Delay, Attack Through Walls (raycast de bloques), Auto Disable on Dimension Change y Debug; se mantienen Range/FOV. Las opciones de Solstice que dependen de InventoryTransaction, spoof de equipo, arco/proyectiles privados, third-person o bypasses de protocolo nuevo no se exponen como toggles sin implementación compatible. Las configs persisten en JSON.
2. **MobEquipmentPacket**: ctor en 0x2631D0 — para el slot-sync del Scaffold en servers (el swap de slot sin el paquete puede rechazar places).
3. **AutoClicker, Step, Jesus**: módulos viables con la infraestructura actual.

## Comandos de Build

```
cmake --build build --config Release --target AzyreSDK
```
DLL resultante: `build/Release/AzyreSDK.dll` — **el juego debe estar cerrado o sin la dll cargada** para que el link pase (si no, LNK1104).

## Estructura de Archivos Clave

```
src/
  Features/Smoothing.hpp|.cpp     ← chase exponencial + buffer de historia
  Features/Rotation.hpp           ← sistema global de rotaciones
  Features/RotationHelper.hpp|.cpp ← rotación visual local del modelo con sustituciones acotadas al setup
  Features/PacketHelper.hpp|.cpp   ← buffer de paquetes del Blink
  SDK/ChatHook.cpp               ← hook sendToServer + applyRotation + tracer de paquetes
  SDK/Camera.hpp                  ← computeView + getCameraView (con smooth)
  SDK/Classes/GameMode.cpp         ← placeBlockFull (orquestador) + tracer GMTrace
  SDK/Classes/BlockSource.cpp      ← getBlockId (firma corregida, legacy id del out)
  SDK/Game.cpp                     ← scanner de vtables + fallback GameMode
  Modules/impl/Combat/Backtrack/   ← freeze + ghost Rise-style
  Modules/impl/Combat/KillAura/    ← target remoto + GameMode::attack + silent rotation
  Modules/impl/Movement/Scaffold/  ← lógica Solstice
  Modules/impl/Movement/Blink/     ← buffer de paquetes + caja del server
```


# nota adicional
- los módulos nuevos deben de tener su descripción y sus configuraciones en inglés, ya que romperia el esquema del cliente
- también se debe de ejecutar dos veces `cmake --build build --config Release --target AzyreSDK` para que el compilador registre correctamente el módulo

## Seguimiento: análisis REA de Animations (2026-10-20)

- El módulo existente está en `src/Modules/impl/Visuals/Animations/Animations.hpp|.cpp`. Ya incluye `Swing Time` y `Third Person Block`, `Only on Block` y rotaciones XYZ. Solstice también expone `Flux Swing`, `Custom Swing Angle`, `Only on Block` para ese ángulo y `Small Items`; `No Switch Animation` y `Test` están comentados/no-op en la referencia y no deben portarse como opciones sin backend.
- REA/IDA sigue analizando el target actual `C:\Users\ELIAS\AppData\Roaming\.minecraft_bedrock\versions\c2fc6b6e-7548-4fda-8936-c105c740b1e7\Minecraft.Win10.DX11.exe`, PE x64, base `0x140000000`, SHA-256 `d5683d2362c62e76dc9e49f8ad08e1462b6f4a8120fca981eb22955368ed38ab`. Provider: IDA MCP adapter 1 / upstream `ida-pro-mcp 1.1.3`.
- **Candidato confirmado para la ruta de swing en primera persona:** `sub_1402FB690` (`0x1402FB690`). El caller observado es `sub_14017CE30` (`0x14017CE30`), callsite `0x14017D334`. La rutina consulta el actor y el item equipado, obtiene/interpola el progreso de swing, configura la matriz y acaba llamando a `sub_1402FA2F0` para dibujar el item.
- **Transformación específica del swing:** `sub_1402FC5A0` (`0x1402FC5A0`), llamada desde `sub_1402FB690` en `0x1402FB950`. La asamblea observa interpolación de valores de rotación del actor con partial tick, curvas con raíz cuadrada/clamps, y dos llamadas a `sub_140088410`. `sub_140088410` usa una matriz eje-ángulo; `sub_140067630` aplica traslación y `sub_140067800` escala. Esto valida el camino de matriz para investigar `Custom Swing Angle`/`Small Items`, pero **no identifica todavía** el global exacto del ángulo configurable ni el punto de parche de `Flux Swing`.
- REA Evidence útiles: `ev_25cc29a36a2be0abc6a7d38038764a4da3b3a129d63c6f59a6e4da2de3677f9f` (`sub_1402FB690`); `ev_2b2f740a5c05183ecba12c38f689ba67e405a6c2f9411bc43b769e52077ed85f` (`sub_1402FC5A0`); `ev_a3f01cdb249423eb16806aee60bbbc38475ffd2b1ce5e445c3ac5082c1a06931` instrucciones de `sub_1402FB690`; `ev_b89ebc8470f20ea07dbf72ac7ebf9cc386ba6d1d6666d87b1e96b1526d4bd30a` (`sub_14017CE30`); `ev_56d1ac960e2b05c0e9dbb8b9721c63c8dff0c4816bb665b701a07a9cf01f3792` instrucciones/callsite; `ev_b0b36a30d4067fd26f2391fbc5a3e75cbbf0166ed36da90feee45a9b9b9fcc32` (`sub_140088410`); `ev_21a41eb32ad5b75223d86c7885a83723a2170c347f0e8e757eb375b6fab4b75c` (`sub_140067630`); `ev_ba764a5f69587cc826b3479588457486567ea2568168bc8201ad3e9ef82d323c` (`sub_140067800`); `ev_aac6b880aedc00e68d0d51622b6923ff228eb8bb939435428134ae199e5a4cc3` xrefs a `sub_1402FB690`.
- Limitación concreta del adapter IDA actual: `read_bytes` e `inspect_native_instruction` no están declarados; no insistir con ellos. El dossier reporta cuerpo/xrefs tipados incompletos, aunque `analyze_function`, pseudocódigo, asamblea e `xrefs` responden. No confundir pseudocódigo decompilado con fuente ni inferir un global desde las constantes matemáticas.
- Próximos probes: identificar el equivalente de `BobHurt`/matriz del item para `Small Items`; localizar el load RIP-relative/global que Solstice llamaba `TapSwingAnim` antes de exponer `Custom Swing Angle`; ubicar el `E8` de `FluxSwing` y comprobar qué efecto suprime antes de permitir togglear un patch. Después implementar solo settings con target y reversión/restauración verificados; build Release dos veces.
- Seguimiento de constantes: la constante `-80.0f` en `0x140B530E4` tiene xref en `sub_1402FB690` (`0x1402FBC02`) dentro de la rama `item use action == 4`; se considera pose/uso de item hasta demostrar lo contrario, no el slider general `Custom Swing Angle`. `-0.2f` (`0x140B52F10`, xref desde `0x1402FBB26`) y `-0.6f` (`0x140B52F58`, xref desde `0x1402FC1E6`) también se usan en la ruta de transformaciones del renderer, pero sin correspondencia probada con la traslación de Solstice `(0.5, -0.2, -0.6)`. La firma antigua `BobHurt` y la firma de callsite `FluxSwing` de Solstice no aparecen como patrón exacto en el EXE actual. Un escaneo local de floats halló `+0.5f` en `0x140B5261C`, pero es muy común (muchos cientos de xrefs), por lo que no es un ancla útil sin un flujo de datos más específico.
- Mejor candidato de hook para modificar la matriz asociada al swing/item: `sub_1402FC5A0`. El callsite observado en `sub_1402FB690` prepara `rdx=actor`, `xmm2=partialTick`, `r9=rsi` (matriz que la rutina continúa usando para draw), y el valor derivado del swing antes de llamar. Es buena frontera para experimentar con `Small Items`/transformación, pero falta verificar en runtime el orden de composición frente al callback BobHurt y el alcance a todas las manos/items; no tratar aún como equivalencia exacta del evento de Solstice.

### Sondeo adicional REA — matriz previa al draw (2026-10-20)

- `sub_1402FA2F0` (`0x1402FA2F0`) se confirma como una rutina común de preparación/dibujo del item: arma su matriz temporal desde el stack de render, ejecuta el draw de item y llama a `sub_1402FA220` para finalizar el estado. La llama `sub_1402FB690` en `0x1402FC0B3`, después de aplicar las transformaciones de la pose de swing. Xrefs: `0x1402C270E`, `0x1402CDC5A`, `0x1402CE18C`, `0x1402CE1AC`, `0x1402D2AB6`, `0x1402D2AD8`, `0x1402FC0B3` más referencias de datos.
- Es un **candidato de frontera para `Small Items`**, porque queda inmediatamente antes del draw; falta demostrar si todos sus callers usan la matriz del item en mano y si una traducción ahí conserva el orden de transforms de Solstice. No aplicar un hook general todavía.
- `sub_1402FC9F0` es otra ruta del renderer, con transformaciones y estado de item/mano; no es el callsite directo del swing investigado.
- `sub_1402FB690` contiene dos ramas diferentes: la curva/progreso de swing común y ramas por `item use action` (`4`/`5`). El `-80°` hallado cae en la rama de acción 4, por lo que queda descartado como global de Custom Swing Angle. La búsqueda debe centrarse en la parte común de la curva, no en la pose de uso.
- Nuevas Evidence: `ev_eee91ebb016e56cbb36ab43cd6672a938c6f826d7ccba36242b5e2030557e223` (dossier `sub_1402FA2F0`); `ev_28e2aa5601cf347bba89537cf7b36b57c315b5a08cb9d400b7d3a5ff2e7e025d` (xrefs a `sub_1402FA2F0`); `ev_968e05fb3a929ea706d3344844738d15cd2e24ec3413832ea5be144e8628b83a` (`sub_1402FC9F0`); `ev_68b4dbd770901a140f8992593b0a4561facc4087288e148367689e0bc273f3d2` (`sub_1402FC930`).
- Xrefs de constantes en la parte común del swing no aíslan una variable configurable: `0x140B52C48` (`ev_d12200cde2daaa5ff3c480791a680c442e509904a895833ddc2b05119e28939f`), `0x140B52B24` (`ev_086d30796fddc20a491f75d01296aed14686bf431bcd36b2b1f787ee47291b3c`), `0x140B52BC0` (`ev_ea8c07540b356ef8f82f495d0c548aaa28fbd9c4d976d9a707d683900d657fc0`) y `0x140B530A4` (`ev_fed6eafe81992997b72488348fc155dc67f3e8a73a846d5feb0de35912f51bfe`) tienen referencias cruzadas por muchas rutinas. No parchear esos globals por su valor aparente.
- Para restringir `Small Items` a primera persona, el callsite exacto observado es `0x1402FC0B3` dentro de `sub_1402FB690`, hacia `sub_1402FA2F0`. Esta última es compartida y también la llaman renderers de entidades (`sub_1402CDA40` / `sub_1402D27E0`), así que un hook global sin filtro afectaría otros modelos.

## Animations — prueba de candidatos REA implementada (2026-10-20)

- `Small Items` y `Custom Swing Angle` ya están expuestos en `Animations` con settings en inglés y persistencia JSON.
- El hook usa `sub_1402FA2F0` (`RVA 0x2FA2F0`) y filtra por return address `RVA 0x2FC0B8` — el único caller de primera persona observado desde `sub_1402FB690` — y por actor local. El resto de callers de item/entity render queda intacto.
- La ruta abre un frame temporal con MatrixStack `xmmword_140FDD850` / `sub_140793F40`, aplica `Small Items` como translate `(0.5,-0.2,-0.6)` vía `sub_140067630`, llama el renderer original y restaura el cursor del stack. La restauración ahora es incondicional al cursor pre-push: el renderer anidado podía dejar el cursor en otro nivel y el cleanup condicional dejaba filtrarse la transformación a la hotbar/crosshair/chat. No se encontró evidencia que justifique cambiar offsets de `GuiComponent`; aún requiere prueba visual dentro del juego.
- `Custom Swing Angle` agrega la diferencia respecto al ángulo nativo supuesto `-80°`, escalada por `sin(sqrt(swingProgress)*pi)`, como rotación X antes del draw. El eje X y el valor `-80°` como swing común siguen siendo una **inferencia**; REA ubicó el `-80°` literal en una rama `item use action == 4`, no en el swing común. Ajustar después de una prueba in-game. `Only on Block` limita el ajuste al botón derecho con item sostenido.
- `Flux Swing` sigue sin implementarse: el callsite de Solstice es de otra build y no se encontró una coincidencia válida en el ejecutable actual. No NOPear una llamada elegida por aproximación.
- Offsets agregados en `src/SDK/Offsets.hpp`: renderer/draw, callsite de primera persona, MatrixStack push y helpers translate/rotate. Confirmados con REA en el target documentado arriba.
- Build normal completado dos veces tras el arreglo de `MC_LOG_WARN` (macro no definida en este proyecto): `cmake --build build --config Release --target AzyreSDK`; DLL en `build/Release/AzyreSDK.dll`. No se hizo prueba visual runtime en esta sesión.

## FreeLook — implementación inicial (2026-10-20)

- **Estado: prototipo incompleto; no cumple el FreeLook clásico.** La implementación actual (`src/Modules/impl/Visuals/FreeLook/`) deja que Actor yaw/pitch sigan siendo la cámara y solo congela la pose local y los paquetes. No fuerza tercera persona y el movimiento sigue ligado al yaw de la cámara.
- Referencia Rise 6.9.5: guarda la perspectiva y yaw/pitch originales, fuerza `thirdPersonView = 1`, mantiene yaw/pitch de cámara separados a partir de deltas del mouse y restaura al soltar/desactivar. Solstice separa rotación de cámara de head yaw del actor, congela head yaw en BaseTick y en paquetes, y guarda/restaura el estado de cámara/perspectiva.
- **Diseño requerido para la versión correcta:** tercera persona forzada durante FreeLook; cámara con rotación propia; rumbo/movimiento del jugador ligado a la orientación guardada, sin cambiar al girar la cámara; al desactivar, restaurar la perspectiva y orientar la cámara al jugador sin saltos.
- Para adaptar eso a este build Bedrock faltan los puntos nativos de input-look y estado/cambio de perspectiva. `off::fn::MouseDeviceFeed` sigue en cero y `RendererCamera` es solo una lectura confirmada para proyección; no asumir que escribirlo cambia la perspectiva. No simular F5 ni escribir offsets no confirmados. La búsqueda REA/IDA de estos puntos debe ser focalizada para no saturar la sesión.
- Las dos compilaciones Release anteriores solo validan que el prototipo compila; no validan el comportamiento funcional.
