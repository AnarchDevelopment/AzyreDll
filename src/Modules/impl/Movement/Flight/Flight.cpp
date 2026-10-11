#include "Flight.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <cstdio>

namespace mc {

namespace {

bool keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool isCursorShowing()
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci))
        return (ci.flags & CURSOR_SHOWING) != 0;
    return false;
}

float axisOf(int negVk, int posVk)
{
    float v = 0.0f;
    if (GetAsyncKeyState(posVk) & 0x8000)
        v += 1.0f;
    if (GetAsyncKeyState(negVk) & 0x8000)
        v -= 1.0f;
    return v;
}

} // namespace

Flight::Flight()
    : Module("Flight", "Allows you to fly", Category::Movement, 'F')
{
    markHasSettings();
}

void Flight::onEnable()
{
    airTicks_ = 0;
    flying_ = false;
    currentFriction_ = 1.0f;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    if (mode_ == kJump)
    {
        currentY_ = local.pos().y;
        lastJump_ = GetTickCount64();
        displayDebug("Jump fly enabled");
    }
}

void Flight::onDisable()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();

    if (mode_ == kJump)
    {
        if (resetOnDisable_ && flying_)
        {
            Vec3 vel = local.velocity();
            vel.x = 0.0f;
            vel.z = 0.0f;
            local.setVelocity(vel);
        }
        flying_ = false;
        displayDebug("Jump fly disabled");
    }
}

void Flight::displayDebug(const std::string& msg)
{
    if (!debug_)
        return;
    chathook::display("§7[§bFly§7]§f " + msg);
}

void Flight::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
        return;

    // Solstice reads the game's input component; Azyre polls raw keys, so
    // gate them while the menu or a game screen is open.
    float forward = 0.0f;
    float right = 0.0f;
    bool jumping = false;
    bool sneaking = false;

    if (!menu::visible() && !isCursorShowing())
    {
        forward = axisOf('S', 'W');
        right = axisOf('A', 'D');
        jumping = keyDown(VK_SPACE);
        sneaking = keyDown(VK_SHIFT);
    }

    if (mode_ == kMotion)
    {
        // Motion: yaw motion at speed/10, Space/Shift vertical, full stop
        // hover when idle (exact Solstice math).
        Vec3 vel{0.0f, 0.0f, 0.0f};

        if (forward != 0.0f || right != 0.0f || jumping || sneaking)
        {
            if (forward != 0.0f || right != 0.0f)
            {
                Vec3 dir = wishDirFromYaw(local.yaw(), forward, right);
                vel.x = dir.x * (speed_ / 10.0f);
                vel.z = dir.z * (speed_ / 10.0f);
            }
            if (jumping)
                vel.y += speed_ / 10.0f;
            else if (sneaking)
                vel.y -= speed_ / 10.0f;
            airTicks_ = 0;
        }
        else
        {
            // Idle hover with anti-kick: a single micro-descent tick every
            // kickInterval ticks resets the server's flying check.
            ++airTicks_;
            if (antiKick_ && airTicks_ >= (int)kickInterval_)
            {
                vel.y = -0.06f;
                airTicks_ = 0;
            }
        }

        local.setVelocity(vel);
        return;
    }

    if (mode_ == kPregame)
    {
        // Pregame: fixed 0.255 speed, -0.0005 anti-gravity hover when idle.
        Vec3 vel{0.0f, -0.0005f, 0.0f};

        if (forward != 0.0f || right != 0.0f || jumping || sneaking)
        {
            const float speed = 0.255f;
            if (forward != 0.0f || right != 0.0f)
            {
                Vec3 dir = wishDirFromYaw(local.yaw(), forward, right);
                vel.x = dir.x * speed;
                vel.z = dir.z * speed;
            }
            if (jumping)
                vel.y += speed;
            else if (sneaking)
                vel.y -= speed;
        }

        local.setVelocity(vel);
        return;
    }

    if (mode_ == kJump)
    {
        // Damage signal: Solstice listens for the incoming
        // SetActorMotionPacket (knockback); Azyre detects the local hurt
        // flag transition instead.
        bool hurt = local.isHurt();
        if (hurt && !wasHurt_)
        {
            lastDamage_ = GetTickCount64() + (ULONGLONG)(flyTime_ * 1000.0f);
            currentY_ = local.pos().y;
            displayDebug("Damage taken");
        }
        wasHurt_ = hurt;

        tickJump(local, forward, right);
    }
}

bool Flight::tickJump(LocalPlayer& local, float forward, float right)
{
    if (damageOnly_ && GetTickCount64() > lastDamage_)
    {
        // Fly window expired: stop flying, keep the fall.
        if (flying_)
        {
            currentY_ = local.pos().y;
            if (resetOnDisable_)
            {
                Vec3 vel = local.velocity();
                vel.x = 0.0f;
                vel.z = 0.0f;
                local.setVelocity(vel);
            }
            flying_ = false;
        }
        currentFriction_ = 1.0f;
        return false;
    }

    flying_ = true;

    if (local.onGround())
    {
        displayDebug("Resetting friction [OnGround]");
        currentFriction_ = 1.0f;
    }
    else if (speedFriction_ && (forward != 0.0f || right != 0.0f))
    {
        currentFriction_ *= friction_;
        Vec3 dir = wishDirFromYaw(local.yaw(), forward, right);
        Vec3 vel = local.velocity();
        vel.x = dir.x * ((speed_ * currentFriction_) / 10.0f);
        vel.z = dir.z * ((speed_ * currentFriction_) / 10.0f);
        local.setVelocityXZ(vel);
    }

    if (local.pos().y < currentY_ && !local.onGround())
    {
        ULONGLONG now = GetTickCount64();
        if (lastJump_ == 0 || now - lastJump_ > (ULONGLONG)(jumpDelay_ * 1000.0f))
        {
            jump(local, forward, right);
            lastJump_ = now;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "Height: %.2f", local.pos().y - currentY_);
        displayDebug(buf);
        currentY_ -= heightLoss_;
    }

    if (local.onGround() && resetOnGround_)
    {
        currentY_ = local.pos().y;
        displayDebug("Resetting height [OnGround]");
    }

    return true;
}

void Flight::jump(LocalPlayer& local, float forward, float right)
{
    // Solstice: setOnGround(true) + jumpFromGround() + restore + horizontal
    // motion. jumpFromGround has no offset here, so the jump velocity is
    // applied directly (same trick BunnyHop uses).
    bool onGround = local.onGround();
    local.setOnGround(true);

    Vec3 vel = local.velocity();
    vel.y = off::gameconst::JumpVelocity;
    if (forward != 0.0f || right != 0.0f)
    {
        Vec3 dir = wishDirFromYaw(local.yaw(), forward, right);
        vel.x = dir.x * (speed_ / 10.0f);
        vel.z = dir.z * (speed_ / 10.0f);
    }
    local.setVelocity(vel);

    local.setOnGround(onGround);

    displayDebug("Jumping");
    currentFriction_ = 1.0f;
    displayDebug("Resetting friction [Jump]");
}

void Flight::drawSettings()
{
    const char* modes[] = {"Motion", "Pregame", "Jump"};
    widgets::CCombo("Mode##fly", &mode_, modes, 3);

    if (mode_ == kMotion || mode_ == kJump)
        widgets::CSlider("Speed##fly", &speed_, 0.0f, 20.0f, "%.1f");

    if (mode_ == kMotion)
    {
        ImGui::Separator();
        widgets::Checkbox("Anti-kick##fly", &antiKick_);
        if (antiKick_)
            widgets::CSlider("Kick interval (ticks)##fly", &kickInterval_, 10.0f, 200.0f, "%.0f");
    }

    if (mode_ == kJump)
    {
        ImGui::Separator();
        widgets::Checkbox("Speed friction##fly", &speedFriction_);
        if (speedFriction_)
            widgets::CSlider("Friction##fly", &friction_, 0.0f, 1.0f, "%.3f");
        widgets::CSlider("Height loss##fly", &heightLoss_, 0.0f, 2.0f, "%.2f");
        widgets::CSlider("Jump delay (s)##fly", &jumpDelay_, 0.0f, 1.0f, "%.2f");
        widgets::Checkbox("Damage only##fly", &damageOnly_);
        if (damageOnly_)
            widgets::CSlider("Fly time (s)##fly", &flyTime_, 0.0f, 10.0f, "%.2f");
        widgets::Checkbox("Reset on ground##fly", &resetOnGround_);
        widgets::Checkbox("Reset on disable##fly", &resetOnDisable_);
        widgets::Checkbox("Debug##fly", &debug_);
    }
}

nlohmann::json Flight::saveSettings() const
{
    return nlohmann::json{
        {"mode", mode_},
        {"speed", speed_},
        {"antiKick", antiKick_},
        {"kickInterval", kickInterval_},
        {"speedFriction", speedFriction_},
        {"friction", friction_},
        {"heightLoss", heightLoss_},
        {"jumpDelay", jumpDelay_},
        {"damageOnly", damageOnly_},
        {"flyTime", flyTime_},
        {"resetOnGround", resetOnGround_},
        {"resetOnDisable", resetOnDisable_},
        {"debug", debug_},
    };
}

void Flight::loadSettings(const nlohmann::json& j)
{
    // Legacy single-mode config (velocity fly): keep the old speeds as the
    // Motion speed so nothing feels lost after the update.
    if (j.contains("hSpeed"))
        speed_ = j.value("hSpeed", 0.5f) * 10.0f;

    mode_ = j.value("mode", mode_);
    if (mode_ < kMotion || mode_ > kJump)
        mode_ = kMotion;
    speed_ = j.value("speed", speed_);
    if (speed_ < 0.0f)
        speed_ = 0.0f;
    if (speed_ > 20.0f)
        speed_ = 20.0f;
    antiKick_ = j.value("antiKick", antiKick_);
    kickInterval_ = j.value("kickInterval", kickInterval_);
    speedFriction_ = j.value("speedFriction", speedFriction_);
    friction_ = j.value("friction", friction_);
    if (friction_ < 0.0f)
        friction_ = 0.0f;
    if (friction_ > 1.0f)
        friction_ = 1.0f;
    heightLoss_ = j.value("heightLoss", heightLoss_);
    if (heightLoss_ < 0.0f)
        heightLoss_ = 0.0f;
    if (heightLoss_ > 2.0f)
        heightLoss_ = 2.0f;
    jumpDelay_ = j.value("jumpDelay", jumpDelay_);
    if (jumpDelay_ < 0.0f)
        jumpDelay_ = 0.0f;
    if (jumpDelay_ > 1.0f)
        jumpDelay_ = 1.0f;
    damageOnly_ = j.value("damageOnly", damageOnly_);
    flyTime_ = j.value("flyTime", flyTime_);
    if (flyTime_ < 0.0f)
        flyTime_ = 0.0f;
    if (flyTime_ > 10.0f)
        flyTime_ = 10.0f;
    resetOnGround_ = j.value("resetOnGround", resetOnGround_);
    resetOnDisable_ = j.value("resetOnDisable", resetOnDisable_);
    debug_ = j.value("debug", debug_);
}

MC_REGISTER_MODULE(Flight);

}
