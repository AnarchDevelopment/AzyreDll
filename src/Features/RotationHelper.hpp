#pragma once

namespace mc {

class LocalPlayer;

namespace rotation_helper {

// Client-only body/head yaw and head pitch, substituted temporarily inside
// PlayerRenderer's model setup so the game retains its own Actor fields.
bool requestClientRotation(const LocalPlayer& player, float yaw, float pitch,
                           int priority = 50);
void tickClientRotation(const LocalPlayer& player);
void updateClientRotation();
bool outgoingHeadYaw(float& yaw);
void clearClientRotation();
void clearClientRotation(int priority);

} // namespace rotation_helper
} // namespace mc
