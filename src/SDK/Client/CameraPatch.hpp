#pragma once

#include <cstddef>
#include <cstdint>

namespace mc::off::camera {

// Camera FOV load instruction used as the verified pattern anchor for Zoom.
inline constexpr uint8_t ZoomFovPattern[] = {0xF3, 0x0F, 0x10, 0x87, 0x48, 0x17, 0x00, 0x00};
inline constexpr size_t ZoomOverwrittenBytes = 8;
inline constexpr size_t ZoomFactorSlotOffset = 24;
inline constexpr size_t ZoomCaveSize = 32;
inline constexpr size_t ZoomMultiplyInstructionOffset = 8;
inline constexpr size_t ZoomJumpInstructionOffset = 16;
inline constexpr size_t ZoomMultiplyRipAfterInstruction = 16;
inline constexpr size_t ZoomJumpRipAfterInstruction = 21;

}
