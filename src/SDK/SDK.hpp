#pragma once

// Public SDK entry point. Prefer including a focused header when a source file
// needs only one subsystem; include SDK.hpp for broad integration points.

#include "SDK/Client/Camera.hpp"
#include "SDK/Client/Client.hpp"
#include "SDK/Client/MoveInputHandler.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Engine/Memory/CodePatch.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"
#include "SDK/Engine/VTable/VTables.hpp"
#include "SDK/Entity/Actor.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Gameplay/Effects.hpp"
#include "SDK/Gameplay/GameMode.hpp"
#include "SDK/Inventory/Inventory.hpp"
#include "SDK/Inventory/ItemInstance/ItemInstance.hpp"
#include "SDK/Network/Packets/Packets.hpp"
#include "SDK/Network/Packets/MobEquipmentPacket.hpp"
#include "SDK/UI/Chat.hpp"
#include "SDK/UI/Hooks/ChatHook.hpp"
#include "SDK/World/Block/Block.hpp"
#include "SDK/World/BlockSource/BlockSource.hpp"
#include "SDK/World/Level.hpp"
