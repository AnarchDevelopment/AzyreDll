#include "TargetStrafe.hpp"

#include "GUI/Menu.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Combat/KillAura/KillAura.hpp"
#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace mc {
namespace {

bool keyDown(int key)
{
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool isCursorShowing()
{
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    return GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) != 0;
}

float axisOf(int negativeKey, int positiveKey)
{
    float value = 0.0f;
    if (keyDown(positiveKey))
        value += 1.0f;
    if (keyDown(negativeKey))
        value -= 1.0f;
    return value;
}

KillAura* activeKillAura()
{
    Module* module = ModuleManager::get().find("KillAura");
    auto* aura = dynamic_cast<KillAura*>(module);
    return aura && aura->enabled() ? aura : nullptr;
}

} // namespace

TargetStrafe::TargetStrafe()
    : Module("TargetStrafe", "Orbit the active KillAura target while preserving vertical motion",
             Category::Movement, 0)
{
    markHasSettings();
}

void TargetStrafe::onEnable()
{
    clockwise_ = true;
    lastTarget_ = 0;
}

void TargetStrafe::onFrame(float /*dt*/)
{
    if (!Game::get().localFound() || menu::visible() || ImGui::GetIO().WantCaptureMouse ||
        isCursorShowing())
        return;

    KillAura* aura = activeKillAura();
    if (!aura)
        return;

    // Scaffold owns movement direction and placement geometry while enabled.
    Module* scaffold = ModuleManager::get().find("Scaffold");
    if (scaffold && scaffold->enabled())
        return;

    const uintptr_t targetAddress = aura->targetAddress();
    Actor target(targetAddress);
    LocalPlayer local = Game::get().localPlayer();
    if (!targetAddress || !target.valid() || target.health() <= 0 || !local.valid())
        return;

    if (targetAddress != lastTarget_)
    {
        clockwise_ = true;
        lastTarget_ = targetAddress;
    }

    if (holdJump_ && !keyDown(VK_SPACE))
        return;

    const bool left = keyDown('A');
    const bool right = keyDown('D');
    if (left != right)
        clockwise_ = right;

    float forward = axisOf('S', 'W');
    if (forward == 0.0f && (left || right))
        forward = 1.0f;
    if (forward == 0.0f)
        return;

    const Vec3 playerPos = local.posExact();
    const Vec3 targetPos = target.posExact();
    const float dx = playerPos.x - targetPos.x;
    const float dz = playerPos.z - targetPos.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    if (!std::isfinite(distance) || distance < 0.2f)
        return;

    const float radialX = dx / distance;
    const float radialZ = dz / distance;
    const float orbitSign = clockwise_ ? 1.0f : -1.0f;
    const float inputSign = forward >= 0.0f ? 1.0f : -1.0f;
    const float tangentX = -radialZ * orbitSign * inputSign;
    const float tangentZ = radialX * orbitSign * inputSign;

    Vec3 velocity = local.velocity();
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.z))
        return;

    // Reuse the player's current horizontal speed (including speed modules or
    // knockback). Only seed a vanilla walking speed when input was just pressed
    // and the movement update has not produced velocity yet.
    float speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    if (speed < 0.001f)
        speed = off::gameconst::WalkSpeedBlocksPerSec / 20.0f;

    // Bias the travel direction toward the configured radius, then normalize
    // it back to the player's measured speed. This keeps radius correction
    // from adding or removing horizontal speed.
    const float radiusError = distance - range_;
    float radialCorrection = 0.0f;
    if (std::fabs(radiusError) > 0.12f)
        radialCorrection = clampf(radiusError * 0.65f, -0.22f, 0.22f);

    float desiredX = tangentX - radialX * radialCorrection;
    float desiredZ = tangentZ - radialZ * radialCorrection;
    const float desiredLength = std::sqrt(desiredX * desiredX + desiredZ * desiredZ);
    if (!std::isfinite(desiredLength) || desiredLength < 0.001f)
        return;

    velocity.x = (desiredX / desiredLength) * speed;
    velocity.z = (desiredZ / desiredLength) * speed;
    local.setVelocityXZ(velocity);
}

void TargetStrafe::onRender()
{
    if (!circle_ || !enabled() || !Game::get().localFound() || menu::visible() ||
        ImGui::GetIO().WantCaptureMouse || isCursorShowing())
        return;

    if ((holdJump_ && !keyDown(VK_SPACE)) ||
        !(keyDown('W') || keyDown('A') || keyDown('S') || keyDown('D')))
        return;

    Module* scaffold = ModuleManager::get().find("Scaffold");
    if (scaffold && scaffold->enabled())
        return;

    KillAura* aura = activeKillAura();
    if (!aura || !aura->targetAddress())
        return;

    Actor target(aura->targetAddress());
    LocalPlayer local = Game::get().localPlayer();
    Client client = Game::get().client();
    if (!target.valid() || !local.valid() || !client.valid())
        return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x <= 0.0f || display.y <= 0.0f)
        return;

    const CameraView camera = getCameraView(local, client);
    const float fov = readGameFov();
    Vec3 center = target.posExact();
    center.y = target.pos().y + 0.04f;

    constexpr int kSegments = 64;
    std::array<ImVec2, kSegments> points{};
    for (int i = 0; i < kSegments; ++i)
    {
        const float angle = (2.0f * kPi * static_cast<float>(i)) /
                            static_cast<float>(kSegments);
        const Vec3 world{center.x + std::cos(angle) * range_, center.y,
                         center.z + std::sin(angle) * range_};
        Vec3 screen{};
        if (!worldToScreen(world, camera.pos, camera.yaw, camera.pitch, fov,
                           display.x, display.y, screen))
            return;
        points[static_cast<size_t>(i)] = ImVec2(screen.x, screen.y);
    }

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw)
        return;
    draw->AddPolyline(points.data(), kSegments, IM_COL32(82, 151, 255, 45),
                      ImDrawFlags_Closed, thickness_ * 3.0f);
    draw->AddPolyline(points.data(), kSegments, IM_COL32(110, 177, 255, 205),
                      ImDrawFlags_Closed, thickness_);
}

void TargetStrafe::drawSettings()
{
    widgets::CSlider("Range##ts", &range_, 0.8f, 3.8f, "%.1f");
    widgets::Checkbox("Hold Jump##ts", &holdJump_);
    widgets::Checkbox("Circle##ts", &circle_);
    if (circle_)
        widgets::CSlider("Thickness##ts", &thickness_, 0.5f, 4.0f, "%.1f");
}

nlohmann::json TargetStrafe::saveSettings() const
{
    return nlohmann::json{
        {"range", range_},
        {"holdJump", holdJump_},
        {"circle", circle_},
        {"thickness", thickness_},
    };
}

void TargetStrafe::loadSettings(const nlohmann::json& j)
{
    range_ = clampf(j.value("range", range_), 0.8f, 3.8f);
    holdJump_ = j.value("holdJump", holdJump_);
    circle_ = j.value("circle", circle_);
    thickness_ = clampf(j.value("thickness", thickness_), 0.5f, 4.0f);
}

std::string TargetStrafe::suffix() const
{
    char text[16]{};
    std::snprintf(text, sizeof(text), "%.1f", range_);
    return text;
}

MC_REGISTER_MODULE(TargetStrafe);

} // namespace mc
