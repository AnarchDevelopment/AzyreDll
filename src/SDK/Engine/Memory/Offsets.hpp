#pragma once

// Compatibility umbrella for build-specific memory layouts. Prefer the
// domain header (ActorOffsets, InventoryOffsets, ClientOffsets, WorldOffsets,
// FunctionRvas) when a file only needs one set of layouts. Packet layouts and
// packet RVAs live with their packet classes under SDK/Network/Packets.
#include "SDK/Client/ClientOffsets.hpp"
#include "SDK/Engine/Memory/FunctionRvas.hpp"
#include "SDK/Entity/ActorOffsets.hpp"
#include "SDK/Inventory/InventoryOffsets.hpp"
#include "SDK/World/WorldOffsets.hpp"
