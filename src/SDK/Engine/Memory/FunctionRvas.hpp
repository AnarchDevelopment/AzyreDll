#pragma once

#include <cstddef>
#include <cstdint>

namespace mc::off {

// Native function RVAs verified for the active game build.
namespace fn
{
    constexpr uintptr_t MinecraftClientTick = 0x04B360;

    constexpr uintptr_t ActorHurt = 0x4D5730;
    constexpr uintptr_t ActorActuallyHurt = 0x267DB0;
    // Actor animation entry that emits the normal AnimatePacket (action 1).
    // Confirmed in the current Minecraft.Win10.DX11.exe build via REA.
    constexpr uintptr_t ActorSwing = 0x268420;
    // Called by ActorSwing with the Actor* and used as the current swing
    // duration (the caller compares swing progress against return_value / 2).
    // This is based on the current executable's ActorSwing decompile.
    constexpr uintptr_t ActorGetSwingDuration = 0x4DA4A0;


    constexpr uintptr_t BlockItemUseOn = 0x5201F0;

    constexpr uintptr_t GameModeUseItemOnWrapper = 0x5113F0;
    // Cadena de rotura de bloques de GameMode (vtable 0xE8A1A0): slot 1 arranca
    // la rotura y envia el PlayerActionPacket, slot 3 acumula el progreso
    // (dt * destroySpeed * 20) y llama al slot 2 al llegar a 1.0, slot 2 aplica
    // la rotura local + paquete. Confirmado por disasm REA en este build.
    constexpr uintptr_t GameModeStartDestroy = 0x510450;
    constexpr uintptr_t GameModeDestroyBlock = 0x510680;
    constexpr uintptr_t GameModeContinueDestroy = 0x510BE0;
    constexpr uintptr_t InputWrapper1 = 0x5111F0;
    constexpr uintptr_t InputWrapper2 = 0x511270;
    // ItemStack::useOn dispatcher (sub_14055D960): forwards the stack,
    // player, support block, face and relative click to Item::useOn.
    constexpr uintptr_t ItemStackUseOn = 0x55D960;
    // ItemStack copy constructor used by LocalPlayer's native
    // MobEquipmentPacket synchronization path.
    constexpr uintptr_t ItemStackCopy = 0x55D350;

    constexpr uintptr_t BlockSourceGetBlock = 0x5E73F0;
    constexpr uintptr_t BlockSourceMayPlace = 0x5E8120;
    constexpr uintptr_t Vec3ToBlockPos = 0x5E6A70;
    constexpr uintptr_t BlockSourceHelper = 0x5E8B50;
    constexpr uintptr_t BlockSourceDestructor = 0x5E6C20;
    constexpr uintptr_t BlockSourceConstructor = 0x5E6B20;

    // (this, NetworkIdentifier*, Packet*)
    constexpr uintptr_t SendToServer = 0x38B940;

    // Thunk de _purecall. Ocupa TODAS las ranuras virtuales de la vtable
    // abstracta PacketSender (0xE699D0): verificarlo exige que la clase no
    // implemente send, y despachar contra ella es un fast-fail de MSVC que
    // __try/__except NO captura (termina el proceso). Confirmado por REA.
    constexpr uintptr_t PureCall = 0x981B06;

    // ChatCtrl::addMessage(this, const std::string&, const std::string&, int, char)
    constexpr uintptr_t ChatAddMessage = 0x0C5C00;

    // Handler que abre el ChatScreen nativo (la tecla de chat). Confirmado por
    // REA: sub_1402149C0 toma el cliente desde el controller, valida que no
    // haya pantalla bloqueando (sub_14004F470) y crea el ChatScreen con la
    // factoria sub_1401CED20 -> push de pantalla sub_1401CD550. Es ranura de
    // vtable (tabla 0xC96510), asi que el hook cubre el dispatch virtual.
    // Devuelve 0 = "no se abrio nada"; el llamador no usa el valor.
    constexpr uintptr_t OpenChatScreen = 0x2149C0;

    // Hermano de OpenChatScreen para el chat con prefijo "/" (comando).
    constexpr uintptr_t OpenCommandChatScreen = 0x2148F0;

    // Gate que usa el propio juego antes de abrir el chat: "no hay ninguna
    // pantalla bloqueando". Confirmado por decompile de sub_14004F470: lee el
    // tope del stack de pantallas (+0x150) y el segundo stack (+0x3B0/+0x3B8),
    // y pregunta a cada pantalla su vslot 35 == 0. El argumento es el
    // MinecraftClient (mismos offsets que SDK/Client/Client.hpp).
    constexpr uintptr_t ClientUiIdle = 0x04F470;

    // ScreenStack::pushScreen(this, const std::shared_ptr<Screen>&, char).
    // Confirmado por REA en este build: sub_14004A9B0 es el UNICO punto que
    // inserta en el vector del stack (a1+0x328..0x336 via sub_140055E50) y
    // solo tiene dos callers de codigo (sub_1401CD650, que es el helper que
    // usan ~70 pantallas — inventario incluido, y sub_1400525C0, el flush de
    // pantallas pendientes). Bloquearlo con el chat abierto impide que el
    // inventario (E), la pausa (ESC), el crafting o los cofres se apilen
    // encima del panel, sin tocar el input del juego.
    constexpr uintptr_t ScreenPush = 0x04A9B0;

    // Handler de la tecla de inventario (sub_1401C8D60): (MinecraftClient*,
    // unsigned mode). Confirmado por decompile en este build: elige entre el
    // inventario creativo ("crafting.creative_inventory_screen") y el de
    // supervivencia ("crafting.survival_inventory_screen"), llama a la factoria
    // sub_1401D0D60/sub_1401D1020/sub_1401D10D0 y empuja con sub_1401CD550.
    // Es el target de los DOS keybinds de inventario (sub_140216030 y
    // sub_140045570, ambos ranuras de vtable). Devolver 0 sin llamar al
    // original evita construir y apilar la pantalla, y no deja estado a medias
    // porque todas las escrituras de esa funcion ocurren despues de este punto.
    constexpr uintptr_t OpenInventory = 0x1C8D60;

    // Render que recibe el ScreenContext* como segundo arg (firma completa
    // desconocida: el thunk de SDK/Gameplay/Effects.cpp solo roba rdx sin tocar el
    // resto). Es el mismo que usa el prototipo de potion_hud.py.
    constexpr uintptr_t RenderWithScreenContext = 0x090B60;

    // VMinecraftUIRenderContext vtable slot 10 (vtable 0x140E67628) queues
    // drawImage commands. Slot Easing intercepts its position/UV arguments so
    // it animates the native selected-slot atlas image instead of relying on
    // HotBarRenderer's unrelated/empty output bounds.
    constexpr uintptr_t MinecraftUIRenderContextDrawImage = 0x32A000;

    // PlayerRenderer model setup (sub_1402D93F0), verified in the current
    // build to interpolate Actor body/head yaw pairs into model state.
    constexpr uintptr_t PlayerRendererModelSetup = 0x2D93F0;

    // Humanoid model animation setup (sub_140321D00): (renderer, actor,
    // partialTick). Called from the player renderer right after the render
    // state is interpolated and before the model parts are drawn, so it is a
    // safe point to capture the model pointer at renderer+0x248.
    constexpr uintptr_t HumanoidModelAnimationSetup = 0x321D00;

    // First person item rendering (sub_1402FB690). The item chain it builds is
    // the Java 1.8 transformFirstPersonItem:
    //   translate(0.56, -0.52 - equip*0.6, -0.72)   -> call 0x140067630 @ 0x2FBB82
    //   rotate(45, Y)                                -> call 0x140088410 @ 0x2FBBAA
    //   rotate(sin(p*p*pi) * -20, Y)                 -> call 0x140088410 @ 0x2FBBD2
    //   rotate(sin(sqrt(p)*pi) * -20, Z)             -> call 0x140088410 @ 0x2FBBF9
    //   rotate(sin(sqrt(p)*pi) * -80, X)             -> call 0x140088410 @ 0x2FBC21
    //   scale(0.4)                                   -> call 0x140067800 @ 0x2FBC3B
    // Constants verified in this build: 0xB52658=0.56, 0xB52F48=-0.52,
    // 0xB52678=0.6, 0xB52F78=-0.72, 0xB52BF8=45, 0xB530A4=-20, 0xB530E4=-80,
    // 0xB525DC=0.4. The three rotate call sites are the ones that carry the
    // swing, so they are also the ones a replacement arc has to silence.
    constexpr uintptr_t ItemInHandRender = 0x2FB690;
    constexpr uintptr_t ItemInHandSwingLiftReturn = 0x2FBBD7;   // sin(p^2*pi)*-20 on Y
    constexpr uintptr_t ItemInHandSwingTwistReturn = 0x2FBBFE;  // sin(sqrt(p)*pi)*-20 on Z
    constexpr uintptr_t ItemInHandSwingRotateReturn = 0x2FBC26; // sin(sqrt(p)*pi)*-80 on X
    constexpr uintptr_t ItemInHandSwingScaleReturn = 0x2FBC40;

    // Matrix helpers are shared by the item renderer and the model part
    // renderer, so overrides must be scoped by return address.
    //   translate(matrix, x, y, z)
    //   rotate(matrix, angleDegrees, axisX, axisY, axisZ)
    //   scale(matrix, x, y, z)
    constexpr uintptr_t RenderMatrixTranslate = 0x067630;
    constexpr uintptr_t RenderMatrixRotate = 0x088410;
    constexpr uintptr_t RenderMatrixScale = 0x067800;

    // ModelPart transform (sub_140227660): (part, model, scale). Reads the part
    // rotation triple at +0x18/+0x1C/+0x20 (radians), applies translate/rotate
    // and recurses into the children. Verified in this build.
    constexpr uintptr_t ModelPartRender = 0x227660;

    // Humanoid model render (sub_140239630, vtable slot 9 of VHumanoidModel
    // 0xE61E88): (model, actor, partialTick, ...). Documented for reference
    // only: hooking it directly is NOT safe. Its full argument list is not
    // typed, and any detour that calls non-trivial functions before jumping to
    // the original destroys the volatile registers (r8/r9/xmm4+) it receives,
    // which makes the player model disappear in second and third person.
    constexpr uintptr_t HumanoidModelRender = 0x239630;

    // Third person held item layer (sub_1402C2420, referenced only from render
    // layer vtables): builds its own translate/rotate/scale chain on the shared
    // matrix stack and draws with sub_1402FA2F0. Same hooking caveat as above.
    constexpr uintptr_t PlayerHeldItemLayer = 0x2C2420;
    constexpr uintptr_t PlayerHeldItemLayerSize = 0x326;

    // MouseDevice::feed(this, actionButtonId, buttonData, x, y, dx, dy,
    // forceMotionlessPointer): la entrada universal de input del juego.
    // actionButtonId 4 = wheel (0x78/0x7F arriba, 0x88/0x80 abajo).
    // RVA pendiente desde IDA (firma de Solstice no matchea este build).
    constexpr uintptr_t MouseDeviceFeed = 0;

    // Actor::moveTo (sub_1404D4FB0, slot 15 de la vtable, +120): el store
    // del target de red (+0xD08 pos, +0xD14 rot, +0xD1C flags) que escriben
    // los handlers de MoveEntityPacket/MovePlayerPacket. Firma:
    //   void moveTo(this, const Vec3& pos, const Vec2& rot, int flags)
    // Confirmado por decompilacion (IDA) en el build actual.
    constexpr uintptr_t ActorMoveTo = 0x4D4FB0;

    // GameMode::buildBlock(BlockPos const& baseCell, uchar blockSide, bool):
    // LA entrada real de colocacion del cliente (arquitectura Solstice):
    // lee el HitResult del crosshair, aplica local y arma+envia el
    // UseItemPacket. Hallar con xrefs a los ctors inline de UseItemPacket
    // (0x5100F0 / 0x510270): el caller que construye el paquete ES
    // buildBlock. RVA pendiente del IDA.
    constexpr uintptr_t GameModeBuildBlock = 0;
}

}
