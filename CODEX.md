# CODEX — Continuidad de KillAura

## Estado

El usuario detuvo una primera sesión para conservar presupuesto; en la continuación se completó la implementación inicial. KillAura y el dispatch nativo están agregados, y `cmake --build build --config Release --target AzyreSDK` terminó correctamente dos veces. **Queda pendiente la validación dentro de una partida** (objetivo elegido, ataque que acepte el servidor y ambas rutas de rotación). No se tocó el resto de cambios preexistentes del workspace deliberadamente.

## REA / IDA

- Skill aplicada: `reverse-engineer-anything`, versión esperada `42`.
- Servidor REA: `rea-agents 6.7.0`.
- Target abierto y enlazado: `C:\Users\ELIAS\AppData\Roaming\.minecraft_bedrock\versions\c2fc6b6e-7548-4fda-8936-c105c740b1e7\Minecraft.Win10.DX11.exe`.
- Identidad observada: PE x64, SHA-256 `d5683d2362c62e76dc9e49f8ad08e1462b6f4a8120fca981eb22955368ed38ab`, base `0x140000000`.
- Provider seleccionado: IDA MCP adapter (`provider_id: ida`), upstream `ida-pro-mcp 1.1.3`; el análisis es el IDB abierto en IDA.
- REA informó que `procedure_pseudo_code`, `procedure_assembly`, `procedure_callees`, `xrefs` y `list_documents` responden. `analyze_function`, `search_procedures`, `list_procedures`, `procedure_callers` y búsquedas de strings aparecieron no disponibles por `ProviderAdapterError`/limitaciones del adapter. No repetir las operaciones fallidas sin un cambio del provider.
- El target sigue abierto en la sesión REA para retomar. Al terminar la investigación futura, exportar la evidencia necesaria y cerrar el target según la skill.

## Hallazgo principal: entrada de ataque

La hipótesis vieja de `AGENTS.md` decía que faltaba hallar `attack` en la vtable de `LocalPlayer`; ya se corrigió porque la evidencia localiza la API virtual en `GameMode`.

- `GameMode` vtable: RVA `0xE8A1A0`; slot 14 está en VA `0x140E8A210` y apunta a `sub_1405103D0`.
- `CreativeMode` vtable: RVA `0xE8A0C8`; slot 14 en `0x140E8A138` apunta al mismo `sub_1405103D0`.
- `SurvivalMode` vtable: RVA `0xE8A240`; slot 14 en `0x140E8A2B0` apunta al mismo `sub_1405103D0`.
- La tabla de punteros se comprobó también leyendo el PE local; REA `xrefs` a `0x1405103D0` devolvió las tres celdas de vtable anteriores y `0x141021DB8`.
- `sub_1405103D0` es un wrapper con firma observada compatible con `(GameMode* this, Player* player, Actor* target)`. Comprueba dos condiciones del modo en `GameMode+0x10` y delega en `sub_140500320` cuando corresponde.
- `sub_140500320(Player*, Actor*)` construye un `VEntityDamageByEntitySource` (`type=2`, atacante = Player) y despacha el método virtual del objetivo en `target vtable + 0x218` (slot 67), con el daño calculado y validaciones adicionales.
- El source de Solstice corrobora el uso de alto nivel: `src/Features/Modules/Combat/Aura.cpp` llama `player->getGameMode()->attack(actor)` (en torno a la línea 541) y trata la rotación por separado en eventos de salida de paquetes.
- `sub_14049F6B0` parecía inicialmente otro candidato porque construye `EntityDamageByEntitySource`, pero su xref de vtable va a `0x140E75308`; no es la entrada de ataque que debe usar KillAura.

### Evidence IDs REA conservados

- `ev_11056efe5d0ddd216a511d8e236648ba43fef4bbc116d0bc1c9f0fd2cc366d96` — identidad/listado del documento abierto.
- `ev_c87f3978768cd30ce027cb936863d5dd747f26b015c45e3a54de9cd8384e2dcb` — pseudocódigo de `sub_140500320`.
- `ev_29923f4db364a4e1601e1452f72909b770e8d576bafc48c1687f726c0303a25c` — ensamblador de `sub_1405103D0`.
- `ev_79461ce0183772547e9d1a848548467923cb4580758793a142715309eca54eaa` — xrefs de `sub_1405103D0`, incluidas las tres celdas de vtable.
- `ev_40b4dc6650bd762eaf5b3c78c74c636a0c151b436d1a8407d0b11872d5a3d059` — pseudocódigo del candidato alternativo `sub_14049F6B0`.

Limitaciones: el adapter de IDA declara los facetes de `analyze_function`/relaciones completas no disponibles. El pseudocódigo es texto del decompilador, no fuente original; el hash identifica el archivo examinado, pero no garantiza que el IDB no tenga parches o anotaciones.

## Referencias revisadas

- Solstice está en `D:\Nueva Era_EE\Solstice-Source-main-1.21.111\Solstice-Source-main`. Su `Aura.cpp` selecciona objetivos en BaseTick, llama al GameMode para atacar y modifica `PlayerAuthInput`/`MovePlayer` al salir, incluyendo `mRot` y `mYHeadRot`.
- Rise está en `D:\Nueva Era_EE\TokenGraber\Rise-6.9.5`. `KillAura.java` separa selección de objetivos, `updateRotations()` y ataque; `RotationComponent.java` mantiene la rotación enviada por eventos de movimiento y permite que visualización/rotación real dependan del modo silent.
- La lectura de referencias fue parcial; no se completó un port 1:1 ni se verificaron todas las opciones/modelos de rotación.

## Implementación realizada

- Añadido `vt::slot::GameMode::Attack = 14`, confirmado por REA para las vtables de GameMode, CreativeMode y SurvivalMode.
- Añadido `GameMode::attack(const Actor&, const Actor&)`: valida modo/jugador/objetivo, lee la vtable de la instancia activa y ejecuta el slot con guardia SEH.
- Añadido `Modules/impl/Combat/KillAura/KillAura.hpp/.cpp`: busca jugadores remotos válidos con vida, excluye amigos, filtra por rango/FOV, apunta al centro de la caja y limita intentos con CPS. La selección se actualiza en frame/tick.
- La rotación se comparte entre el hook del paquete (`rot::request`, prioridad Combat) y el modelo local (`rotation_helper::requestClientRotation`, también prioridad Combat). `RotationHelper` ahora arbitra prioridades y el clear de Scaffold es solo de prioridad Movement, preservando Combat. Al perder el objetivo o desactivar KillAura se limpia solo Combat.
- Bug de seguimiento corregido: el avance/instalación del render helper estaba dentro de `Scaffold::onFrame` y el update de body yaw en `Scaffold::onTick`, por eso el modelo de KillAura no giraba hasta activar Scaffold. Ambos avances ahora corren desde el Present global (`DX11.cpp` y `RotationHelper::updateClientRotation`).
- Nombre, descripción y settings UI están en inglés. No se agregó modo de rotación visible ni edición directa de cámara.
- Build Release ejecutado dos veces como dicta `AGENTS.md`; ambos terminaron con DLL generada. La primera regeneró CMake por el archivo nuevo; mostró únicamente warnings ya existentes de conversión en `AutoClicker.cpp`. La segunda compiló `KillAura.cpp` y enlazó correctamente.
- Evidencia REA exportada en `rea/kill-aura-evidence-20261020.json` (82 registros); la sesión de análisis se cerró después.

## Próximos pasos

1. Validar dentro de partida que `remotePlayersSnapshot()` entrega jugadores con el layout actual y que `health()` reporta vida correctamente.
2. Confirmar que el slot 14 acepta el ataque en el modo/servidor usado; si devuelve false, observar la llamada desde IDA/REA sin aumentar la frecuencia de intentos.
3. Observar que el paquete usa la rotación de combate y que la cabeza/modelo local coincide; comprobar también limpieza al perder target y al apagar.
4. Ajustar range/FOV/CPS y el orden de selección según el comportamiento observado en juego.

## Seguimiento: KillAura sin Scaffold

El usuario informó que KillAura no aplicaba rotación visual hasta activar Scaffold. La causa estaba localizada: Scaffold era el único llamador de `rotation_helper::updateClientRotation()` y `tickClientRotation()`. Se movió el update a `DX11.cpp` después de los callbacks `onFrame`; `updateClientRotation()` ahora actualiza también el body yaw, y Scaffold ya no es dueño de ese ciclo global. Build Release pasó dos veces después del cambio. Falta confirmar in-game con Scaffold apagado.

## Seguimiento: swing de KillAura (2026-10-20)

- El usuario reportó que KillAura solo ponía rojo al objetivo, sin aplicar el ataque al servidor ni mostrar swing.
- Solstice separa ambas operaciones: llama `player->swing()` y luego, en el modo Earliest, `player->getGameMode()->attack(actor)`. Se conserva ese orden para el build actual.
- REA sobre el ejecutable actual (`Minecraft.Win10.DX11.exe`, SHA-256 `d5683d2362c62e76dc9e49f8ad08e1462b6f4a8120fca981eb22955368ed38ab`) confirmó `sub_140268420(this)`: actualiza estado de swing y construye/envía `VAnimatePacket` con acción 1, runtime ID del actor. `Actor::swing()` ahora despacha esa RVA bajo SEH; KillAura la invoca antes de `GameMode::attack`.
- `sub_140503700` envía `VAnimatePacket` con acción 4 en una ruta de golpe; no es el swing normal. `sub_1405103D0` → `sub_140500320` es la ruta local de daño. El pseudocódigo visible no demuestra por sí solo que el servidor reciba/aplique el ataque; confirmar in-game. Si persiste, identificar el paquete/handler de ataque del protocolo de este build antes de agregar un paquete manual; no asumir el layout de `InteractPacket` ni copiar el `InventoryTransactionPacket` de Solstice 1.21.
- REA exportado en `rea/kill-aura-swing-evidence-20261020.json` y snapshot en `rea/kill-aura-swing-snapshot-20261020.json`. Evidencia normal de swing: `ev_ba09630495f66b7c31a0e7eb2579b913f4542bcde9aaac6e60c0ad7f68c1e389`; acción crítica: `ev_366a9795e363f5f379414abe732efcbc5e340f464e1044abb3096f7ff8a23ce4`; wrapper de ataque local: `ev_c87f3978768cd30ce027cb936863d5dd747f26b015c45e3a54de9cd8384e2dcb`. El adapter de IDA no ofrece xrefs/callers completos; se evitó una búsqueda amplia que quedó esperando.
- Release compilado dos veces después del cambio; ambas pasadas terminaron con `AzyreSDK.dll`. Solo aparecieron warnings C4244 existentes en `AutoClicker.cpp` durante la recompilación completa.

## Seguimiento: daño server-side de KillAura (2026-10-20)

- La vtable de `InteractPacket` está en RVA `0xE44C40`. El constructor natural `sub_140397A20` inicializa el paquete de 0x20 bytes con header `type=2`, `field=1` y target ID en `+0x18`; el serializador `sub_140043B40` escribe la acción de `+0x10` y el ID de `+0x18`.
- El handler natural de clic izquierdo `sub_14004B9D0` arma `VInteractPacket` con acción `2` y runtime ID del actor, y lo envía por PacketSender antes del efecto local. `sub_140045570` usa la misma clase con acción `1` para la interacción de clic derecho; `sub_1404B4D90` usa acción `3` para salir de vehículo. Esto distingue el ataque de la interacción.
- `GameMode::attack()` ahora envía el paquete equivalente mediante `chathook::sendRawPacket`, después llama al slot 14 para daño predicho local. El packet struct replica el header/tamaño/campos observados; `off::player::RuntimeID=0x198` y `vt::InteractPacket=0xE44C40`.
- REA Evidence: `ev_55c8ec8667e12aa759200dee26cf06b854d56d9e85946bc98f638bcbb238521d` (`sub_14004B9D0`, acción 2); `ev_afcc4c2bc9819474e7cadb4d4d1adc953ed9900d702384ee9ca59df20c031ca4` (ctor y tamaño); `ev_7eb04efa097916ce100f8600e240958edc18cc08e7f85bc08b4a3e39fd9a3d8e` (serializer); `ev_5badb744eaa37ad742b7795f928dec2acf58e74889cb9d6d8c49869d960cad57` (xref del vtable). Export actualizado: `rea/kill-aura-server-attack-evidence-20261020.json` y snapshot `rea/kill-aura-server-attack-snapshot-20261020.json`.
- Aún falta una prueba en un servidor real: verificar que `sendRawPacket` use los handles capturados y que el servidor reduzca la salud. El código pasó dos builds Release; DLL en `build/Release/AzyreSDK.dll`.

## Archivos inspeccionados

- `src/SDK/VTables.hpp`, `src/SDK/Offsets.hpp`, `src/SDK/Classes/GameMode.hpp/.cpp`, `src/SDK/Classes/Actor.hpp`, `src/SDK/Classes/LocalPlayer.hpp`, `src/SDK/Game.hpp/.cpp`, `src/Framework/Math.hpp`, `src/Features/Rotation.hpp`, `src/Features/RotationHelper.hpp`, `src/Features/Friends.hpp`, `src/Modules/ModuleManager.cpp`, `src/Render/DX11.cpp`.
- Módulos existentes revisados como referencias: `AimAssist`, `Backtrack`, `AutoClicker`, `RapidHit`.

## Seguimiento: el servidor aún no aplicaba el hit (2026-10-20)

- El usuario confirmó que el objetivo remoto seguía sin recibir daño. El problema de la implementación anterior era la ruta: `chathook::sendRawPacket()` llamaba a `origSendToServer`, pero REA muestra que el clic izquierdo natural manda `VInteractPacket` por `MinecraftClient + 0xE8 -> connector + 0x98 -> PacketSender::send` (vslot 2).
- `GameMode::attack()` ahora obtiene ese sender vivo desde `Game::clientAddr()`, valida su vtable (`PacketSender`/`BatchedPacketSender`) y llama el slot 2 con el mismo paquete observado en `sub_14004B9D0`: type 2, field 1, action 2, target RuntimeID en +0x18. El daño local predicho aún se ejecuta, pero ya no convierte el resultado en éxito si el sender no fue invocado.
- El análisis REA de `sub_14004B9D0` se hizo sobre el IDA MCP conectado al ejecutable de esta build, digest SHA-256 `d5683d2362c62e76dc9e49f8ad08e1462b6f4a8120fca981eb22955368ed38ab`. La ruta sender corresponde a `0x14004BD91–0x14004BDD1`. Evidence y snapshot exportados a `rea/kill-aura-sender-route-20261020.json` y `rea/kill-aura-sender-route-snapshot-20261020.json`; la sesión REA se cerró.
- Añadidos `off::chain::ClientNetworkConnector=0xE8` y `NetworkConnectorPacketSender=0x98`. `cmake --build build --config Release --target AzyreSDK` pasó dos veces tras el cambio; DLL resultante `build/Release/AzyreSDK.dll` (5,087,744 bytes, 2026-10-20 17:44:38).
- **Pendiente de prueba in-game**: observar que el PacketSender transmite el paquete y que un servidor real acepta el ataque. Si no aplica daño, activar una traza de PacketSender filtrada por vtable `InteractPacket`/acción 2 y revisar el runtime ID y la respuesta del server; la llamada sin excepción no prueba aceptación remota.

## Seguimiento: configuraciones de Solstice para KillAura (2026-10-20)

- Añadidos los ajustes compatibles con el protocolo y la infraestructura de esta build: Mode (Single/Multi/Switch), Rotate Mode (None/Normal/Flick), Head Yaw (+90° al campo enviado), Dynamic Range, Randomize APS con Min/Max, Swing, Swing Delay/Value, Attack Through Walls (raycast de bloques), Auto Disable on Dimension Change y Debug. Se conservan Range/FOV y las claves `cps` de configs previas.
- `Mode` ahora elige objetivo más cercano, rota el índice de objetivos o prioriza el que lleva más tiempo sin recibir intento. El rango dinámico reduce el rango cuando la velocidad horizontal es menor de 0.05. El intervalo aleatorio APS se sortea al hacer cada ataque. La rotación Flick manda una solicitud corta al paquete de movimiento en el instante del ataque; Normal la refresca mientras hay objetivo; None no cambia las rotaciones.
- No se agregaron controles sin backend: `Synched`/InventoryTransaction, spoof de equipo, auto-bow/proyectiles privados, cambio a tercera persona y bypasses de la versión moderna de Solstice no corresponden a este cliente pre-1.16.
- Release compiló dos veces con `TargetName=AzyreSDK_KillAura` porque `build/Release/AzyreSDK.dll` estaba bloqueada (`LNK1104`, juego probablemente cargó la DLL). El artefacto actualizado queda en `build/Release/AzyreSDK_KillAura.dll`; no se cerró el juego para reemplazar la DLL en uso. La primera compilación al nombre alternativo reconstruyó los proyectos Luau por el override de `TargetName`; la segunda pasó incremental.

## Seguimiento: KillAura Strafe (2026-10-20)

- Añadido el checkbox `Strafe` (default true, persistido como `strafe`). Sigue la implementación observada en Solstice: alinea la rotación local con el target mientras se mantiene independiente la rotación silenciosa del paquete. Aquí usa `rotation_helper` render-scoped, de modo que la cámara no se modifica; al apagar Strafe se limpia solo la rotación client-side de KillAura.

## Continuidad: Animations — investigación REA (2026-10-20)

### Estado de la tarea

Se retomó el pedido de completar las configuraciones de `Animations` que aparecen en Solstice y requieren evidencia del binario actual. Esta sesión **encontró y confirmó la ruta central del swing en primera persona**, pero no editó código ni compiló. No presentar como encontradas las ubicaciones exactas de `TapSwingAnim` o `FluxSwing`: ambas siguen pendientes.

### Target y proveedor

- Target IDA: `C:\Users\ELIAS\AppData\Roaming\.minecraft_bedrock\versions\c2fc6b6e-7548-4fda-8936-c105c740b1e7\Minecraft.Win10.DX11.exe`.
- PE x64, image base `0x140000000`, SHA-256 `d5683d2362c62e76dc9e49f8ad08e1462b6f4a8120fca981eb22955368ed38ab`.
- Proveedor observado: REA IDA MCP adapter v1; upstream `ida-pro-mcp 1.1.3`. Las solicitudes dirigidas respondieron rápido.
- El target quedó abierto en la sesión REA al acabar; no se exportó bundle ni se cerró IDA/REA.
- El adapter declara `read_bytes` e `inspect_native_instruction` como no soportados en este provider. Los límites del dossier también dicen que cuerpos completos/xrefs tipados pueden faltar. No repetir sin cambio de provider.

### Candidato confirmado

- `sub_1402FB690` (`0x1402FB690`) es la rutina de render del item en mano: lee item equipado/contexto del actor local, obtiene/interpola el progreso de swing, transforma la matriz y llama a `sub_1402FA2F0` para dibujar.
- `sub_14017CE30` (`0x14017CE30`) llama a `sub_1402FB690` en `0x14017D334`; esa es la conexión observada con la etapa de render.
- `sub_1402FB690` llama a `sub_1402FC5A0` en `0x1402FB950` con actor, partial tick y matriz. La asamblea de `sub_1402FC5A0` muestra interpolación de campos de rotación, curvas/clamps de swing y dos llamadas a `sub_140088410`.
- Helpers confirmados: `sub_140088410` aplica una rotación eje-ángulo a la matriz; `sub_140067630` multiplica/aplica traslación; `sub_140067800` aplica escala. Por eso `sub_1402FC5A0` es el candidato concreto para la transformación del swing, y `sub_1402FB690` para la ruta de render del item.
- Esto aún no prueba qué global altera el ángulo `-80°` de Solstice, ni dónde cae el parche Flux Swing en este build. No adaptar la firma activa de Solstice a ciegas: su `TapSwingAnim` actual usa `SigType::Sig` con un prólogo de función aunque el consumidor lo convierte a `float*`; la firma comentada `RefSig` es distinta y también requiere validación contra este ejecutable.

### Evidencia REA de esta sesión

- `ev_25cc29a36a2be0abc6a7d38038764a4da3b3a129d63c6f59a6e4da2de3677f9f` — dossier de `sub_1402FB690`.
- `ev_2b2f740a5c05183ecba12c38f689ba67e405a6c2f9411bc43b769e52077ed85f` — dossier de `sub_1402FC5A0`.
- `ev_a3f01cdb249423eb16806aee60bbbc38475ffd2b1ce5e445c3ac5082c1a06931` — instrucciones de `sub_1402FB690`; progreso de swing y call a `sub_1402FC5A0`/`sub_1402FA2F0`.
- `ev_b89ebc8470f20ea07dbf72ac7ebf9cc386ba6d1d6666d87b1e96b1526d4bd30a` y `ev_56d1ac960e2b05c0e9dbb8b9721c63c8dff0c4816bb665b701a07a9cf01f3792` — dossier/instrucciones de `sub_14017CE30`, incluido el callsite `0x14017D334`.
- `ev_b0b36a30d4067fd26f2391fbc5a3e75cbbf0166ed36da90feee45a9b9b9fcc32` — `sub_140088410`.
- `ev_21a41eb32ad5b75223d86c7885a83723a2170c347f0e8e757eb375b6fab4b75c` — `sub_140067630`.
- `ev_ba764a5f69587cc826b3479588457486567ea2568168bc8201ad3e9ef82d323c` — `sub_140067800`.
- `ev_aac6b880aedc00e68d0d51622b6923ff228eb8bb939435428134ae199e5a4cc3` — xrefs a `sub_1402FB690`.
- Búsqueda literal `swing` en el inventario de strings no encontró coincidencias; `animation` solo devolvió UI/controller strings no relacionados. Un resultado vacío de strings no demuestra ausencia de código.

### Estado del módulo y siguiente trabajo

- `src/Modules/impl/Visuals/Animations/Animations.hpp|.cpp` contiene actualmente Swing Time, Third Person Block, Only on Block y rotaciones XYZ.
- En Solstice, opciones adicionales candidatas son Flux Swing, Custom Swing Angle/Only on Block y Small Items. No portar No Switch Animation ni Test como toggles aún: No Switch está comentado y Test es código de prueba/no-op.
- Próximos pasos: (1) rastrear la matriz/callback equivalente a `BobHurt` para Small Items; (2) localizar y validar el global RIP-relative de `TapSwingAnim`; (3) ubicar el call `E8` de Flux Swing y confirmar qué comportamiento omite. Solo entonces implementar las opciones respaldadas por una ruta estable, con restauración al apagar, y compilar Release dos veces.

### Seguimiento de constantes y firmas

- Xrefs de `0x140B530E4` confirman que `-80.0f` se consume en `sub_1402FB690` (`0x1402FBC02`), dentro de la rama que comprueba `item use action == 4`. Debe tratarse como una rotación de pose/uso de item hasta encontrar evidencia adicional; no usarla como `Custom Swing Angle` general.
- `-0.2f` en `0x140B52F10` es leído desde `sub_1402FB690` en `0x1402FBB26`; `-0.6f` en `0x140B52F58` se lee en `0x1402FC1E6`. Son transformaciones reales de la ruta del renderer, pero todavía no demuestran el vector `Small Items` `(0.5, -0.2, -0.6)` de Solstice. `+0.5f` en `0x140B5261C` tiene cientos de referencias en todo el binario y no sirve de firma aislada.
- Las firmas byte de Solstice para `BobHurt` y el callsite `FluxSwing` no coinciden con bytes del ejecutable de este target. No se debe asumir que el offset/patch de la build 1.21.51 aplica al build actual.
- Frontera concreta para un futuro hook de matriz: `sub_1402FB690` prepara `rdx=actor`, `xmm2=partialTick` y `r9=rsi` (la matriz de render local) antes de llamar a `sub_1402FC5A0`; además ya pasó el valor derivado del progreso de swing. `sub_1402FC5A0` aplica rotaciones a esa matriz. Esto la hace el mejor candidato actual para probar un ajuste de matriz `Small Items`/swing, aunque falta confirmar runtime y el orden relativo al BobHurt original.
- Evidencia adicional: `ev_f22b2b81e434567bb4639858563598b1d87809bd85feec19be4687058e8a7645` (xref de `-80.0f`, incluye el uso en `sub_1402FB690`); `ev_48a6318e96e99cb89f0b99cf31e2e3fd00d5683ea5524a0ab4dc515565671df5` (xref de `-0.2f`); `ev_340ee1c0b01cd9f7d67dbd8f15a074784a070229cf1a40c40b024bfad1c103f8` (xref de `-0.6f`); `ev_d734fa7abaa06e8eac80be500a56705e2c6ed35dee334f4275af9a3442b01b86` (xref muy compartido de `+0.5f`); `ev_07d16fa7d57b4f4b37d4326c9d5d6880367a8ad636a30d0012b66ed99a39d9b6` (`sub_14017C940`, llama al render stage `sub_14017CE30`, no parece ser el callback de matriz `BobHurt`).


### Nuevo límite de investigación: item draw

- REA dossier `ev_eee91ebb016e56cbb36ab43cd6672a938c6f826d7ccba36242b5e2030557e223` identifica `sub_1402FA2F0` como preparación/dibujo común del item. El caller de swing `sub_1402FB690` llega a esta rutina en `0x1402FC0B3`, una vez compuesta la pose. Sus xrefs incluyen varios render paths (`0x1402C270E`, `0x1402CDC5A`, `0x1402CE18C`, `0x1402CE1AC`, `0x1402D2AB6`, `0x1402D2AD8`, `0x1402FC0B3`). Es el candidato actual más cercano a un hook para `Small Items`, sujeto a confirmar qué matriz pasa cada caller y el orden de multiplicación.
- `sub_1402FC9F0` (`ev_968e05fb3a929ea706d3344844738d15cd2e24ec3413832ea5be144e8628b83a`) y `sub_1402FC930` (`ev_68b4dbd770901a140f8992593b0a4561facc4087288e148367689e0bc273f3d2`) son rutas auxiliares de pose/mano. Ninguna evidencia actual las identifica como el callback de `BobHurt`.
- Solstice confirma que `BobHurtEvent` recibe `void* this` y `glm::mat4*`, corre el original primero y luego modifica la matriz. Las firmas de bytes del repo de referencia no coinciden con este ejecutable; usar la semántica como guía, no sus bytes.
- `-80°` en `sub_1402FB690` pertenece a la transformación condicional de `item use action == 4`, no al ángulo general de swing. La parte común de la progresión es la zona de `0x1402FB9A0–0x1402FBB55`; aún falta aislar qué giro es el ángulo ajustable y localizar el target de `Flux Swing`.

### Aislamiento de callers y constantes de swing

- La rutina común `sub_1402FA2F0` recibe el call de primera persona desde `sub_1402FB690` en `0x1402FC0B3`; sus demás callers incluyen `sub_1402CDA40` y `sub_1402D27E0`, que son otras rutas de render. Para modificar Small Items sin alterar modelos de mundo, el candidato debe filtrar el caller `0x1402FC0B3` (o usar una frontera anterior solo de primera persona).
- La curva de swing común en `sub_1402FB690` usa entre otros `0x140B52C48`, `0x140B52B24`, `0x140B52BC0` y `0x140B530A4`; los xrefs de todos son compartidos por varias funciones del binario. Quedan descartados como globals para escribir a ciegas. Evidencia de xrefs: `ev_d12200cde2daaa5ff3c480791a680c442e509904a895833ddc2b05119e28939f`, `ev_086d30796fddc20a491f75d01296aed14686bf431bcd36b2b1f787ee47291b3c`, `ev_ea8c07540b356ef8f82f495d0c548aaa28fbd9c4d976d9a707d683900d657fc0`, `ev_fed6eafe81992997b72488348fc155dc67f3e8a73a846d5feb0de35912f51bfe`.
- Xrefs de `sub_1402FA2F0`: `ev_28e2aa5601cf347bba89537cf7b36b57c315b5a08cb9d400b7d3a5ff2e7e025d`. Los dossiers de los dos callers de render de entidad: `ev_bc31170d6f480fae79be28340c83002024f61e4ab3f2c2fb234a78a4245d8af7` (`sub_1402CDA40`) y `ev_09c8d8dfdb15b181a6fac3ab6007b4d3ede3015325d88a217b5a5300774d3a63` (`sub_1402D27E0`).

# COSAS A SOLUCIONAR (Trabajo en equipo con Codex y Claude)

### FreeLook — implementación inicial (2026-10-20)

- Estado corregido: es un prototipo, **no un FreeLook auténtico**. Actor yaw/pitch siguen conduciendo la cámara; el helper/paquete solo congela modelo y wire yaw. No fuerza tercera persona y el movimiento sigue la cámara.
- Referencia de comportamiento (Rise 6.9.5 / Solstice): perspectiva de tercera persona temporal, yaw/pitch de cámara independientes actualizados con deltas del mouse, orientación/movimiento real del jugador congelados, restauración de perspectiva/rotación al soltar.
- Para portarlo a Bedrock hacen falta los hooks/estados nativos de input-look y perspectiva. `MouseDeviceFeed=0`; `RendererCamera` solo se ha leído para proyección. No escribir ese campo ni simular F5 por aproximación. REA/IDA se saturó con búsqueda de procedimientos; continuar con consultas de dirección concreta cuando la sesión esté estable.
- Las dos compilaciones Release pasaron, pero solo validan compilación, no el comportamiento in-game.

### Implementación exploratoria de candidatos

- Agregados `Small Items`, `Custom Swing Angle`, `Only on Block` y `Swing Angle` a `Animations`, con descripciones/configuración en inglés y serialización JSON.
- `Small Items`: hook en `sub_1402FA2F0` filtrado por return address `mem::base()+0x2FC0B8` y actor local. Hace un MatrixStack push (`sub_140793F40`), aplica `(0.5,-0.2,-0.6)` con `sub_140067630`, deja que el original dibuje y restaura el cursor. REA demuestra el callsite de primera persona posterior a la composición de swing; el filtro evita afectar los callers de render de entidades. Para evitar que `Custom Swing Angle`/el frame temporal desfasen hotbar, crosshair y chat, el cleanup restaura incondicionalmente el cursor al valor pre-push aunque el renderer original deje un nivel anidado abierto; no se cambió ningún offset de `GuiComponent`.
- `Custom Swing Angle`: en la misma matriz antes del draw, suma una rotación X correspondiente a `(ángulo configurado − (-80°)) * sin(sqrt(progreso)*π)`. Esta parte es una prueba de la hipótesis Java/estándar, no un ángulo común confirmado por REA. En target, el literal `-80°` observado está condicionado a `item use action == 4`; probar visualmente y retocar antes de afirmar equivalencia con Solstice. `Only on Block` usa RMB + item sostenido.
- `Flux Swing` no se agregó aún: la firma/callsite de Solstice no valida en esta build, y no hay evidencia para elegir qué llamada parchear.
- `Offsets.hpp` ahora tiene RVAs para renderer y helpers de matriz, además del global MatrixStack `0xFDD850`.
- Compilación Release pasó dos veces después del fix de macro. Resultado: `build/Release/AzyreSDK.dll`. Falta prueba visual con el juego y la DLL cargada.
