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

### GameMode — cadena de rotura de bloques (vtable 0xE8A1A0, 19 slots)
Vtable leída del PE en disco (RVA 0xE8A1A0 → offset), no de la memoria del proceso:
```
 0 0x1405100C0   1 0x140510450   2 0x140510680   3 0x140510BE0
 4 0x140510FE0   5 0x1405111F0   6 0x140511270   7 0x140511310
 8 0x140031050   9 0x140511BC0  10 0x140511820  11 0x140511700 (useItem)
12 0x1405113F0 (useItemOn)      13 0x1405103C0  14 0x1405103D0 (attack)
15 0x1405118A0  16 0x140031050  17 0x140031080  18 0x140042F00
```
Firma común de los tres primeros (ABI MSVC x64: this=rcx, player=rdx, BlockPos*=r8, face=r9b):
```
startDestroyBlock(this, Player*, BlockPos const*, uchar face)   // slot 1
continueDestroyBlock(this, Player*, BlockPos const*, uchar face) // slot 3
destroyBlock(this, Player*, BlockPos const*, uchar face)         // slot 2
```
- **slot 1 (0x510450)**: si `progress == 0` arma el `PlayerActionPacket` (vtable `off_140E63B58`, header type=2/field=1, x/y/z + face + tick de `player+0x198`) y lo manda por el `PacketSender` interno (`this+8`, slot 2). Después fija `NextDestroyTime`.
- **slot 3 (0x510BE0)**: el motor real de la rotura. Si `now < NextDestroyTime` retorna; si la pos pedida no es la que tiene en curso (`DestroyPosX/Y/Z`) **reinicia el ciclo, limpia el progreso y reenvía el START**. Si coincide: `dt = now − LastUpdateTime`, `destroySpeed = block.vtable[0x1A8]`, multiplicadores de encantamiento (`powf` con 1.44/1.2/0.49/0.7) y
  `DestroyProgress += dt * destroySpeed * 20.0` (constante `0x140B52B80 = 20.0f`).
  Si `DestroyProgress >= 1.0f` (`0x140B527C0`) llama al slot 2, limpia `+0x34..+0x3B` y fija `NextDestroyTime = now + DestroyDelayMs*1e6`.
- Layout del estado (offsets de instancia, `off::gamemode`): `+0x28/0x2C/0x30` BlockPos en curso, `+0x38` float progreso, `+0x40` double último update (segundos), `+0x58` uint64 próximo tick permitido (ns), `+0x60` próximo efecto, `+0x68` int ms de delay entre roturas, `+0x78` int ms de partículas. `player+0x1F0` = BlockSource, `player+0xFCCh` = modo (1/3 → rama instantánea de creativo), `player+0xFC8h` = byte de permiso.

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

### NoJumpDelay (Movement) — NUEVO
- **Propósito**: elimina el retraso de ticks vanilla al mantener la tecla de salto presionada.
- **Lógica**: en cada tick donde el jugador está en el suelo (`onGround()`) y aterrizando (`vel.y <= 0.08`), inyecta el impulso de salto configurado (`jumpHeight_`, default 0.42) y actualiza `onGround(false)` inmediatamente para encadenar saltos continuos sin pausas.
- **Settings**: Jump Height, Require Space, Only Moving.

### Parkour (Movement) — NUEVO
- **Propósito**: salta automáticamente al alcanzar el borde exacto de un bloque.
- **Detección**: mientras el jugador esté en tierra y desplazándose en la dirección deseada (`wishDirFromYaw` / velocidad), proyecta una sonda hacia el borde del AABB (`0.3 + edgeDistance`). Si la celda debajo de la sonda es aire (`!isSolidId`) y no hay pared enfrente a nivel de pies, detecta el vacío/salto y gatilla el impulso vertical con cooldown de 250ms.
- **Settings**: Edge Distance, Jump Height, Check Sneak, Check Fall Depth.

### FakeLag (Movement) — NUEVO
- **Reusa el buffer del Blink** (PacketHelper) cíclicamente: retiene MovePlayerPacket hasta cumplir hold time (ms) O depth (paquetes), suelta en ráfaga FIFO y reinicia el ciclo. Sin hooks propios.
- **Conflicto resuelto**: el buffer es compartido — si el Blink está activo, FakeLag se desactiva solo con aviso al chat (un solo dueño). Ambos límites a 0 se clampan a depth=6 (evita un "Blink duplicado" infinito).
- **Release on damage**: onHurt del manager suelta el burst al recibir daño (anti-kite). Gap natural: entre deactivate→activate pasa a lo sumo 1 paquete en vuelo.

### FastBreak (Misc) — NUEVO
- **No arma paquetes**: reusa `GameMode::continueDestroyBlock` (slot 3) forzando antes `DestroyProgress` y `NextDestroyTime`, así el flujo nativo (rotura local + paquete de destroy) sigue intacto.
- **Gate**: solo actúa si `DestroyProgress > 0` — o sea, con una rotura ya iniciada por el juego y la BlockPos en curso sincronizada con el server. Con progreso 0 no toca nada (forzarlo reiniciaría el ciclo y mandaría un START extra).
- **Requisito de pos**: se le pasa la misma BlockPos que el GameMode tiene en `+0x28`; una pos distinta hace que la función trate el bloque como nuevo y reenvíe el `PlayerActionPacket`.
- **Modos**: Instant (`progress = 1.0` → rompe en la primera llamada) y Multiplier (`progress *= speed`, clamp 1.0). Settings en inglés: Mode, Speed, Require click, Only survival, Debug (`[FastBreak]` en el log).

### Blink (Movement)
- PacketHelper.cpp (renombrado para evitar LNK4042 por colisión de basename).
- Captura MovePlayerPacket en hkSendToServer (copia bytes, el original muere con el caller).
- Libera con `chathook::sendRawPacket` (los handles capturados del propio hook).
- Caja en lastSent con **encuadre del ESP**: `lastSent` ya es centro +0x0C, solo se mapea la Y a pies (`yOff = local.pos().y − posExact().y`), `height()`, suavizado `smooth::interp` (sin delay). Contador de paquetes.

### Custom Chat (Interface) — reemplazo Rise
- **Archivos**: `src/Modules/impl/Interface/Chat/CustomChat.hpp|.cpp`. Panel compacto anclado abajo a la izquierda, basado en Rise 6.9.5: historial de altura limitada, fondo oscuro, scroll e input en el mismo panel.
- **ChatScreen nativo**: no se instala ni se modifica `ChatOpenHook`. Cuando `LocalPlayer` existe, `KeyGateHook` y el gate de RAW neutralizan la tecla `T` para Bedrock, mientras CustomChat la detecta con `GetAsyncKeyState` y abre su panel. Sin `LocalPlayer`, `T` pasa intacta al juego. Mientras el panel está abierto, el hook neutraliza el resto del teclado de Bedrock.
- **Input**: usa `menu::setUiCapture` / `uiOwnsInput()` para el cursor virtual, neutralización de botones/ejes/teclas RAW y continuidad del overlay; la rueda se preserva tanto para ImGui como para el juego. No deshabilitar `CoreWindow::IsInputEnabled`: corta también los eventos de teclado y rueda que necesita el panel, congela el cursor y deja el chat sin input. El juego permanece input-enabled.
- **Historial**: se copian y vacían las dos listas del ChatHelper (`client+0x2B0` → helper, `+0x128` scrollback y `+0x140` HUD, stride 112), así no se dibuja el HUD vanilla. Orden LIFO observado en el código anterior, recorrido inverso; dedupe por `source|text` por frame y ecos propios con TTL de 6 s.
- **Formato y fuente**: paleta Minecraft de 16 colores desde `§0`–`§f`; `§l` usa `dx11::gameFontBold()` (Mojangles Bold), texto regular `dx11::gameFont()` (Mojangles).
- **Envío y teclas**: `chatsend::send()` por TextPacket; `T` abre chat, `/` abre el campo con prefijo `/`, Escape cierra y Enter envía. Open Key configurable como tecla secundaria; T es el default.
- **Settings** (inglés): Font Size, Width, Open Height, Max Closed Height, History, Disappearance (ms), Animations, Background, Text Shadow, Open Key, Clear History.
- **Pendiente de validación in-game**: cursor de ImGui, apertura/salida de input CoreWindow, scroll del historial y orden real de mensajes en el build 0.15.10.0.

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

## TargetHUD — la vida del jugador remoto NO se sincroniza (evidencia 0.15.10)

En este build el cliente **nunca recibe la vida de otro jugador**. La barra de TargetHUD
no puede leerla: se estima. Evidencia que cerró la búsqueda:

- `sub_1404DC970` es el inicializador de atributos del actor: busca `[actor+0xCB0]`
  (attribute map) con `sub_140445D40` y escribe el valor del descriptor de
  `generic.health` (`dword_140FD5F58`) directo en `AttributeInstance+0x84` con
  `0x41200000` = **20.0f**. Ese 20 es el que lee `Actor::currentHealth()` para
  siempre: no hay escritor de red (los xrefs al descriptor son solo
  `sub_1404DCBB0` getHealth, `sub_1404DCC00` getMaxHealth, este inicializador y
  las 3 rutas de GameMode que aplican daño **local**).
- `[AttackTrace]` muestra `hp=20.00->20.00 cache=20.00->20.00` alrededor del ataque
  nativo: ni `currentHealth()` (attr `+0x84`) ni el int legacy `off::player::Health`
  (`+0x220`) cambian al golpear.
- `ActorEventPacket` (vtable `0xE644C8`, slot 3 = `0x140262C30`, que lee runtimeId
  `+0x10`, event byte `+0x18` y data varint `+0x1C`) esta bien identificado, pero el
  detour de MinHook **nunca se ejecuta** (`[THEventRead]` = 0 en todas las sesiones,
  aunque el log confirma "Hooked"). `packets::EntityEventPacket` queda como helper
  sin uso: no volver a basar la deteccion de golpes en el.
- `off::player::HurtTime` (`+0x0CD8`) **no es hurt time**: el byte devuelve 0..5 y
  255 (0xFF = int -1 en un campo vecino). Descartado; era el que restaba 1 HP falso.

**Solucion implementada**: `Features/HitFeed.hpp|.cpp` (ring atomico de impactos) +
`ItemInstance::attackDamage()` (tabla por id legacy: espadas 4/5/6/7, hachas 3..6,
picos 2..5, palas x.5, puño 1). `GameMode::attack` anota `(target, dano del arma)`
cada vez que el `VInteractPacket` salio (`sentToServer`), y TargetHUD consume el
registro por frame y descuenta ese dano del ancla nativa. Settings en ingles:
`Damage Source` (Weapon / Custom) y `Damage Per Hit` (calibracion manual para
servers con dano propio o espadas encantadas, que la tabla no contempla).
Reanclaje: cambio de objetivo y teleport > 24 bloques (respawn) vuelven al valor
nativo. Queda fuera el dano de terceros y el que no sale por `GameMode::attack`
(clics manuales); `[THHealth]` traza `hits`/`dealt` para verificarlo.

**Ventana de inmunidad (0.15.10)**: KillAura dispara 10+ ataques por segundo, pero el
server solo aplica dano una vez por `hurtTime` (10 ticks = 500 ms). Descontar cada
ataque vaciaba la barra ~7x mas rapido que la vida real (log 0.15.10: 20 -> 13 -> 7 -> 0
en ~1.5 s con `hits=0` en la linea, porque el trace solo reporta el frame muestreado).
Fix: `TargetHUD::kHurtImmunityMs = 500` **constante del juego, no ajuste** —
`lastDamageApplyMs_` limita a un golpe descontado por ventana y descarta el resto (como
hace el server). Se resetea al cambiar de objetivo, al reanclar por respawn/teleport y
en `onEnable/onDisable/onShutdown`. `[THHealth]` ahora imprime `applied` e `immunity`;
`source=immune window` marca los golpes descartados. Pendiente conocido: si el kit del
server tiene mas de 20 HP maximos, la barra sigue topeada en el maxHealth no replicado.

**`attackDamage()` devuelve 1.0 en runtime** (visto en `[AttackTrace] dmg=1.00`): el id
leido de `Item*+0x36` (`off::item::LegacyId`) no matchea ninguna entrada de la tabla o
el stack en mano no expone id valido. `[AttackTrace]` ahora loguea `held-id` para
diagnosticarlo con evidencia; hasta confirmarlo, calibrar con `Damage Per Hit`.

## Herramientas de Diagnóstico (en el código)

| Herramienta | Qué hace | Dónde |
|---|---|---|
| `[SC]/[SCDBG]` | Estado del Scaffold, aborts con throttling 1s | Scaffold.cpp |
| `[THHealth]` | Vida del objetivo: raw/cache nativos, hits consumidos del HitFeed, daño descontado, estimado y mostrado | TargetHUD.cpp |
| `[AttackTrace]` | Estado del objetivo antes/despues del ataque nativo (hp/cache/hurt) | GameMode.cpp |
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

14. **InteractPacket: el sender puede ser la vtable ABSTRACTA (0xE699D0)** — REA confirmó que `PacketSender` base no implementa `send`: sus ranuras 1..4 apuntan al thunk `_purecall` (`fn::PureCall = 0x981B06`). Despachar contra ella es un **fast-fail de MSVC que `__try/__except` NO captura** (termina el proceso, no lanza SEH). El envío real solo existe en `BatchedPacketSender` (0xE69A00, slot 2 = `sub_14038B720` → serializa y reenvía a `sendToServer`). `PacketSender::send` ahora rechaza el thunk con log `[PktSend] rejected: <motivo>`. Además `InteractPacket::sendAttack` ya no falla mudo: loguea `[Attack] rejected: <motivo>` (target ilegible / runtime id 0 o −1 / sender rechazó) y `GameMode::attack` resuelve la vtable **antes** de enviar.
15. **Orden rotación → ataque (validación de mirada del server)** — el server valida el `InteractPacket` contra el **último `MovePlayerPacket` recibido**, no contra la request local de `rot::`. Atacar en el mismo frame en que se pide la rotación (o justo tras cambiar de objetivo) manda el golpe mientras el server aún te ve mirando al lado anterior. `ChatHook::applyRotation` incrementa `rot::markApplied()`; KillAura guarda `rot::appliedCount()` al cambiar de objetivo y retiene el ataque hasta que salga un paquete con la rotación nueva, con **timeout de 100 ms** (`kRotationAckTimeoutMs`) para no romper el CPS si no hay tráfico de movimiento.
16. **Gate de KillAura: `ImGui::WantCaptureMouse` era demasiado ancho** — también dispara cuando ImGui toma el ratón para ventanas invisibles, y apagaba el aura por tramos largos (los huecos de 20-100 s sin `[AttackTrace]` en el log). El gate correcto es `menu::visible() || Game::get().client().chatOpen()`.

17. **ImGui 1.92: `ImFont::FontSize` NO existe y `PushFont(font)` está obsoleto** — el campo se llama `LegacySize` (el tamaño pasado a `AddFontXXX`) y `PushFont` pasó a `PushFont(font, font_size_base_unscaled)`. Para conservar el tamaño actual: `ImGui::PushFont(font, 0.0f)`. `ImFont::CalcTextSizeA(size, max_w, wrap_w, begin, end)` y `AddText(font, size, pos, col, ...)` siguen igual.
18. **Ocultar el chat nativo: capturar y vaciar en el MISMO instante** — el HUD nativo dibuja desde `ChatHelper` (`client+0x2B0` → `+0x128`, vector de 112 bytes por mensaje). Vaciar la lista (escribir `begin` en `base+8`, equivalente exacto a `vector::clear()`: size 0 sin liberar ni mover el buffer, nunca se toca `begin` ni el cap) oculta el chat del juego sin hooks. Pero si el drenado corre a menor tasa que el vaciado, **se pierden mensajes**: hay que leer y vaciar en la misma operación (cada frame), no un poll a 50 ms con limpieza aparte. Con `Hide Vanilla Chat` apagado se vuelve al muestreo con dedupe por el último texto conocido.
19. **Cursor: NUNCA escribir `CoreWindow::PointerCursor`; la UI usa el cursor software de ImGui** — `GetCursorInfo` puede seguir indicando oculto en UWP incluso cuando `CoreWindow::PointerCursor` ya está asignado por inventario/pausa. Leer `PointerCursor` es válido para decidir si ImGui debe dibujar su cursor; no asignarlo. Estado actual: `io.MouseDrawCursor = !realVisible && (uiOwnsInput() || corePointerAvailable)`.
20. **Captura pegada = HUD editor activo con el menu cerrado** — `uiOwnsInput()` es `visible() || hudEditing() || uiCapture()`. El boton "Edit HUD" vive en el menu, pero `g_hudEditing` sobrevivia al cierre de este: con eso `uiOwnsInput()` no volvia nunca a false y el juego quedaba **permanentemente** sin raw input y con el cursor de la UI encima (sintoma: "el cursor y el scroll estan muertos en el juego, en la GUI va normal"). Fix: al ocultar el menu se cierra el editor (guardando posiciones), `renderHudEditor()` exige `g_visible`, y `setHudEditing` pasa por `input::refreshUiCapture()` como `setUiCapture`.
21. **La rueda del juego NUNCA se bloquea** — el gate de captura anulaba el wheel del juego (raw input en bloque, `WM_NULL` en Peek/GetMessage, `return 1` en el LL hook y `return` en `MouseDevice::feed` con `action==4`). Con cualquier captura pegada eso dejaba el scroll del juego muerto de forma permanente. Fix: el wheel no se anula en ninguna ruta (ni raw, ni LL, ni Peek/GetMessage, ni `MouseHook::hkFeed`); en el raw input se conserva `RI_MOUSE_WHEEL` + `usButtonData` y solo se neutralizan botones y ejes. La GUI recibe su propia copia por `wheel::consume()` → `AddMouseWheelEvent`. Regla: bloquear botones/ejes/teclas del juego en la GUI es correcto; bloquear el wheel, nunca. **El scroll del juego tampoco depende del hover del cursor de la DLL**: si el cursor del CoreWindow esta congelado (ver bug 22), la rueda del juego funciona igual porque no pasa por nuestro estado.
22. **Con el puntero del CoreWindow oculto, `GetCursorPos` esta CONGELADO — el cursor de la UI debe ser virtual** — evidencia: `[CustomChat] dbg ... mouse=(934,255)` repetido durante segundos mientras el usuario movia el raton y scrolleaba, y `[WheelDBG][Input] ... posValid=1 mouse=(934,255)` fijo. El EXE del juego **no importa `user32.dll`** (escaneo del binario: 0 ocurrencias de `ClipCursor`, `SetCursorPos`, `GetCursorPos`, `ShowCursor`) — no usa puntero Win32; mueve la camara con los **deltas del raw input** (`lLastX/lLastY`). Cuando el CoreWindow oculta su puntero, Windows deja de actualizar la posicion absoluta: cualquier UI que lea `GetCursorPos` ve un cursor clavado ("el cursor esta bloqueado"). Fix: `wheel::consumeMouseDelta()` acumula `lLastX/lLastY` en `parseRawMouse` y `InputSystem` mantiene un **cursor virtual** (`g_virtualCursor`) que suma esos deltas escalados por cliente→DisplaySize mientras `uiOwnsInput()`; si el puntero real SI se mueve (menu principal, pantallas nativas) se re-ancla a el para no duplicar. `io.MousePos` sale del virtual y `io.MouseDrawCursor` lo dibuja. NO intentar `ClipCursor(nullptr)` ni `SetCursorPos`: el juego no los usa y no descongelan nada.
23. **Interceptar `OpenChatScreen` y devolver 0 rompe el input del CLIENTE (no solo el chat)** — con `ChatOpenHook` armado, pulsar `T` dejaba al juego en "modo UI sin pantalla": su puntero del sistema quedaba visible en gameplay (`[Input] juego cursor visible=1`) y dejaba de procesar mouse y scroll de forma permanente. Sintoma reportado: "el scroll, el mouse y la visibilidad del mouse mueren despues de abrir el custom chat, cosa que con el menu no pasaba". Como el hook se armaba con el modulo habilitado (no al abrir el panel), bastaba una pulsacion de `T` para romperlo con el panel cerrado. Fix: no instalar `ChatOpenHook`; el panel se abre con `Open Key` y el chat vanilla (`T`/`/`) sigue operativo. Regla general: **nunca interceptar la apertura de una pantalla nativa devolviendo 0**; el cliente ya cambio su modo de input antes de llamar.

### CustomChat (Visuals) — diseno historico (ver "Custom Chat (Interface)" arriba)
- **AVISO**: esta seccion describe la version previa del modulo (panel sin ventana ImGui, `Open Key` propio y `CoreWindow::IsInputEnabled`). El modulo actual vive en `Modules/impl/Interface/Chat/` y su contrato es el de la seccion "Custom Chat (Interface) — NUEVO": apertura por `Open Key`, captura por `menu::uiCapture()` y KeyGate. Se conserva como referencia de los intentos descartados.
- **Qué hace**: reemplaza el chat nativo (puerto del `Chat` de Rise 6.9.5) con panel propio dibujado en `ImDrawList` sobre `GetForegroundDrawList()`, parser de formato `§` (colores 0-f, `§l` bold, `§r` reset; `§k/m/n/o` se descartan), **Mojangles** para texto normal y **Mojangles-Bold** para los tramos bold (`dx11::gameFont()` / `dx11::gameFontBold()`, ya cargadas desde recursos).
- **Ocultar el vanilla**: `captureNativeChat(true)` lee la lista del `ChatHelper` y deja el vector en size 0 en el mismo frame (ver bug 18). Nunca libera ni mueve el buffer, así que el `push_back` del juego reusa la primera celda y la memoria sigue válida. Reversible: apagar `Hide Vanilla Chat`.
- **Envío sin el input del juego**: `chatsend::send()` (clona un `TextPacket` real capturado por `ChatHook`, así que requiere haber mandado un mensaje vanilla una vez en la partida). Prefijo `.` → `ModuleManager::queueCommand()`. `Show Own Messages` añade `<nombre> texto` localmente.
- **La tecla de chat del juego YA NO se intercepta (build 22:33) — el hook de supresión crasheaba el cliente.** Evidencia: `[ChatOpen] native chat screen suppressed` y acto seguido el proceso muere con `0xC0000409` (STATUS_STACK_BUFFER_OVERRUN / fail-fast de ucrtbase, `ucrtbase.dll+0x11858`) en `CrashDumps\Minecraft.Win10.DX11.exe.*.dmp` y en el Event Log (22:14, 22:15, 22:24, 22:25). El handler de la tecla ya cambió el modo de input del cliente antes de crear la pantalla; devolver 0 lo deja en "modo UI sin pantalla" y su bucle de input termina reventando. `ChatOpenHook.cpp` sigue en el árbol pero **ya no se instala** (`chatopen::install/setIntercept/uninstall` eliminados de `CustomChat`). El panel se abre con **Open Key** (default `Y`, `handleHotkey` en `onFrame`) y se cierra con Enter/Escape.
- **Con el ChatScreen nativo abierto NO se drena la lista** (`hideVanilla_ && !client().chatOpen()`): la pantalla del juego es la que muestra historial e input mientras se escribe ahí; al cerrarse se reanuda el drenado y se captura todo lo acumulado. Sin esto el chat vanilla aparecía vacío.
- **Teclado silenciado por el propio módulo**: `setGameKeyboardMuted()` → `CoreWindow::IsInputEnabled(false)` se llama **solo desde el hilo de render** (`onFrame`), nunca desde el hilo del juego/hook, y se reafirma cada 250 ms mientras `open_`. El cursor del panel se pide con `ImGui::GetIO().MouseDrawCursor = true` en `onFrame` (InputSystem lo apaga en gameplay). Log: `[CChat] game keyboard muted|restored`.
- **Input propio**: ventana ImGui invisible (`##azyre_custom_chat_input`) con `InputText` + `EnterReturnsTrue`, historial con Up/Down (`SetKeyboardFocusHere` al abrir, `Escape` cierra). Mientras el panel está abierto se anula la velocidad horizontal (`setVelocityXZ(0,0,0)`) y se cierra solo si aparece un menú del juego o una pantalla nativa sostenida ~200 ms (`nativeScreenHeld`).
- **Ciclo de vida**: `open_` se cierra solo si aparece `client().chatOpen()` o el menú; `openAnim_` anima alto/alpha; las líneas caducan con `Message Lifetime` + `Fade Time` cuando el chat está cerrado.
- **Settings** (persisten en JSON): Hide Vanilla Chat, Background, Border, Text Shadow, Animate, Show Own Messages, Freeze Player While Typing, Opacity, Width, Open Height, Closed Height, Message Lifetime, Fade Time, Scale, Bottom Margin, Max Lines, Open Key.

## Pendientes / Ideas para la próxima sesión

1. **KillAura**: implementado en `Modules/impl/Combat/KillAura/`. REA confirmó que `GameMode` slot 14 (`attack`) apunta a `sub_1405103D0`; su wrapper llama a `sub_140500320`, que aplica el daño/hurt local. Para compatibilidad server-side, `GameMode::attack` envía primero `VInteractPacket` (acción 2 = left-click/attack, runtime ID objetivo) por la ruta del clic natural: `MinecraftClient+0xE8 → connector+0x98 → PacketSender vslot 2` (`sub_14004B9D0`); `sendToServer` no es la ruta del paquete gameplay. Después aplica el daño local predicho, pero el retorno solo marca éxito si se llamó al PacketSender. Layout del paquete del build actual y serializer `sub_140043B40`; no usar el layout de Solstice 1.21. Configs portadas desde Solstice que sí aplican a este build: modos Single/Multi/Switch, Rotate Mode None/Normal/Flick, Head Yaw (offset de 90° solo en packet headYaw), Strafe (alinea el modelo local al objetivo sin mover cámara; rotación server-side independiente), Dynamic Range, Randomize APS, Swing/Swing Delay, Attack Through Walls (raycast de bloques), Auto Disable on Dimension Change y Debug; se mantienen Range/FOV. Las opciones de Solstice que dependen de InventoryTransaction, spoof de equipo, arco/proyectiles privados, third-person o bypasses de protocolo nuevo no se exponen como toggles sin implementación compatible. Las configs persisten en JSON.
2. **MobEquipmentPacket**: ctor en 0x2631D0 — para el slot-sync del Scaffold en servers (el swap de slot sin el paquete puede rechazar places).
3. **Jesus**: módulo viable con la infraestructura actual (AutoClicker y Step ya existen).
4. **CustomChat — supresión total del teclado del juego**: el hook cubre la apertura del chat, no las demás teclas (salto pierde impulso por el zero de velocidad, pero `E`/`1-9`/`Q` siguen actuando). Falta localizar el equivalente de `KeyboardDevice::feed` (análogo a `MouseDevice::feed`, cuyo `off::fn::MouseDeviceFeed` sigue en 0) o bien hookear el push de pantallas `sub_1401CD550` para tragar pantallas mientras se escribe (riesgo: dejaría de aparecer la pantalla de desconexión; filtrar por vtable).
5. **REA/IDA**: el binario analizado es `C:\Users\ELIAS\AppData\Roaming\.minecraft_bedrock\versions\0.15.10.0\Minecraft.Win10.DX11.exe` (pack instalado `Microsoft.MinecraftUWP` = 0.15.10.0, SHA-256 `d5683d23…8ab`); la ruta `…\versions\c2fc6b6e-…` documentada antes ya no existe y hacía fallar al adaptador. Si el proxy responde `fetch failed` con el puerto 13337 escuchando, `close_binary` + `open_binary` reconecta el upstream. `analyze_function` y `search_procedures` siguen dando timeout 40504; usar `xrefs` + `procedure_pseudo_code` (rápidos) — `procedure_callers` no está soportado por este provider.
6. **CustomChat — ocultamiento por hook**: alternativa no destructiva a vaciar la lista; requiere localizar el render del HUD de chat (`ChatScreen`, vtable 0xE4BB48).

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
  Features/HitFeed.hpp|.cpp        ← registro de impactos melee (vida estimada del remoto)
  SDK/UI/Hooks/ChatHook.cpp      ← hook sendToServer + applyRotation + tracer de paquetes
  SDK/Client/Camera.hpp          ← computeView + getCameraView (con smooth)
  SDK/Gameplay/GameMode.cpp        ← placeBlockFull (orquestador) + tracer GMTrace
  SDK/World/BlockSource/BlockSource.cpp ← getBlockId (firma corregida, legacy id del out)
  SDK/Core/Game.cpp              ← scanner de vtables + fallback GameMode
  Modules/impl/Combat/Backtrack/   ← freeze + ghost Rise-style
  Modules/impl/Combat/KillAura/    ← target remoto + GameMode::attack + silent rotation
  Modules/impl/Movement/Scaffold/  ← lógica Solstice
  Modules/impl/Movement/Blink/     ← buffer de paquetes + caja del server
  Modules/impl/Misc/FastBreak/     ← progreso de rotura vía GameMode::continueDestroyBlock
  Modules/impl/Visuals/TargetHUD/  ← tarjeta del objetivo con vida estimada por HitFeed
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
- Offsets agregados en `src/SDK/Engine/Memory/Offsets.hpp`: renderer/draw, callsite de primera persona, MatrixStack push y helpers translate/rotate. Confirmados con REA en el target documentado arriba.
- Build normal completado dos veces tras el arreglo de `MC_LOG_WARN` (macro no definida en este proyecto): `cmake --build build --config Release --target AzyreSDK`; DLL en `build/Release/AzyreSDK.dll`. No se hizo prueba visual runtime en esta sesión.

## FreeLook — implementación inicial (2026-10-20)

- **Estado: prototipo incompleto; no cumple el FreeLook clásico.** La implementación actual (`src/Modules/impl/Visuals/FreeLook/`) deja que Actor yaw/pitch sigan siendo la cámara y solo congela la pose local y los paquetes. No fuerza tercera persona y el movimiento sigue ligado al yaw de la cámara.
- Referencia Rise 6.9.5: guarda la perspectiva y yaw/pitch originales, fuerza `thirdPersonView = 1`, mantiene yaw/pitch de cámara separados a partir de deltas del mouse y restaura al soltar/desactivar. Solstice separa rotación de cámara de head yaw del actor, congela head yaw en BaseTick y en paquetes, y guarda/restaura el estado de cámara/perspectiva.
- **Diseño requerido para la versión correcta:** tercera persona forzada durante FreeLook; cámara con rotación propia; rumbo/movimiento del jugador ligado a la orientación guardada, sin cambiar al girar la cámara; al desactivar, restaurar la perspectiva y orientar la cámara al jugador sin saltos.
- Para adaptar eso a este build Bedrock faltan los puntos nativos de input-look y estado/cambio de perspectiva. `off::fn::MouseDeviceFeed` sigue en cero y `RendererCamera` es solo una lectura confirmada para proyección; no asumir que escribirlo cambia la perspectiva. No simular F5 ni escribir offsets no confirmados. La búsqueda REA/IDA de estos puntos debe ser focalizada para no saturar la sesión.
- Las dos compilaciones Release anteriores solo validan que el prototipo compila; no validan el comportamiento funcional.

## Input: rueda, cursor y gate global (2026-11-07)

- **ESTADO ACTUAL (build 22:23): `src/Input` es el del remoto (`origin/main`), SIN gate global.** Se bajaron `InputSystem.cpp/.hpp`, `WheelHook.cpp/.hpp` y `MouseHook.cpp` desde `origin/main`; los únicos cambios locales son dos `#include` adaptados a las rutas de este árbol (`SDK/Game.hpp` → `SDK/Core/Game.hpp` y `SDK/Offsets.hpp` → `SDK/Engine/Memory/FunctionRvas.hpp`). La API remota es solo `input::{init,shutdown,updateFrame}` y `wheel::{setModule,install,uninstall,addWinrtDelta,addWinrtDirectDelta,addGameDelta,consume,lastValue}`. **NO existen** `setCapturing`/`capturing`/`setCaptureProvider`/`captureProviderActive`/`vanillaUiIdle`/`setGameInputMuted`; los bullets de esta sección que las mencionan documentan el diseño local anterior y **no se reimplementan** sin pedido explícito. Consecuencias vigentes: `ModuleManager::handleKeybinds` gatea solo con `client().chatOpen()`; `io.MouseDrawCursor = menu::visible() && !IsSystemCursorShowing()`; la rueda llega por `PointerWheelChanged` (directo y por dispatcher) y por el hook a `MouseDevice::feed`.
- **Scroll universal (build 22:33): foco por `Activated` + última posición válida.** El wheel se descartaba con `posValid == 0`, y fuera de mundo (menú principal, pantallas del juego) `WindowFromPoint` no resuelve al proceso → `io.MousePos` quedaba en `-FLT_MAX`, ImGui no tenía ventana hovered y el scroll moría. Ahora `InputSystem` guarda `g_windowActive` desde el evento `Activated` de CoreWindow (la única señal de foco fiable en UWP: `GetForegroundWindow` devuelve el HWND del host) y `g_lastValidMouse` con la última posición resuelta; si `!posValid && g_windowActive` se reusa esa posición y el wheel se entrega igual (`wheelTarget = posValid || (active && última posición)`). Traza: `[WheelDBG][Input] consume=… posValid=… focus=…`.
- **CustomChat debe dejar habilitado CoreWindow.** El mute global (`IsInputEnabled(false)`) se probó en el reemplazo nuevo y dejó `InDiag en=0`, `evK` congelado y sin eventos de rueda; también quitó el cursor virtual. El panel usa `uiCapture()` y el gate RAW existente. `WH_KEYBOARD_LL` no dispara en este cliente UWP y no debe considerarse su protección de teclado.
- **Regla del gate local (histórico, ya no aplica):** `wheel::setCapturing()` se armaba con `(menu::visible() || menu::hudEditing() || chat typing) && !nativeScreen`, donde `nativeScreen = client().chatOpen() || IsSystemCursorShowing()` y `typing = wheel::captureProviderActive()`. Con pantalla nativa abierta el DLL no tocaba el mouse: `gateRawInput` anulaba `lLastX/lLastY/usButtonData` y `gateMessage` los mensajes de mouse, así que dejarlo armado clavaba y escondía el puntero y mataba el scroll de la hotbar.
- **El cursor del SISTEMA es el detector de pantalla nativa que sí funciona (2026-11-07, build 22:12).** `GetCursorInfo`+`CURSOR_SHOWING`: en gameplay el cliente lo mantiene **oculto** (`show=0`, también con el panel del chat abierto) y sus pantallas (inventario, pausa, menú principal) lo **encienden** (`show=1`). El chat vanilla va aparte: lo identifica `chatOpen()` (vtable del tope = ChatScreen 0xE4BB48) porque lo mantiene con el cursor oculto. Con esto `nativeNow` deja de parpadear (el build 21:52 alternaba `capture ON/OFF` con el panel abierto porque `ClientUiIdle` oscilaba).
- **El vector de pantallas (`client+0x148`) NO sirve como "hay pantalla abierta" (2026-11-07, build 22:03).** En gameplay lleva **4-6 entradas siempre** (`depth=4/5/6`, top `topvt=0xE5E800`), así que `activeScreenDepth() > 0` es siempre cierto: el veto y `nativeScreenOpen()` bloqueaban la apertura del panel (`[CChat] chat panel blocked: client ui busy`) y el gate no se armaba nunca (`capture=0` con `menu=1`). `activeScreenDepth()`/`activeScreen()` quedan solo como **telemetría** (`depth=`, `topvt=0x…`).
- **`ClientUiIdle` (0x4F470) NO es un detector de pantallas: no usarlo para el gate ni para cerrar el panel (2026-11-07, build 22:01).** Lo que hace es preguntar a las pantallas del stack si "bloquean input" (su vslot 35) y devuelve `idle=0` tambien durante ~1 s **después de cualquier tecla**. Evidencia del log (build 21:52): con el inventario/pausa abiertos devolvió `idle=1` (21:52:56-21:53:11, `show=1` = cursor del sistema visible = UI del juego) y el gate se armó encima de la pantalla; y en gameplay devolvió `idle=0` justo tras pulsar T (21:53:18-19), con lo que `nativeScreenOpen()` cerraba el panel del chat al instante. Queda solo como telemetría (`idle=`).
- **Histéresis ASIMÉTRICA (2026-11-07, bug del cursor en menús del juego):** armar el bloqueo es **inmediato** (`nativeNow` → `nativeScreen = true` en ese frame) y desarmarlo exige **250 ms de idle continuo**. Antes eran 150 ms en las dos direcciones: al abrir el inventario/pausa el gate seguía armado 150 ms más y esos frames dejaban el cursor del menú recién abierto clavado.
- **El chat propio SÍ entra en el gate (2026-11-07, build 21:19).** Es la pieza que faltaba y la causa de "al activar Custom Chat el cursor empieza a fallar": con el chat abierto el juego seguía en modo gameplay, con el puntero preso en **1×1 píxel** y sin cursor dibujado, porque `captureProviderActive()` solo bloqueaba el **teclado**. Evidencia del log (build 21:14, chat abierto):
  ```
  21:15:21 [Input] state menu=0 hud=0 text=1 idle=1 chat=0 native=0 capture=0
  21:15:21 [Cursor] show=0 pos=874,695 clip=874,695-875,696 owner=other(14152) active=1
  21:15:29 [Cursor] show=0 pos=874,695 clip=874,695-875,696 owner=other(14152) active=1
  ```
  Ahora `typing` entra en `guiCapturing` y en `io.MouseDrawCursor`: la cámara no gira al mover el ratón, el click no ataca/coloca, la rueda no cambia el slot y el cursor existe (ImGui) porque el clip se suelta cada frame.
- **El clip se suelta en CADA frame, no una vez.** `ClipCursor(nullptr)` solo cuando cambia el estado no basta: el juego revisa el clip cuando le conviene (log: 1×1 a las 21:15:21, clip completo a las 21:15:30, 1×1 otra vez a las 21:15:32). `InputSystem::ReleaseMouseClip()` se llama por frame mientras haya pantalla abierta y solo toca el clip si `MouseClipIsCaged()` (rect ≤ 1×1): reescribirlo cuando ya está suelto no aporta y ensucia el estado.
- **El clip debe volver EXACTAMENTE a como estaba y el cursor del sistema se corrige solo (2026-11-07, build 21:39).** Tres reglas que salieron de comparar el log de 21:15 (`show=0`, estado sano) con el de 21:34-21:36 (`show=1` permanente, posición clavada):
  1. `RestoreMouseClip()` devuelve el rect **guardado tal cual, incluido el 1×1** de gameplay. Dejarlo suelto para siempre desincroniza la máquina de estados del juego: cree que su captura sigue puesta, no la vuelve a aplicar y, al abrir después sus menús, tampoco la suelta → la flecha del inventario/pausa queda clavada y el cursor del sistema visible hasta reiniciar.
  2. El clip se suelta con **cualquier pantalla abierta**, no solo la nuestra: `freeCursor = guiCapturing || nativeScreen`. Si el juego quedó desincronizado y no libera su 1×1 al abrir el inventario, forzarlo aquí es lo que devuelve el movimiento a su puntero. Además `RestoreMouseClip()` **no vuelve a encerrar** si `GetCursorInfo` reporta `CURSOR_SHOWING`: con el cursor visible hay una interfaz pidiendo puntero y el 1×1 lo dejaría clavado.
  3. `KeepSystemCursor`/`ShowCursor` **se eliminó (build 21:44)**: el compositor de la CoreWindow ignora el contador de `ShowCursor`, así que con `show=0` forzado también se quedaban sin flecha los menús del juego (log 21:40-21:42: `show=0` junto a `native=1` con el puntero moviéndose). La visibilidad la decide el juego; la DLL solo suelta/restaura el clip.
  La telemetría `[Cursor]` ahora incluye `native= idle= menu= text=`, para saber si el juego reportó pantalla abierta (`idle=0`) en el mismo instante en que se ve el cursor raro.
- **ELIMINADO el ScreenGate del CustomChat (2026-11-07).** Se hookeaban `ScreenStack::pushScreen` (`0x04A9B0`) y el keybind del inventario (`0x1C8D60`) para que, con el panel de chat abierto, ninguna pantalla del juego se apilara. **Rompe el cliente**: al pulsar E/ESC el juego ya entra en modo UI (oculta el puntero del sistema y captura/recentra el ratón) pero la pantalla nunca se apila → no hay cursor que dibujar ni pantalla con la que salir. Evidencia en el log:
  ```
  20:52:52 [ScreenGate] screen push blocked (stack 0000027D35202040)
  20:53:12 [ScreenGate] inventory open blocked
  ```
  Regla: **la DLL no hookea `ScreenStack::pushScreen`**. `ChatOpenHook` intercepta **solo** `OpenChatScreen`/`OpenCommandChatScreen`, y únicamente suprime la pantalla cuando el panel propio **sí** se abre (build 21:52: si `canOpen` falla devuelve `false` y el juego crea su ChatScreen normal). El hook a `OpenInventory` (`0x1C8D60`) **se eliminó por completo en el build 21:52**: devolver 0 ahí deja al cliente en "modo UI sin pantalla" (el handler ya cambió el modo de input antes de construir la pantalla) y su puntero de menús queda congelado hasta reiniciar. Evidencia del log: `[ChatOpen] inventory open blocked (mode=0)` a las 21:32:19, 21:32:24, 21:32:28 y 21:42:29, seguido de `[Cursor] show=0 clip=0,0-1920,1080` con `idle=1` y el puntero del juego clavado. Si el jugador abre otra pantalla mientras escribe, `CustomChat::onFrame` cierra el panel (`nativeScreenOpen()` con histéresis de 250 ms) y el juego recupera su input. `io.MouseDrawCursor` también exige `!nativeScreen` (nada de flechas falsas sobre un menú nativo).
- **`CoreWindow::PointerCursor` es INERTE frente al juego: no se usa (build 21:19).** El EXE no importa ninguna API Win32 de cursor; la visibilidad va por WinRT (`sub_1403F33D0` → factoría `Windows.UI.Core.CoreCursor` + `CreateCursor(type,id)`; `sub_1403F17E0` → `CreateCursor(0 = Arrow)` + `put_PointerCursor` sobre `host+0x28`; `sub_1403F18E0` → idem vía QI sobre `host+0x50`, IID `unk_140B3CD38`). Se replicó desde el hilo de UI (`Dispatcher().RunAsync`) y **no funcionó**: el juego repone el valor en su loop de HUD. Prueba en el log:
  ```
  21:15:15 [Cursor] PointerCursor <- Arrow
  21:15:16 [Cursor] show=0 ... own=1
  ```
  Por eso el cursor se dibuja solo con ImGui (`io.MouseDrawCursor`) y la asignación se eliminó. No reintroducir `PointerCursor` esperando que muestre el puntero.
- **El foco NO se puede gatear con `GetForegroundWindow`+PID.** MC Win10 es UWP: el HWND en primer plano puede pertenecer a otro proceso (host de la ventana), y `foregroundIsGame()` devolvía false siempre → `LLKeyboardProc` nunca tragaba teclas y la E del inventario pasaba intacta. Ahora `foregroundIsGame()` acepta PID propio **o** `mc::input::windowActive()` (estado del evento `Activated` de CoreWindow, expuesto desde `InputSystem`). Diagnóstico one-shot: `[Wheel] LL keyboard gate firing (vk= capture= chat=)`.
- **El hook LL de teclado NO dispara en este cliente UWP: el teclado del juego se corta con `CoreWindow::IsInputEnabled(false)` (2026-11-07, build 22:12).** La traza `[Wheel] LL keyboard gate firing` **nunca** apareció en ninguno de los logs del 7/11, así que `capturing()` no tragaba ni una tecla: lo escrito en el panel llegaba íntegro al gameplay (`[ChatOpen] native chat screen suppressed` repetido mientras se escribía = el juego sí recibía T/E/etc.). Cadena del fallo reportado: escribir una `e` dentro del mensaje abría el inventario con el gate del mouse armado encima → puntero clavado e invisible "hasta reiniciar". `InputSystem::setGameInputMuted()` ponía `IsInputEnabled(false)` mientras `guiCapturing` y lo restauraba en el mismo frame en que la GUI se cerraba (se re-afirmaba cada 250 ms por si el juego lo devolvía a true, y se restauraba en `shutdown()`). **Tras bajar `src/Input` del remoto (build 22:23) esa función ya no existe: el mismo mute vive ahora en `CustomChat` (`setGameKeyboardMuted`, re-afirmado por frame mientras `open_`) y solo cubre el panel de chat.** Es seguro porque ImGui lee `GetAsyncKeyState`/`GetCursorPos` por polling y la rueda entra por el hook de raw input, no por WinEvent. Log: `[CChat] game keyboard muted|restored`; si `IsInputEnabled` fallara sale `[CChat] CoreWindow::IsInputEnabled not writable`.
- **Telemetría `[Cursor]` (1/s)** — ahora **siempre**, también en gameplay puro (build 21:30): sin esa línea base no se puede distinguir si el cursor visible es el del juego o el nuestro. Campos: `show=` (CURSOR_SHOWING), `pos=`, `clip=` (`GetClipCursor`), `owner=self/other/pid` (dueño del HWND en primer plano), `active=` y `capture=`. Responde de una pasada: (a) si el puntero está preso (`clip` de 1×1), (b) si el juego pinta el cursor (`show=1` solo con pantallas nativas), (c) si el gate por foreground es viable (`owner=other` con el juego enfocado = no lo es).
- **Bug propio de la deduplicación: la capa INTERNA no debe neutralizar (2026-11-07, build 21:30).** Al añadir `g_rawDepth`, la lectura anidada (`user32!GetRawInputData` → `win32u!NtUserGetRawInputData`) dejó de alimentar el acumulador —correcto—, pero **seguía aplicando `gateRawInput` al MISMO buffer**. Resultado: con `capturing()` la capa interna ponía `usButtonFlags/usButtonData` a cero antes de que la capa externa parseara, así que el acumulador de la GUI nunca veía la rueda y el scroll del panel/menú moría justo cuando se armaba el gate. Regla: **solo la capa externa (`g_rawDepth == 0`) lee y neutraliza**; la interna devuelve el bloque intacto. `gateRawInput` debe aplicarse una única vez y siempre **después** de `parseRawMouse`. Además `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL` entraron en `gatedMessage`, para que el juego no mueva la hotbar con la rueda mientras la GUI propia captura.
- **La rueda se contaba DOBLE: un notch = `2.00` (2026-11-07, build 21:23).** Es la causa de "el scroll no funciona". `user32!GetRawInputData` es una capa sobre `win32u!NtUserGetRawInputData`, y como se hookean **las dos**, un único `WM_INPUT` de rueda entraba dos veces en `wheelFeed` (mismo delta ±120, mismo handle) → `consume=-2.00`. Evidencia del log (build 21:14): `21:15:11 [WheelDBG][Input] consume=-2.00` y a continuación `consume=-4.00` (dos notches duplicados). Arreglo en `WheelHook.cpp`: (1) `thread_local g_rawDepth` — solo la capa **externa** alimenta el acumulador y aplica el gate (ver el bullet siguiente: neutralizar tambien en la interna dejaba el scroll muerto); (2) regla *same-read*: mismo `HRAWINPUT` + mismo delta dentro de **25 ms** = relectura del mismo evento; (3) regla *same-gesture*: mismo delta desde otra ruta dentro de **60 ms** (antes 25 ms, insuficiente para el dispatch de WinRT). Sin esto, cualquier ruta nueva que también vea el gesto duplica el scroll.
- **Wheel:** el juego lo lee por **raw input** (`NtUserGetRawInputData`, confirmado en el log: "source active") y el pipeline `MouseMapper::dispatch` (`action == 4`, `data` int8 ±120). El `WH_MOUSE_LL` casi nunca ve la rueda con la ventana activa: por eso la vía real es `gateRawInput` (anula `usButtonFlags/usButtonData/lLast*` solo con `capturing()`), y `LLMouseProc` devuelve 1 únicamente con `capturing()`. Con pantalla nativa abierta `capturing()` es false → la rueda llega intacta al juego (`[WheelPass]`). Nunca anular `lLastX/lLastY` sin `capturing()`: es lo que clava el cursor de los menús.
- **Log de verificación:** `[Input] capture ON/OFF (menu= hud= text= native= idle= chat=)` en cada transición, más `[Input] state ...` 1/s mientras una GUI propia está abierta (sirve para ver qué señal mantiene el gate armado), `[WheelPass] wheel passed to game (capturing=0)` y `[WheelFeed] src= raw|raw-nt|raw-buf|ll|winrt|msg delta= verdict= accept|same-read|same-gesture dups=` (máx 1/300 ms, build 21:23) — con esta última se confirma de una pasada qué ruta reportó el gesto y si la deduplicación lo descartó. Sesión 20:20:55–20:21:23: `capture ON (menu=1)`, `capture ON (text=1)` y `[WheelPass]` una sola vez: el gate se armaba por el panel y por el chat, nunca por el juego. Sesión 20:52–20:53: sin transiciones de capture (solo el chat estaba abierto y en ese build el chat no capturaba mouse) y con las dos entradas de `[ScreenGate]` que delataron el bloqueo de menús. Sesión 21:14–21:15 (build 21:14): `capture ON (menu=1)` y, al abrir el chat, `state ... text=1 ... capture=0` con `clip=874,695-875,696` → el puntero quedó preso en 1×1 sin cursor: la prueba de que el chat debía entrar en el gate. Además `owner=other(14152) active=1` confirma que el HWND en primer plano NO pertenece al juego.
- **Evidencia del pipeline de rueda (REA, build 0.15.10.0, SHA-256 `d5683d23…`):**
  - `.?AVMouseMapper@@` → TypeDescriptor `0x140F637D0` → COL RVA `0xC4F280` → vtable RVA `0xB50D18` (slots `0x78ABD0`, `0x78ACA0`, `0x78B510`, `0x78B1F0`, `0x42F30`, `0x78B560`, `0x31050`, `0x78E880`).
  - `sub_14078B1F0` (slot 3) es el **dispatch de la cola de input**: recorre registros de 16 bytes `{i16 x, i16 y, i16 flags, pad, u8 action, i8 data}`; `action == 4` es la rueda y `data` es un **int8 con ±120**, convertido a float con `dword_140B52EB4` (negativo) / `dword_140B5232C` (positivo). Confirma la semántica `0x78/0x7F/0x80/0x88` de Solstice, pero **no** es `MouseDevice::feed`.
  - `HIDController` (RTTI `.?AU__IHIDControllerPublicNonVirtuals@@` @ `0x140F446A0`) es quien se suscribe a `Windows.Devices.Input.MouseDevice`; sus `TypedEventHandler` están en `0x140F44090` (mouse) y `0x140F43D70` / `0x140F43EF0` (teclado y char del CoreWindow). `MouseDevice` no tiene RTTI propio en este build (no es polimórfico) → `feed` es una función miembro plana y su RVA sigue **sin confirmar**: `off::fn::MouseDeviceFeed` queda en 0 y `MouseHook` inerte.
  - Límite del adapter IDA actual: `search_bytes`, `procedure_info` y `address_name` no están declarados; `procedure_pseudo_code` y `search_procedures` devuelven timeout 40504. Funcionan `procedure_assembly`, `search_strings`, `xrefs`, `open_binary` (con `provider_id: "ida"`).


## Animations — Swing Time, Flux y presets de Rise (2026-11-08)

- **`Swing Time` es checkbox + slider (`Swing Length`).** `swingTimeEnabled_` (JSON `swingTimeEnabled`, default true) gatea `g_swingTimeActive`; el hook de duración (`ActorGetSwingDuration` 0x4DA4A0) se instala igual y, con el toggle apagado, devuelve la duración nativa. `publishSwingTime(moduleEnabled, timeEnabled, ticks)` es el único punto que escribe `g_swingTime`/`g_swingTimeActive`.
- **Flux Swing = reimplementación del arco Java 1.8, no byte patch.** Se escaneó el EXE 0.15.10.0 con la firma de Solstice (`E8 ? ? ? ? F3 0F 10 85 84 01 00 00`): **0 coincidencias** (es de 1.21.x), así que no hay patch auténtico que portar y no se NOPea nada por aproximación. Rise tampoco tiene "Flux": su fuente solo implementa retiming (`swingSpeed`) y los modos `Swing Animation` no tienen backend. Implementación actual: los **tres** callsites nativos del swing se silencian (Y `0x2FBBD7`, Z `0x2FBBFE`, X `0x2FBC26`) y el arco se reconstruye en el último (X) con el orden y las curvas de `transformFirstPersonItem` de Java 1.8 — `R(f4·-20, Z)`, `R(f2·-20, Y)`, `R(f2·-80, X)`, donde `f4 = sin(p²π)` y `f2 = sin(√p·π)`. La cadena **nativa de Bedrock los tiene intercambiados** (Y usa la curva cuadrada, Z la raíz), que es justo la diferencia visible respecto a un cliente Java. Sobre el scale se suma `applyFluxDip` (`doItemUsedTransformations(p)`): `T(-0.4·f2, 0.2·sin(2π·√p), -0.2·sin(π·p))` para que el ítem viaje en vez de girar sobre su pivote. Traza: `[Animations] rebuilt arc: square … envelope … peak …` (5 líneas por enable).
- **Presets = lista `Block Animation` de Rise 6.9.5** (`TokenGraber/Rise-6.9.5/.../render/Animations.java`). `kBlockPresets` (28 entradas, índice 0 = `Custom`) guarda cadenas de ops `translate/rotate/scale/block` que se componen en el callsite del scale de primera persona. El switch aplastado por el decompilador se resolvió por los `labelN` anidados (`label293`…`label274`): `Dortware`/`Beta` comparten cuerpo (`label293`), `?` es `label250`, `Swing` `label251`, …, `None` `label274`. Firmas verificadas con dumpbin: `sub_140067630` = `(matrix*, float x, float y, float z)`; `sub_140088410` = rotate en grados; `sub_140067800` = scale; el translate se resuelve pero **no** se hookea.
- **Correcciones de fidelidad (2026-11-08, tercera pasada — cadena nativa verificada)**: (1) **ejes normalizados** — `GlStateManager.rotate` normaliza el eje y `sub_140088410` no (usa `sub_1400584C0`, eje crudo), así que ejes como `(0,15,200)` hay que normalizarlos en runtime o la rotación sale distorsionada; (2) **ganancia de traslación = 1.0** — la pasada anterior asumió `1/0.4` diciendo que "Rise compone después de `scale(0.4)`". Es falso: Rise **no** tiene ese `scale(0.4)`, solo `scale(d1)` con `d1 = 1`. La cadena nativa de Bedrock aplica `scale(0.4)` al final y encoge ítem **y** traslaciones por igual, así que la proporción respecto al ítem ya es la de Rise; compensar con `2.5×` era la causa directa de que las poses "se salieran de la vista"; (3) **trío de swing silenciado completo** — antes solo se descartaba la X (`-80`); Y (`sin(p²π)·-20`) y Z (`sin(√p·π)·-20`) seguían sumándose bajo la pose. Ahora `blockSwingMode()` decide: `BlockSwingNative` (sin preset), `BlockSwingJava` (preset con `keepSwing`, arco reconstruido Java 1.8) y `BlockSwingNone` (preset con `swing = 0`: sin arco); (4) **ancla `equip`** — Rise a veces pasa un `equippedProgress` modificado a `transformFirstPersonItem`, y ese valor es el término `-0.6` del translate base. La cadena nativa ya lo aplicó con el progreso intacto, así que `BlockPreset::anchorY = (equip - 1) · -0.6` restaura el residuo (Smooth/Astro +0.60, XIV +0.66, Noov +0.75, Lucid/Beta/Old/Exhibition Old/New/Dortware +0.30, …); (5) **op `Block`** — `doBlockTransformations()` de Java (`T(-0.5, 0.2, **0.02**)`, `R(30,Y)`, `R(-80,X)`, `R(60,Y)`) en la posición exacta en que Rise lo llama, porque Bedrock 0.15 no tiene pose de bloqueo propia; (6) **Astro/XIV ya no duplican la base** — portaban `translate(0.56,-0.52,-0.72) + rotate(45,Y)`, que el juego ya aplica.
- **Ops dinámicas**: Rise usa varias curvas del progreso de swing. Aquí se recuperan **dos** en vivo de la cadena nativa: `f2 = sin(√p·π)` (desde el callsite X, `angle / -80`) y `f4 = sin(p²π)` (desde el Y, `angle / -20`, exacto y no reconstruido). De `f2` se reconstruye `p = (asin(f2)/π)²` para `f3 = 0.8p-0.8p²` y `f12 = sin(p²π-3)`. Los componentes de eje dinámicos de Rise (p. ej. `(-f2, 0, 9)`) llevan máscara `dynamicAxis` (bits 0/1/2). Ambos factores se publican **antes** de cualquier supresión, para que la pose siga teniendo curvas aunque el trío nativo esté silenciado. La envolvente lleva sello de tiempo; con más de 100 ms se usa 0 para que la pose no quede congelada a mitad de animación.
- **Límites conocidos**: los sliders X/Y/Z solo aplican con `Custom` (los presets los reemplazan) y la pose de brazo en tercera persona sigue leyendo `g_blockRotations` (el path de `ModelPart` sobrescribe una tripleta, no una matriz). `Stab` **sí** conserva el giro por reloj de pared (`CurveTime` → `-(GetTickCount64()/2 % 360)` sobre X); `Flup` usa 70° para `ConstantManager.J`. `1.7` llama además a un helper privado (`JX()`) sin equivalente aquí, así que se aproxima con la guardia. `Rhys` y `Swing` estaban **intercambiados** en la tabla (tenían el cuerpo del otro) y ya se corrigieron. Las `Swing Animation` de Rise (`None`/`Punch`/`Shove`/`Smooth`/`1.9+`) **no** se exponen: solo reordenan llamadas nativas de Java.
- Traza de verificación: `[Animations] block preset '<nombre>' applied (first person)` (one-shot por preset), más los one-shot existentes del hook de scale (`translate %p`).
- Build Release ejecutado dos veces; DLL regenerada sin errores.

# Nota adicional para Qoder
Sí Qoder está en la sesión no debe de usar la sintaxis de CLangd, si no la de MSVC
