#include "Speed.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <cmath>
#include <cstdlib>

namespace mc {

namespace {

bool keyHeld(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool usingMoveKeys()
{
    return keyHeld('W') || keyHeld('A') || keyHeld('S') || keyHeld('D');
}

bool isStrafing()
{
    return keyHeld('A') || keyHeld('D');
}

// Offset de yaw segun las teclas (puerto de MathUtils::getRotationKeyOffset).
float keyYawOffset(bool allowStrafe)
{
    bool w = keyHeld('W');
    bool a = keyHeld('A');
    bool s = keyHeld('S');
    bool d = keyHeld('D');
    if (!(w || a || s || d))
        return 0.0f;
    if (w && a && allowStrafe)
        return -45.0f;
    if (w && d && allowStrafe)
        return 45.0f;
    if (s && a && allowStrafe)
        return -135.0f;
    if (s && d && allowStrafe)
        return 135.0f;
    if (w)
        return 0.0f;
    if (a && allowStrafe)
        return -90.0f;
    if (s)
        return -180.0f;
    if (d && allowStrafe)
        return 90.0f;
    return 0.0f;
}

// Puerto de MathUtils::getMotion: (x, z) segun yaw + teclas.
struct Motion2
{
    float x, z;
};

Motion2 getMotion(float yaw, float speed, bool allowStrafe)
{
    if (!usingMoveKeys())
        return {0.0f, 0.0f};
    float yR = deg2rad(yaw + keyYawOffset(allowStrafe) + 90.0f);
    return {std::cos(yR) * speed, std::sin(yR) * speed};
}

float randRange(float lo, float hi)
{
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

} // namespace

Speed::Speed()
    : Module("Speed", "Lets you move faster", Category::Movement, 'G')
{
    markHasSettings();
}

void Speed::onEnable()
{
    frictionState_ = 1.0f;
    airTick_ = 0;
    lastOnGround_ = true;
    wasHurt_ = false;
    damageBoostVal_ = 1.0f;
}

void Speed::onDisable()
{
    onEnable();
}

void Speed::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();

    // Damage boost: Solstice lo dispara con el SetActorMotionPacket
    // (knockback); el equivalente nativo es el flag de flasheo del golpe.
    if (damageBoost_)
    {
        bool hurt = local.isHurt();
        if (hurt && !wasHurt_)
            damageBoostVal_ = damageBoostSpeed_;
        wasHurt_ = hurt;
        damageBoostVal_ += (1.0f - damageBoostVal_) * clampf(damageBoostSlowdown_, 0.0f, 1.0f);
    }
    else
    {
        damageBoostVal_ = 1.0f;
    }

    if (mode_ == kFriction)
        tickFriction(local);
    else
        tickLegit(local);

    lastOnGround_ = local.onGround();
}

void Speed::applyFastFall(LocalPlayer& local)
{
    if (fastFall_ == kFFNone)
        return;

    if (fastFall_ == kFFPredict)
    {
        if (airTick_ == fallTicks_)
        {
            Vec3 vel = local.velocity();
            float y = vel.y;
            for (int i = 0; (float)i < fallSpeed_; ++i)
                y = (y - 0.08f) * 0.98f;
            local.setVelocity({vel.x, y, vel.z});
        }
        if (fastFall2_ && airTick_ == fallTicks2_)
        {
            Vec3 vel = local.velocity();
            float y = vel.y;
            for (int i = 0; (float)i < fallSpeed2_; ++i)
                y = (y - 0.08f) * 0.98f;
            local.setVelocity({vel.x, y, vel.z});
        }
    }
    else // kFFSetVel
    {
        if (airTick_ == fallTicks_)
        {
            Vec3 vel = local.velocity();
            vel.y = -(fallSpeed_ / 10.0f);
            local.setVelocity(vel);
        }
        if (fastFall2_ && airTick_ == fallTicks2_)
        {
            Vec3 vel = local.velocity();
            vel.y = -(fallSpeed2_ / 10.0f);
            local.setVelocity(vel);
        }
    }
}

void Speed::handleJump(LocalPlayer& local)
{
    if (jumpType_ == kJumpNone || !usingMoveKeys() || !local.onGround())
        return;

    // Solstice: Vanilla llama jumpFromGround() (estado de salto del juego)
    // y despues pisa la Y; sin ese offset ambos tipos aplican la velocidad
    // directa (el salto vanilla con altura default 0.42 es identico).
    Vec3 vel = local.velocity();
    vel.y = jumpHeight_;
    local.setVelocity(vel);
}

void Speed::tickFriction(LocalPlayer& local)
{
    if (local.onGround())
    {
        frictionState_ = 1.0f;
        airTick_ = 0;
    }
    else
    {
        frictionState_ *= friction_;
        airTick_++;
    }

    applyFastFall(local);

    float sp = randomize_ ? randRange(minSpeed_, maxSpeed_) : speed_;
    if (isStrafing() && useStrafeSpeed_)
        sp = strafeSpeed_;

    // "Ignore Strafe": sin el boost del damage para no levantar sospechas.
    float effective = dontBoostStrafe_
                          ? (sp / 10.0f) * frictionState_
                          : ((sp * damageBoostVal_) / 10.0f) * frictionState_;

    Motion2 motion = getMotion(local.yaw(), effective, strafe_);
    Vec3 vel = local.velocity();
    local.setVelocity({motion.x, vel.y, motion.z});

    handleJump(local);
}

void Speed::tickLegit(LocalPlayer& local)
{
    bool onGround = local.onGround();
    if (onGround)
        airTick_ = 0;
    else
        airTick_++;

    applyFastFall(local);

    Vec3 vel = local.velocity();
    float movementSpeed = vecLength2D(vel);
    bool moving = usingMoveKeys();

    // Boost en el tick de despegue (wasOnGround && !onGround).
    if (!onGround && moving && lastOnGround_)
    {
        float add = randomize_ ? randRange(minSpeed_, maxSpeed_) : speed_;
        movementSpeed += (add / 10.0f / 10.0f) * damageBoostVal_;
    }
    else if (moving && !onGround && !lastOnGround_ && vel.y < 0.0f)
    {
        // Solstice usa getFallDistance() > 0; sin ese offset, la velocidad
        // vertical negativa es el proxy de "cayendo".
        movementSpeed *= friction_;
    }

    float movementYaw = std::atan2(vel.z, vel.x);
    Vec3 newVel = {std::cos(movementYaw) * movementSpeed, vel.y,
                    std::sin(movementYaw) * movementSpeed};
    if (speed_ != 0.0f)
        local.setVelocity(newVel);

    handleJump(local);
}

void Speed::drawSettings()
{
    const char* modes[] = {"Friction", "Legit"};
    widgets::CCombo("Mode##sp", &mode_, modes, 2);

    ImGui::Separator();

    widgets::Checkbox("Damage Boost##sp", &damageBoost_);
    if (damageBoost_)
    {
        widgets::CSlider("Boost Speed##sp", &damageBoostSpeed_, 1.0f, 10.0f, "%.2f");
        widgets::CSlider("Boost Slowdown##sp", &damageBoostSlowdown_, 0.0f, 1.0f, "%.3f");
        widgets::Checkbox("Ignore Strafe##sp", &dontBoostStrafe_);
    }

    widgets::Checkbox("Randomize Speed##sp", &randomize_);
    if (randomize_)
    {
        widgets::CSlider("Min Speed##sp", &minSpeed_, 0.0f, 10.0f, "%.2f");
        widgets::CSlider("Max Speed##sp", &maxSpeed_, 0.0f, 10.0f, "%.2f");
    }
    else
    {
        widgets::CSlider("Speed##sp", &speed_, 0.0f, 10.0f, "%.2f");
    }

    if (mode_ == kFriction)
    {
        widgets::Checkbox("Strafe Only##sp", &strafe_);
        widgets::Checkbox("Custom Strafe Speed##sp", &useStrafeSpeed_);
        if (useStrafeSpeed_)
            widgets::CSlider("Strafe Speed##sp", &strafeSpeed_, 0.0f, 10.0f, "%.2f");
    }

    widgets::CSlider("Friction##sp", &friction_, 0.0f, 1.0f, "%.3f");

    const char* ff[] = {"Ninguno", "Predict", "Set Vel"};
    widgets::CCombo("Fast Fall##sp", &fastFall_, ff, 3);
    if (fastFall_ != kFFNone)
    {
        widgets::CSlider("Fall Ticks##sp", &fallTicks_, 0, 20);
        widgets::CSlider("Fall Speed##sp", &fallSpeed_, 0.0f, 10.0f, "%.2f");
        widgets::Checkbox("Fast Fall 2##sp", &fastFall2_);
        if (fastFall2_)
        {
            widgets::CSlider("Fall Ticks 2##sp", &fallTicks2_, 0, 20);
            widgets::CSlider("Fall Speed 2##sp", &fallSpeed2_, 0.0f, 10.0f, "%.2f");
        }
    }

    const char* jt[] = {"Vanilla", "Velocity", "Ninguno"};
    widgets::CCombo("Jump Type##sp", &jumpType_, jt, 3);
    if (jumpType_ != kJumpNone)
        widgets::CSlider("Jump Height##sp", &jumpHeight_, 0.0f, 1.0f, "%.3f");
}

nlohmann::json Speed::saveSettings() const
{
    return nlohmann::json{
        {"mode", mode_},
        {"speed", speed_},
        {"randomize", randomize_},
        {"minSpeed", minSpeed_},
        {"maxSpeed", maxSpeed_},
        {"strafe", strafe_},
        {"useStrafeSpeed", useStrafeSpeed_},
        {"strafeSpeed", strafeSpeed_},
        {"dontBoostStrafe", dontBoostStrafe_},
        {"friction", friction_},
        {"fastFall", fastFall_},
        {"fallTicks", fallTicks_},
        {"fallSpeed", fallSpeed_},
        {"fastFall2", fastFall2_},
        {"fallTicks2", fallTicks2_},
        {"fallSpeed2", fallSpeed2_},
        {"jumpType", jumpType_},
        {"jumpHeight", jumpHeight_},
        {"damageBoost", damageBoost_},
        {"damageBoostSpeed", damageBoostSpeed_},
        {"damageBoostSlowdown", damageBoostSlowdown_},
    };
}

void Speed::loadSettings(const nlohmann::json& j)
{
    mode_ = j.value("mode", mode_);
    if (mode_ != kFriction && mode_ != kLegit)
        mode_ = kFriction;

    speed_ = clampf(j.value("speed", speed_), 0.0f, 10.0f);
    randomize_ = j.value("randomize", randomize_);
    minSpeed_ = clampf(j.value("minSpeed", minSpeed_), 0.0f, 10.0f);
    maxSpeed_ = clampf(j.value("maxSpeed", maxSpeed_), 0.0f, 10.0f);

    strafe_ = j.value("strafe", strafe_);
    useStrafeSpeed_ = j.value("useStrafeSpeed", useStrafeSpeed_);
    strafeSpeed_ = clampf(j.value("strafeSpeed", strafeSpeed_), 0.0f, 10.0f);
    dontBoostStrafe_ = j.value("dontBoostStrafe", dontBoostStrafe_);
    friction_ = clampf(j.value("friction", friction_), 0.0f, 1.0f);

    fastFall_ = j.value("fastFall", fastFall_);
    if (fastFall_ < kFFNone || fastFall_ > kFFSetVel)
        fastFall_ = kFFNone;
    fallTicks_ = j.value("fallTicks", fallTicks_);
    if (fallTicks_ < 0)
        fallTicks_ = 0;
    if (fallTicks_ > 20)
        fallTicks_ = 20;
    fallSpeed_ = clampf(j.value("fallSpeed", fallSpeed_), 0.0f, 10.0f);
    fastFall2_ = j.value("fastFall2", fastFall2_);
    fallTicks2_ = j.value("fallTicks2", fallTicks2_);
    if (fallTicks2_ < 0)
        fallTicks2_ = 0;
    if (fallTicks2_ > 20)
        fallTicks2_ = 20;
    fallSpeed2_ = clampf(j.value("fallSpeed2", fallSpeed2_), 0.0f, 10.0f);

    jumpType_ = j.value("jumpType", jumpType_);
    if (jumpType_ < kJumpVanilla || jumpType_ > kJumpNone)
        jumpType_ = kJumpVanilla;
    jumpHeight_ = clampf(j.value("jumpHeight", jumpHeight_), 0.0f, 1.0f);

    damageBoost_ = j.value("damageBoost", damageBoost_);
    damageBoostSpeed_ = clampf(j.value("damageBoostSpeed", damageBoostSpeed_), 1.0f, 10.0f);
    damageBoostSlowdown_ = clampf(j.value("damageBoostSlowdown", damageBoostSlowdown_), 0.0f, 1.0f);
}

MC_REGISTER_MODULE(Speed);

}
