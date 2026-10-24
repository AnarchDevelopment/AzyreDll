


#pragma once

#include <cstdint>

namespace mc::vt {

constexpr uintptr_t LocalPlayer = 0xE63B88;
constexpr uintptr_t RemotePlayer = 0xE645A0;
constexpr uintptr_t RemotePlayerAlt = 0xE64588;
constexpr uintptr_t ServerPlayer = 0xE6CDA8;

constexpr uintptr_t MinecraftClient = 0xE44B28; /* Like ClientInstance */
constexpr uintptr_t MoveInputHandler = 0xE618E8;
constexpr uintptr_t PacketSender = 0xE699D0;
constexpr uintptr_t BatchedPacketSender = 0xE69A00;
constexpr uintptr_t ServerNetworkHandler = 0xE6BDA0;
constexpr uintptr_t MovePlayerPacket = 0xE644F8;

constexpr uintptr_t BlockSource = 0xEAEDD8;
constexpr uintptr_t Block = 0xE7B470;
constexpr uintptr_t Level = 0xEAF9F8;

// Chunk sources (para validar la cadena de lectura de bloques).
constexpr uintptr_t ChunkSource = 0xEAEFF8;
constexpr uintptr_t MainChunkSource = 0xEAF1B0;
constexpr uintptr_t PlayerChunkSource = 0xEAF350;
constexpr uintptr_t FlatLevelSource = 0xEAFEF8;
constexpr uintptr_t MultiPlayerLevel = 0xE62878;
constexpr uintptr_t ServerLevel = 0xE6CCE0;

constexpr uintptr_t CreativeMode = 0xE8A0C8;
constexpr uintptr_t SurvivalMode = 0xE8A240;
constexpr uintptr_t GameMode = 0xE8A1A0;

constexpr uintptr_t Item = 0xE6A350;
constexpr uintptr_t BlockItem = 0xE8AE28;
constexpr uintptr_t AuxDataBlockItem = 0xE8AC80;
constexpr uintptr_t BlockPlanterItem = 0xE8AFD0;
constexpr uintptr_t DoorItem = 0xE8C200;
constexpr uintptr_t SaplingBlockItem = 0xE8DC48;
constexpr uintptr_t LeafBlockItem = 0xE8EE80;
constexpr uintptr_t ClothBlockItem = 0xE8ECD8;
constexpr uintptr_t StoneSlabBlockItem = 0xE8EB30;
constexpr uintptr_t WoodSlabBlockItem = 0xEAE1B0;
constexpr uintptr_t TopSnowBlockItem = 0xE8DDF0;
constexpr uintptr_t WaterLilyBlockItem = 0xE8E490;
constexpr uintptr_t SignItem = 0xE8E638;
constexpr uintptr_t DiggerItem = 0xE8D3F0;
constexpr uintptr_t PotionItem = 0xE8FD70;
constexpr uintptr_t BucketItem = 0xE8B670;
constexpr uintptr_t BowItem = 0xE8B4C8;
constexpr uintptr_t ArrowItem = 0xE8AAD8;
constexpr uintptr_t BoatItem = 0xE8B178;
constexpr uintptr_t MinecartItem = 0xE8F6D0;
constexpr uintptr_t HorseArmorItem = 0xE8D8E8;
constexpr uintptr_t ArmorItem = 0xE8A930;
constexpr uintptr_t MapItem = 0xE8F520;
constexpr uintptr_t EmptyMapItem = 0xE8C550;
constexpr uintptr_t ComplexItem = 0xE8BD10;

constexpr uintptr_t SetEntityMotionPacket = 0xE6A1A0;
constexpr uintptr_t SetEntityDataPacket = 0xE6A200;
constexpr uintptr_t SetEntityLinkPacket = 0xE631D8;
constexpr uintptr_t TextPacket = 0xE4BDC0;
constexpr uintptr_t UseItemPacket = 0xE6A020;
constexpr uintptr_t TakeItemEntityPacket = 0xE6A110;
constexpr uintptr_t AddItemPacket = 0xE69E70;
constexpr uintptr_t AddItemEntityPacket = 0xE69E40;
constexpr uintptr_t DropItemPacket = 0xE62BC0;
constexpr uintptr_t ItemFrameDropItemPacket = 0xE69BA0;
constexpr uintptr_t ReplaceSelectedItemPacket = 0xE6A320;
constexpr uintptr_t ClientboundMapItemDataPacket = 0xE6A5D8;
constexpr uintptr_t AnimatePacket = 0xE64498;
// InteractPacket (VInteractPacket): usado por el cliente en el clic izquierdo
// sobre una entidad; confirmado en sub_14004B9D0/sub_140397A20.
constexpr uintptr_t InteractPacket = 0xE44C40;
constexpr uintptr_t LevelEventPacket = 0xE69BD0;
constexpr uintptr_t UpdateBlockPacket = 0xE6A2C0;
constexpr uintptr_t PlayerActionPacket = 0xE63B58;
constexpr uintptr_t PlayerInputPacket = 0xE63B28;
constexpr uintptr_t MobEquipmentPacket = 0xE64528;

constexpr uintptr_t BaseContainerScreen = 0xE4B408;
constexpr uintptr_t ChestScreen = 0xE4BE40;
constexpr uintptr_t InventoryScreen = 0xE5ACC0;
constexpr uintptr_t FurnaceScreen = 0xE592D8;
constexpr uintptr_t BrewingStandScreen = 0xE4B878;
constexpr uintptr_t EnchantingScreen = 0xE58EE0;
constexpr uintptr_t AnvilScreen = 0xE4B0F0;
constexpr uintptr_t GuiComponent = 0xE48BE0;
constexpr uintptr_t GuiElement = 0xE455E0;
constexpr uintptr_t ChatScreen = 0xE4BB48;
constexpr uintptr_t OptionsItem = 0xE46190;
constexpr uintptr_t OptionsCustomLabelItem = 0xE5C830;

constexpr uintptr_t EntityDamageSource = 0xE79798;
constexpr uintptr_t EntityDamageByEntitySource = 0xE79718;
constexpr uintptr_t EntityDamageByChildEntitySource = 0xE796D8;
constexpr uintptr_t EntityDamageByBlockSource = 0xE79698;

constexpr uintptr_t ItemRenderer = 0xE65F90;
constexpr uintptr_t ItemInHandRenderer = 0xE67020;
constexpr uintptr_t ItemFrameRenderer = 0xE65F28;
constexpr uintptr_t ItemSpriteRenderer = 0xE65FD0;
constexpr uintptr_t ThrownPotionRenderer = 0xE66B68;
constexpr uintptr_t InventoryItemRenderer = 0xE47920;
constexpr uintptr_t FlyingItemRenderer = 0xE47058;
constexpr uintptr_t BreakingItemParticle = 0xE63500;

constexpr uintptr_t ItemFrameBlockEntity = 0xE99898;
constexpr uintptr_t ItemFrameBlock = 0xE9DCE8;
constexpr uintptr_t ItemEntity = 0xE7B068;

constexpr uintptr_t SpawnData = 0xE90480;
constexpr uintptr_t FishReward = 0xE79C00;
constexpr uintptr_t MapItemSavedData = 0xEB2208;
constexpr uintptr_t RepairItemRecipe = 0xE8C0C8;

namespace slot {

namespace LocalPlayer
{
    constexpr int ResetVelocity = 11;
    constexpr int Move = 14;
    constexpr int GetFriction = 20;
    // sub_1404D4560 (slot 211): NO arma el MovePlayerPacket como se creia.
    // Es el ORQUESTADOR de render — lee +0x0C (X de mPosition) como yaw y
    // +0x14 (Z de mPosition) como pitch, arma dos Vec3 con el offset de
    // altura del actor, llama a sub_1405EAE30(vtable, renderPos, posAlt,
    // posFeet, 0, 0, 200) y devuelve si el resultado esta en {2,3}.
    // Sub_1405EAE30 (DECOMPILADO, 100% confirmado): NO es una funcion de
    // rotacion — es un VOXEL CAST / ClipHelper estilo Minecraft Java. Sus
    // parametros 2 y 3 son los puntos Vec3 del rayo, convierte ambos a
    // BlockPos con floor(), avanza al mas lejano en cada paso de rejilla y
    // por cada celda llama a sub_1405E7620(blockSource, &hitByte, &pos) para
    // leer el bloque, vtable+56 para el collision shape, vtable+320 para
    // canCollide y vtable+456 para el HitResult. Ese +200 que sub_1404D4560
    // le pasa es el MAXIMO DE Celdas del cast, no un yaw. El retorno es el
    // HitResult (0=miss, 1..2..3=hit), por eso el caller hace (ret-2)<=1.
    // DESCARTADO para bodyYaw/headYaw: no hay rotacion que escribir aqui.
    constexpr int RenderOrchestrator = 211;
    // sub_1404D4770 (slot 214): setter de NOMBRE (lee slot 1688, copia el
    // string, llama a los slots 1112/1680 y 800 = setText). No toca rot.
    constexpr int ActuallyHurt = 67;
    constexpr int Hurt = 69;
}

namespace BlockItem
{
    constexpr int UseOn = 31;
}

namespace GameMode
{
    // Vtable de GameMode (0xE8A1A0): 19 slots (0..18). El "slot 59" que
    // habia aqui leia FUERA de la tabla (puntero basura) — eliminado.
    constexpr int UseItemOnWrapper = 12; // sub_1405113F0 (candidato principal)
    // Mismo slot mientras se confirma en IDA. Candidatos alternativos a
    // GameMode::useItemOn (verificar decompilando y buscando la construccion
    // del UseItemPacket — vtable 0xE6A020 escrita en buffer+0x10 — seguida
    // del send): slot 9 sub_140511BC0, slot 10 sub_140511820,
    // slot 11 sub_140511700, slot 15 sub_1405118A0. El caller de los
    // ctores de UseItemPacket (0x5100F0/0x510270) ES el orquestador.
    constexpr int UseItemOn = 12;
    // Confirmado por REA en GameMode/CreativeMode/SurvivalMode: slot 14
    // despacha sub_1405103D0(this, Player*, Actor*), la entrada de ataque.
    constexpr int Attack = 14;
}

namespace UseItemPacket
{
    constexpr int Destructor = 0;
    constexpr int Handle = 1;
    constexpr int Read = 2;
    constexpr int Write = 3;
    constexpr int GetName = 4;
}

namespace SetEntityMotionPacket
{
    constexpr int Handle = 4;
}

namespace PacketSender
{
    constexpr int Send = 2;
}

}

inline constexpr uintptr_t blockItemVtables[] = {
    BlockItem, AuxDataBlockItem, BlockPlanterItem, DoorItem, SaplingBlockItem,
    LeafBlockItem, ClothBlockItem, StoneSlabBlockItem, WoodSlabBlockItem,
    TopSnowBlockItem, WaterLilyBlockItem, SignItem,
};

inline constexpr uintptr_t levelVtables[] = {
    Level, MultiPlayerLevel, ServerLevel,
};

}
