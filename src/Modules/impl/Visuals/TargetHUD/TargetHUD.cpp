#include "TargetHUD.hpp"

#include "Features/Friends.hpp"
#include "Features/HitFeed.hpp"
#include "Features/HudRegistry.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Combat/KillAura/KillAura.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/DX11.hpp"
#include "Render/ResourceSprites.hpp"
#include "SDK/Client/Camera.hpp"
#include "SDK/Core/Game.hpp"
#include "resource.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mc {

namespace {

float readHudHealth(const Actor& actor)
{
    // A HUD target must be a world actor. Never let LocalPlayer health leak
    // into the remote target bar if target selection becomes stale.
    if (!actor.isRemotePlayer())
        return -1.0f;

    // generic.health del remoto NO se sincroniza en esta build: la
    // AttributeInstance conserva el valor con el que el inicializador nativo
    // la crea (20). Sirve como ancla (vida maxima/inicial y resync si algun
    // dia cambia), no como lectura del estado real del objetivo.
    const float current = actor.currentHealth();
    if (current >= 0.0f && std::isfinite(current))
        return current;

    const float cached = actor.cachedHealth();
    return cached >= 0.0f && std::isfinite(cached) ? cached : -1.0f;
}

ImU32 riseGradientColor(const ImVec4& top, const ImVec4& bottom, float y, float minY, float maxY)
{
    const float t = std::clamp((y - minY) / std::max(maxY - minY, 0.001f), 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        top.x + (bottom.x - top.x) * t,
        top.y + (bottom.y - top.y) * t,
        top.z + (bottom.z - top.z) * t,
        top.w + (bottom.w - top.w) * t));
}

// Rise's RGQ shader draws a vertically blended rounded rectangle. Build the
// same convex SDF outline as a small colored triangle fan for ImGui's DX11 list.
void addRiseRoundedGradient(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                            float radius, const ImVec4& top, const ImVec4& bottom)
{
    if (!draw || max.x <= min.x || max.y <= min.y)
        return;

    const float r = std::clamp(radius, 0.0f, std::min(max.x - min.x, max.y - min.y) * 0.5f);
    std::array<ImVec2, 28> points{};
    size_t count = 0;
    auto arc = [&](float cx, float cy, float startDegrees) {
        for (int i = 0; i <= 6; ++i)
        {
            const float angle = (startDegrees + 15.0f * i) * 0.017453292519943295f;
            points[count++] = ImVec2(cx + std::cos(angle) * r, cy + std::sin(angle) * r);
        }
    };
    arc(min.x + r, min.y + r, 180.0f);
    arc(max.x - r, min.y + r, 270.0f);
    arc(max.x - r, max.y - r, 0.0f);
    arc(min.x + r, max.y - r, 90.0f);

    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const ImU32 centerColor = riseGradientColor(top, bottom, center.y, min.y, max.y);
    const ImU32 base = (ImDrawIdx)draw->_VtxCurrentIdx;
    draw->PrimReserve((int)(count * 3), (int)count + 1);
    draw->PrimWriteVtx(center, draw->_Data->TexUvWhitePixel, centerColor);
    for (size_t i = 0; i < count; ++i)
        draw->PrimWriteVtx(points[i], draw->_Data->TexUvWhitePixel,
                           riseGradientColor(top, bottom, points[i].y, min.y, max.y));
    for (size_t i = 0; i < count; ++i)
    {
        draw->PrimWriteIdx(base);
        draw->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
        draw->PrimWriteIdx((ImDrawIdx)(base + 1 + ((i + 1) % count)));
    }
}

} // namespace

TargetHUD::TargetHUD()
    : Module("TargetHUD", "Target information card with Azyre and Rise styles", Category::Visuals, 0)
{
    markHasSettings();
}

TargetHUD::~TargetHUD()
{
    releaseSkinTexture();
}

void TargetHUD::onEnable()
{
    // Los golpes previos no pertenecen al objetivo que se va a mostrar.
    lastHitSequence_ = hitfeed::sequence();
    lastDamageApplyMs_ = 0;
}

void TargetHUD::onDisable()
{
    estimatedHealth_ = -1.0f;
    lastObservedHealth_ = -1.0f;
    lastDamageApplyMs_ = 0;
    lastPositionValid_ = false;
}

void TargetHUD::releaseSkinTexture()
{
    if (skinSrv_)
    {
        static_cast<ID3D11ShaderResourceView*>(skinSrv_)->Release();
        skinSrv_ = nullptr;
    }
    skinHash_ = 0;
}

void TargetHUD::onShutdown()
{
    releaseSkinTexture();
    skinOwner_ = 0;
    targetAddr_ = 0;
    displayedTarget_ = 0;
    riseParticlesLive_.clear();
    riseTargetAddr_ = 0;
    riseLastTargetMs_ = 0;
    riseFollowTarget_ = 0;
    riseFollowInitialized_ = false;
    anim_ = 0.0f;
    riseAnimTarget_ = 0.0f;
    lastObservedHealth_ = -1.0f;
    estimatedHealth_ = -1.0f;
    lastPositionValid_ = false;
    lastHitSequence_ = hitfeed::sequence();
    lastDamageApplyMs_ = 0;
}

void TargetHUD::updateSkinTexture(const Actor& actor)
{
    if (skinOwner_ != actor.address())
    {
        releaseSkinTexture();
        skinOwner_ = actor.address();
    }

    std::array<uint8_t, off::skin::PixelBytes> pixels{};
    if (!actor.skinRgba(pixels))
        return;

    uint64_t hash = 1469598103934665603ull;
    for (uint8_t byte : pixels)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    if (skinSrv_ && hash == skinHash_)
        return;

    ID3D11Device* device = static_cast<ID3D11Device*>(dx11::d3dDevice());
    if (!device)
        return;

    // The front face is only 8x8 texels in the skin atlas. Composite the hat
    // layer, then expand each source texel 8x with nearest-neighbor sampling
    // so ImGui's linear sampler does not blur the small face crop.
    std::array<uint8_t, off::skin::PixelBytes> facePixels{};
    std::array<std::array<uint8_t, 4>, 64> faceTexels{};
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const size_t baseOffset = (static_cast<size_t>(y + 8) * 64 + (x + 8)) * 4;
            const size_t overlayOffset = (static_cast<size_t>(y + 8) * 64 + (x + 40)) * 4;
            auto& out = faceTexels[static_cast<size_t>(y) * 8 + x];
            const unsigned alpha = pixels[overlayOffset + 3];
            const unsigned inverseAlpha = 255u - alpha;
            for (size_t channel = 0; channel < 3; ++channel)
            {
                out[channel] = static_cast<uint8_t>(
                    (static_cast<unsigned>(pixels[overlayOffset + channel]) * alpha +
                     static_cast<unsigned>(pixels[baseOffset + channel]) * inverseAlpha + 127u) /
                    255u);
            }
            out[3] = std::max(pixels[baseOffset + 3], pixels[overlayOffset + 3]);
        }
    }
    for (int y = 0; y < 64; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            const auto& texel = faceTexels[static_cast<size_t>(y / 8) * 8 + (x / 8)];
            const size_t offset = (static_cast<size_t>(y) * 64 + x) * 4;
            std::copy(texel.begin(), texel.end(), facePixels.begin() + offset);
        }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64;
    desc.Height = 64;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = facePixels.data();
    init.SysMemPitch = 64 * 4;

    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &init, &texture)) && texture)
    {
        if (SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &view)) && view)
        {
            if (skinSrv_)
                static_cast<ID3D11ShaderResourceView*>(skinSrv_)->Release();
            skinSrv_ = view;
            skinHash_ = hash;
        }
        texture->Release();
    }
}

void TargetHUD::findTarget(const LocalPlayer& local)
{
    targetAddr_ = 0;
    Module* auraModule = ModuleManager::get().find("KillAura");
    if (!auraModule || !auraModule->enabled())
        return;

    auto* aura = static_cast<KillAura*>(auraModule);
    const uintptr_t candidate = aura->targetAddress();
    if (!candidate || candidate == local.address())
        return;

    Actor actor(candidate);
    if (!actor.isRemotePlayer())
        return;
    const float currentHealth = readHudHealth(actor);
    if (currentHealth == 0.0f ||
        (currentHealth < 0.0f && actor.maxHealth() <= 0.0f))
        return;
    targetAddr_ = candidate;
}

bool TargetHUD::updateHealth(const Actor& actor, float maxHealth, float dt)
{
    if (!actor.isRemotePlayer())
        return false;

    const float currentHealth = readHudHealth(actor);
    const float smoothing = std::min(1.0f, dt * 10.0f);

    if (displayedTarget_ != actor.address())
    {
        displayedTarget_ = actor.address();
        estimatedHealth_ = currentHealth >= 0.0f ? currentHealth : maxHealth;
        health_ = std::clamp(estimatedHealth_, 0.0f, maxHealth);
        lastObservedHealth_ = currentHealth;
        // Los golpes que salieron contra el objetivo anterior no descuentan
        // nada en el nuevo.
        lastHitSequence_ = hitfeed::sequence();
        lastDamageApplyMs_ = 0;
        lastPositionValid_ = false;
        return false;
    }

    // Respawn/teleport: la vida estimada pertenece a la vida anterior, se
    // reancla al valor nativo en lugar de arrastrar el estimado a 0.
    const Vec3 rawPosition = actor.pos();
    if (lastPositionValid_ &&
        distance3D(rawPosition, lastPosition_) > kRespawnTeleportDistance)
    {
        estimatedHealth_ = currentHealth >= 0.0f ? currentHealth : maxHealth;
        health_ = std::clamp(estimatedHealth_, 0.0f, maxHealth);
        lastDamageApplyMs_ = 0;
    }
    lastPosition_ = rawPosition;
    lastPositionValid_ = true;

    bool authoritativeChange = false;
    const float previousObservedHealth = lastObservedHealth_;
    if (currentHealth >= 0.0f && std::isfinite(currentHealth))
    {
        authoritativeChange = lastObservedHealth_ >= 0.0f &&
            std::fabs(currentHealth - lastObservedHealth_) >= 0.01f;
        // Si algun dia el server empieza a replicar generic.health, ese valor
        // manda sobre la estimacion local.
        if (lastObservedHealth_ < 0.0f || authoritativeChange)
            estimatedHealth_ = currentHealth;
        lastObservedHealth_ = currentHealth;
    }

    // El unico dano verificable es el que salio por GameMode::attack: cada
    // impacto trae el dano del arma con la que se golpeo. Con "Custom" se
    // ignora la tabla del arma y se usa el valor calibrado por impacto.
    uint64_t processedHitSequence = lastHitSequence_;
    unsigned hitCount = 0;
    float dealtDamage = hitfeed::consumeHits(actor.address(), lastHitSequence_,
                                             processedHitSequence, hitCount);
    lastHitSequence_ = processedHitSequence;
    if (damageMode_ == 1 && hitCount > 0)
        dealtDamage = std::max(0.5f, customDamage_) * static_cast<float>(hitCount);

    // KillAura puede mandar 10+ ataques por segundo y el server aplica dano
    // como mucho una vez por ventana de inmunidad (hurtTime = 10 ticks). Se
    // descuenta un solo golpe por ventana; el resto tampoco resta vida en el
    // server, y contarlos vaciaba la barra ~7x mas rapido que la vida real.
    const uint64_t nowMs = GetTickCount64();
    unsigned appliedHits = 0;
    if (hitCount > 0 && !authoritativeChange)
    {
        const bool immuneWindow = lastDamageApplyMs_ != 0 &&
            nowMs - lastDamageApplyMs_ < kHurtImmunityMs;
        if (!immuneWindow)
        {
            float perHit = damageMode_ == 1
                ? std::max(0.5f, customDamage_)
                : dealtDamage / static_cast<float>(hitCount);
            perHit = std::max(0.5f, perHit);
            const float base = estimatedHealth_ >= 0.0f ? estimatedHealth_ : currentHealth;
            if (base >= 0.0f)
            {
                appliedHits = 1;
                lastDamageApplyMs_ = nowMs;
                estimatedHealth_ = std::max(0.0f, base - perHit);
                dealtDamage = perHit;
            }
        }
    }

    const bool landedHits = appliedHits > 0;

    if (estimatedHealth_ >= 0.0f && std::isfinite(estimatedHealth_))
    {
        const float shownHealth = std::clamp(estimatedHealth_, 0.0f, maxHealth);
        // Keep easing toward the best available health every frame.
        health_ += (shownHealth - health_) * smoothing;
        if (std::fabs(shownHealth - health_) < 0.05f)
            health_ = shownHealth;
    }

    health_ = std::clamp(health_, 0.0f, maxHealth);

    static uint64_t lastHealthTraceMs = 0;
    if (nowMs - lastHealthTraceMs >= 500)
    {
        lastHealthTraceMs = nowMs;
        MC_LOG("[THHealth] target=%p rid=%llu raw=%.2f max=%.2f cache=%.2f hits=%u applied=%u dealt=%.2f estimated=%.2f displayed=%.2f immunity=%llums source=%s",
               reinterpret_cast<void*>(actor.address()),
               static_cast<unsigned long long>(actor.runtimeId()), currentHealth,
               maxHealth, actor.cachedHealth(), hitCount, appliedHits, dealtDamage,
               estimatedHealth_, health_,
               static_cast<unsigned long long>(kHurtImmunityMs),
               authoritativeChange ? "attribute" : (landedHits ?
                   (damageMode_ == 1 ? "custom hit" : "weapon hit") :
                   (hitCount > 0 ? "immune window" : "unchanged")));
    }
    return landedHits ||
           (currentHealth >= 0.0f && previousObservedHealth >= 0.0f &&
            currentHealth < previousObservedHealth - 0.05f);
}

void TargetHUD::onRender()
{
    Game& game = Game::get();
    if (!game.localFound() || menu::hudEditing())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.isLocal())
        return;
    findTarget(local);

    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
    if (mode_ == 1)
    {
        renderRise(local, dt);
        return;
    }

    const bool showing = targetAddr_ != 0;
    const float targetAnim = showing ? 1.0f : 0.0f;
    anim_ += (targetAnim - anim_) * std::min(1.0f, dt * 10.0f);
    if (anim_ < 0.01f)
        return;

    if (!showing)
        return;

    Actor actor(targetAddr_);
    if (!actor.isRemotePlayer())
    {
        targetAddr_ = 0;
        return;
    }

    targetName_ = actor.name();
    if (targetName_.empty())
        targetName_ = "Player";

    maxHealth_ = actor.maxHealth();
    if (maxHealth_ < 1.0f || maxHealth_ > 2048.0f)
        maxHealth_ = 20.0f;
    updateHealth(actor, maxHealth_, dt);

    updateSkinTexture(actor);
    drawCard(local, actor, dt);
}

void TargetHUD::updateRiseAnimation(bool showing, uint64_t nowMs)
{
    const float destination = showing ? 1.0f : 0.0f;
    if (destination != riseAnimTarget_)
    {
        riseAnimFrom_ = anim_;
        riseAnimTarget_ = destination;
        riseAnimStartMs_ = nowMs;
        riseAnimDurationMs_ = showing ? 850u : 400u;
    }

    const float progress = std::clamp(
        static_cast<float>(nowMs - riseAnimStartMs_) /
            static_cast<float>(std::max<uint32_t>(riseAnimDurationMs_, 1u)),
        0.0f, 1.0f);
    float eased = progress;
    if (showing)
    {
        if (progress <= 0.0f)
            eased = 0.0f;
        else if (progress >= 1.0f)
            eased = 1.0f;
        else
            eased = std::pow(2.0f, -10.0f * progress) *
                        std::sin((progress * 10.0f - 0.75f) * 2.09439510239f) * 0.5f +
                    1.0f;
    }
    else
    {
        eased = 2.70158f * progress * progress * progress -
                1.70158f * progress * progress;
    }
    anim_ = riseAnimFrom_ + (riseAnimTarget_ - riseAnimFrom_) * eased;
    if (progress >= 1.0f)
        anim_ = riseAnimTarget_;
}

void TargetHUD::renderRise(const LocalPlayer& local, float dt)
{
    const uint64_t nowMs = GetTickCount64();
    const uintptr_t candidate = targetAddr_;
    if (candidate != 0)
    {
        if (riseTargetAddr_ != candidate)
        {
            riseTargetAddr_ = candidate;
            riseFollowTarget_ = candidate;
            riseFollowInitialized_ = false;
            displayedTarget_ = 0;
            riseParticlesLive_.clear();
        }
        riseLastTargetMs_ = nowMs;
    }

    const bool graceActive = riseTargetAddr_ != 0 &&
        nowMs - riseLastTargetMs_ < 1000ull;
    updateRiseAnimation(graceActive, nowMs);

    if (riseTargetAddr_ != 0)
    {
        Actor actor(riseTargetAddr_);
        riseTargetValid_ = actor.isRemotePlayer();
        if (riseTargetValid_)
        {
            targetName_ = actor.name();
            if (targetName_.empty())
                targetName_ = "Player";

            maxHealth_ = actor.maxHealth();
            if (maxHealth_ < 1.0f || maxHealth_ > 2048.0f)
                maxHealth_ = 20.0f;
            const bool tookDamage = updateHealth(actor, maxHealth_, dt);
            if (riseParticles_ && tookDamage)
            {
                for (int i = 0; i < 7; ++i)
                {
                    const float randUnit = (float)(std::rand() % 2001) / 1000.0f - 1.0f;
                    RiseParticle particle;
                    particle.x = randUnit * 11.0f;
                    particle.y = randUnit * 8.0f;
                    particle.vx = randUnit * 70.0f;
                    particle.vy = -22.0f - (float)(std::rand() % 55);
                    riseParticlesLive_.push_back(particle);
                }
            }

            riseDistance_ = distance3D(local.pos(), actor.pos());
            riseLocalHealth_ = local.currentHealth();
            updateSkinTexture(actor);
        }
    }
    else
    {
        riseTargetValid_ = false;
    }

    if (riseTargetAddr_ == 0 || anim_ > 0.001f)
        drawRiseCard(local, dt);

    if (riseAnimTarget_ == 0.0f && anim_ <= 0.001f)
    {
        riseTargetAddr_ = 0;
        displayedTarget_ = 0;
        targetName_.clear();
        riseParticlesLive_.clear();
        releaseSkinTexture();
        skinOwner_ = 0;
    }
}

void TargetHUD::drawRiseCard(const LocalPlayer& local, float dt)
{
    (void)local;
    if (anim_ <= 0.001f)
        return;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    if (display.x <= 0.0f || display.y <= 0.0f)
        return;

    const float s = scale_;
    // Rise uses light/medium fonts; these slightly smaller sizes account for
    // the taller glyph metrics of the fonts available in Azyre.
    const float titleSize = 20.0f * s;
    const float healthSize = 20.0f * s;
    const char* advantage = !riseTargetValid_ || riseLocalHealth_ < 0.0f ? "Target:" :
        (riseLocalHealth_ >= health_ ? "Winning:" : "Losing:");
    const char* visibleName = targetName_.empty() ? "Player" : targetName_.c_str();
    char healthText[24]{};
    std::snprintf(healthText, sizeof(healthText), "%.1f", health_);

    ImFont* font = dx11::riseFont();
    if (!font)
        font = ImGui::GetFont();
    ImFont* mediumFont = dx11::hudFont();
    if (!mediumFont)
        mediumFont = font;
    const float advantageW = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, advantage).x;
    const float nameW = mediumFont->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, visibleName).x;
    const float healthW = mediumFont->CalcTextSizeA(healthSize, FLT_MAX, 0.0f, healthText).x;
    const float barW = std::max(65.0f * s, advantageW + nameW + 35.0f * s - healthW);
    const float fullW = 48.0f * s + barW + 4.0f * s + healthW + 8.0f * s;
    // Slightly taller than Rise's original 48 px card because ImGui's Product
    // Sans metrics have a taller glyph box; this keeps the two rows separate.
    const float fullH = 56.0f * s;
    const float transform = std::clamp(anim_, 0.0f, 1.10f);
    const float alpha = std::clamp(anim_, 0.0f, 1.0f);

    const ImVec2 defaultPos(display.x * 0.5f - fullW * 0.5f,
                            display.y * 0.5f - fullH * 0.5f);
    ImVec2 defaultRisePos = defaultPos;
    bool riseProjectionValid = false;
    if (riseFollowPlayer_ && riseTargetValid_ && riseTargetAddr_ != 0)
    {
        Actor target(riseTargetAddr_);
        if (target.valid())
        {
            const CameraView camera = getCameraView(local, Game::get().client());
            const Vec3 exact = target.posExact();
            const Vec3 feet = target.pos();
            const float targetHeight = target.height();
            Vec3 screenFeet{}, screenHead{};
            const Vec3 worldFeet{exact.x, feet.y, exact.z};
            const Vec3 worldHead{exact.x, feet.y + targetHeight, exact.z};
            if (camera.project(worldFeet, screenFeet) &&
                camera.project(worldHead, screenHead))
            {
                defaultRisePos = ImVec2((screenFeet.x + screenHead.x) * 0.5f - fullW * 0.5f,
                                        (screenFeet.y + screenHead.y) * 0.5f - fullH * 0.5f);
                defaultRisePos.x = std::clamp(defaultRisePos.x, 0.0f,
                                               std::max(0.0f, display.x - fullW));
                defaultRisePos.y = std::clamp(defaultRisePos.y, 0.0f,
                                               std::max(0.0f, display.y - fullH));
                riseProjectionValid = true;
            }
        }
    }
    if (riseFollowPlayer_)
    {
        if (riseProjectionValid)
        {
            if (!riseFollowInitialized_ || riseFollowTarget_ != riseTargetAddr_)
            {
                riseFollowX_ = defaultRisePos.x;
                riseFollowY_ = defaultRisePos.y;
                riseFollowTarget_ = riseTargetAddr_;
                riseFollowInitialized_ = true;
            }
            else
            {
                const float follow = 1.0f - std::exp(-14.0f * std::clamp(dt, 0.0f, 0.1f));
                riseFollowX_ += (defaultRisePos.x - riseFollowX_) * follow;
                riseFollowY_ += (defaultRisePos.y - riseFollowY_) * follow;
            }
            defaultRisePos = ImVec2(riseFollowX_, riseFollowY_);
        }
        else if (riseFollowInitialized_)
        {
            defaultRisePos = ImVec2(riseFollowX_, riseFollowY_);
        }
    }
    else
    {
        riseFollowInitialized_ = false;
    }
    const ImVec2 panelPos = hud::Registry::get().report("TargetHUD", defaultPos,
                                                         ImVec2(fullW, fullH));
    const ImVec2 effectivePos = riseFollowPlayer_ ? defaultRisePos : panelPos;
    const ImVec2 center(effectivePos.x + fullW * 0.5f, effectivePos.y + fullH * 0.5f);
    const ImVec2 boxMin(center.x - fullW * transform * 0.5f,
                        center.y - fullH * transform * 0.5f);
    const ImVec2 boxMax(center.x + fullW * transform * 0.5f,
                        center.y + fullH * transform * 0.5f);
    const float rounding = 16.0f * s * transform;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    // Rise queues blur underneath every Modern background mode. The mode
    // controls the gradient painted over that blurred surface.
    blur::requestFullRate();
    void* blurTexture = blur::ready() ? blur::srv() : nullptr;
    if (blurTexture)
    {
        const ImVec2 uv0(boxMin.x / display.x, boxMin.y / display.y);
        const ImVec2 uv1(boxMax.x / display.x, boxMax.y / display.y);
        draw->AddImageRounded(ImTextureRef(blurTexture), boxMin, boxMax, uv0, uv1,
                              IM_COL32(255, 255, 255, (int)(255.0f * alpha)), rounding);
    }
    else
    {
        addRiseRoundedGradient(draw, boxMin, boxMax, rounding,
                               ImVec4(0.035f, 0.045f, 0.075f, alpha),
                               ImVec4(0.012f, 0.016f, 0.030f, alpha));
    }

    const ImVec4 accent = theme::AccentAt4(center.x, center.y);
    ImVec4 bgTop(0.035f, 0.045f, 0.075f, 0.52f * alpha);
    ImVec4 bgBottom(0.012f, 0.016f, 0.030f, 0.52f * alpha);
    ImVec4 primaryTop(accent.x, accent.y, accent.z, alpha);
    ImVec4 primaryBottom = theme::AccentTri ? theme::Accent3 : theme::Accent2;
    ImVec4 healthTop = primaryTop;
    ImVec4 healthBottom = primaryBottom;
    if (riseBackground_ == 1) // Tint
    {
        const ImVec4 tintTop = theme::AccentAt4(boxMin.x, boxMin.y);
        const ImVec4 tintBottom = theme::AccentAt4(boxMin.x, boxMax.y);
        bgTop = ImVec4(tintTop.x / 5.0f, tintTop.y / 5.0f, tintTop.z / 5.0f, 0.50f * alpha);
        bgBottom = ImVec4(tintBottom.x / 5.0f, tintBottom.y / 5.0f, tintBottom.z / 5.0f, 0.50f * alpha);
    }
    else if (riseBackground_ == 2) // Solid
    {
        bgTop = ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.50f * alpha);
        bgBottom = ImVec4(theme::Accent2.x, theme::Accent2.y, theme::Accent2.z, 0.50f * alpha);
        healthTop = ImVec4(1.0f, 1.0f, 1.0f, alpha);
        healthBottom = ImVec4(0.643f, 0.643f, 0.643f, alpha);
    }
    addRiseRoundedGradient(draw, boxMin, boxMax, rounding, bgTop, bgBottom);

    const float headSize = 32.0f * s * transform;
    const ImVec2 headMin(boxMin.x + 8.0f * s * transform,
                         boxMin.y + (boxMax.y - boxMin.y - headSize) * 0.5f);
    const ImVec2 headMax(headMin.x + headSize, headMin.y + headSize);
    // Rise places a soft drop shadow under the portrait and bloom around the
    // panel; avoid the hard concentric rings that made the old card look heavy.
    widgets::SoftShadow(draw, headMin, headMax, 10.0f * s * transform,
                        30.0f * alpha, 8.0f * s);
    if (skinSrv_)
        draw->AddImageRounded(ImTextureRef(skinSrv_), headMin, headMax,
                              ImVec2(0, 0), ImVec2(1, 1),
                              IM_COL32(255, 255, 255, (int)(255.0f * alpha)), 8.0f * s);
    else if (void* head = resources::srv(IDR_STEVE_HEAD))
        draw->AddImageRounded(ImTextureRef(head), headMin, headMax,
                              ImVec2(0, 0), ImVec2(1, 1),
                              IM_COL32(255, 255, 255, (int)(255.0f * alpha)), 8.0f * s);

    const float contentX = boxMin.x + 47.0f * s * transform;
    const float statusY = boxMin.y + 11.0f * s * transform;
    const float animatedTitleSize = titleSize * transform;
    const ImU32 white = IM_COL32(245, 247, 250, (int)(255.0f * alpha));
    draw->AddText(font, animatedTitleSize, ImVec2(contentX, statusY), white, advantage);
    const ImVec2 advantageSize = font->CalcTextSizeA(animatedTitleSize, FLT_MAX, 0.0f, advantage);
    const ImVec4 nameColor = riseBackground_ == 2
        ? ImVec4(1.0f, 1.0f, 1.0f, alpha)
        : theme::AccentAt4(contentX + advantageSize.x + 3.0f * s, statusY);
    draw->AddText(mediumFont, animatedTitleSize, ImVec2(contentX + advantageSize.x + 3.0f * s, statusY),
                  ImGui::ColorConvertFloat4ToU32(ImVec4(nameColor.x, nameColor.y, nameColor.z, alpha)),
                  visibleName);

    const float barY = boxMin.y + 34.0f * s * transform;
    const ImVec2 barMin(contentX, barY);
    const ImVec2 barMax(contentX + barW * transform, barY + 6.0f * s * transform);
    const float barRadius = 3.0f * s * transform;
    ImVec4 underTop = primaryTop;
    ImVec4 underBottom = primaryBottom;
    underTop.w = (riseBackground_ == 2 ? 0.32f : 0.34f) * alpha;
    underBottom.w = (riseBackground_ == 2 ? 0.32f : 0.34f) * alpha;
    addRiseRoundedGradient(draw, barMin, barMax, barRadius, underTop, underBottom);
    const float healthPct = std::clamp(health_ / std::max(maxHealth_, 1.0f), 0.0f, 1.0f);
    if (healthPct > 0.001f)
    {
        const float filledWidth = std::max(barRadius * 2.0f,
            (barMax.x - barMin.x) * healthPct);
        const ImVec2 healthMax(std::min(barMin.x + filledWidth, barMax.x), barMax.y);
        addRiseRoundedGradient(draw, barMin, healthMax,
                               std::min(barRadius, (healthMax.x - barMin.x) * 0.5f),
                               healthTop, healthBottom);
    }
    const ImVec4 healthTextColor = riseBackground_ == 2
        ? ImVec4(1.0f, 1.0f, 1.0f, alpha)
        : ImVec4(primaryBottom.x, primaryBottom.y, primaryBottom.z, alpha);
    draw->AddText(mediumFont, healthSize * transform,
                  ImVec2(barMax.x + 4.0f * s * transform,
                         boxMin.y + 32.0f * s * transform),
                  ImGui::ColorConvertFloat4ToU32(healthTextColor), healthText);

    for (size_t i = 0; riseParticles_ && i < riseParticlesLive_.size();)
    {
        RiseParticle& particle = riseParticlesLive_[i];
        particle.age += dt;
        particle.x += particle.vx * dt;
        particle.y += particle.vy * dt;
        particle.vy += 110.0f * dt;
        if (particle.age >= particle.lifetime)
        {
            riseParticlesLive_.erase(riseParticlesLive_.begin() + i);
            continue;
        }
        const float particleAlpha = (1.0f - particle.age / particle.lifetime) * alpha;
        draw->AddCircleFilled(ImVec2(headMin.x + headSize * 0.5f + particle.x * s,
                                     headMin.y + headSize * 0.5f + particle.y * s),
                              1.5f * s, theme::AccentU32(particleAlpha));
        ++i;
    }
    if (!riseParticles_)
        riseParticlesLive_.clear();

    draw->AddRect(boxMin, boxMax,
                  ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z,
                                                        0.24f * alpha)),
                  rounding, 0, 1.0f * s);
}

void TargetHUD::drawCard(const LocalPlayer& local, const Actor& actor, float dt)
{
    (void)dt;
    const float s = scale_;
    const float a = anim_;
    const float fullW = 240.0f * s;
    const float fullH = 74.0f * s;
    const float w = fullW * a;
    const float h = fullH * a;
    if (w < 1.0f || h < 1.0f)
        return;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 defaultPos(display.x * 0.5f - fullW * 0.5f,
                            display.y * 0.5f - fullH * 0.5f);
    const ImVec2 panelPos = hud::Registry::get().report(
        "TargetHUD", defaultPos, ImVec2(fullW, fullH));
    const ImVec2 boxMin(panelPos.x + (fullW - w) * 0.5f,
                        panelPos.y + (fullH - h) * 0.5f);
    const ImVec2 boxMax(boxMin.x + w, boxMin.y + h);
    const float radius = 12.0f * s * a;
    const float pad = 12.5f * s * a;
    const bool hasBlur = blur::wanted() && blur::ready() && blur::srv();

    widgets::SoftShadow(draw, boxMin, boxMax, radius, 24.0f * a, 8.0f * s);
    if (hasBlur && display.x > 0.0f && display.y > 0.0f)
    {
        const ImVec2 uv0(boxMin.x / display.x, boxMin.y / display.y);
        const ImVec2 uv1(boxMax.x / display.x, boxMax.y / display.y);
        draw->AddImageRounded(ImTextureRef(blur::srv()), boxMin, boxMax, uv0, uv1,
                              IM_COL32(255, 255, 255, static_cast<int>(255.0f * a)), radius);
        draw->AddRectFilled(boxMin, boxMax,
                            IM_COL32(8, 10, 16, static_cast<int>(95.0f * a)), radius);
    }
    else
        draw->AddRectFilled(boxMin, boxMax,
                            IM_COL32(10, 12, 18, static_cast<int>(232.0f * a)), radius);

    const ImU32 accent = friends::isFriend(targetName_)
        ? friends::colorU32()
        : theme::AccentU32(a);
    draw->AddRectFilled(boxMin, ImVec2(boxMin.x + 3.0f * s * a, boxMax.y), accent, radius);
    draw->AddRect(boxMin, boxMax,
                  IM_COL32(255, 255, 255, static_cast<int>(28.0f * a)), radius, 0, 1.0f * s);

    const float headSize = 55.0f * s * a;
    const ImVec2 headMin(boxMin.x + pad, boxMin.y + (h - headSize) * 0.5f);
    const ImVec2 headMax(headMin.x + headSize, headMin.y + headSize);
    if (skinSrv_)
    {
        const ImTextureRef texture(skinSrv_);
        draw->AddImageRounded(texture, headMin, headMax,
                              ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                              IM_COL32(255, 255, 255, static_cast<int>(255.0f * a)),
                              10.0f * s * a);
    }
    else if (void* head = resources::srv(IDR_STEVE_HEAD))
    {
        draw->AddImageRounded(ImTextureRef(head), headMin, headMax,
                              ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                              IM_COL32(255, 255, 255, static_cast<int>(255.0f * a)),
                              10.0f * s * a);
    }

    const float contentX = headMax.x + 12.0f * s * a;
    const float rightPad = 12.0f * s * a;
    const float contentRight = boxMax.x - rightPad;
    const float textSize = fontSize_ * s * a;
    const float titleY = boxMin.y + 9.0f * s * a;
    const ImU32 textWhite = IM_COL32(245, 247, 250, static_cast<int>(255.0f * a));
    draw->AddText(nullptr, textSize, ImVec2(contentX, titleY), textWhite,
                  targetName_.c_str());

    const float distance = distance3D(local.pos(), actor.pos());
    const float localHealth = local.currentHealth();
    const bool winning = localHealth >= 0.0f && health_ >= 0.0f && localHealth >= health_;
    const char* status = localHealth < 0.0f ? "Status unavailable"
        : (winning ? "Winning" : "Losing");
    const float statusY = titleY + 23.0f * s * a;
    if (showStatus_)
    {
        const ImU32 statusColor = localHealth < 0.0f
            ? IM_COL32(185, 190, 202, static_cast<int>(220.0f * a))
            : winning
            ? IM_COL32(140, 225, 170, static_cast<int>(245.0f * a))
            : IM_COL32(240, 142, 142, static_cast<int>(245.0f * a));
        draw->AddText(nullptr, 13.0f * s * a, ImVec2(contentX, statusY), statusColor, status);
    }
    if (showDistance_)
    {
        char distanceText[24]{};
        std::snprintf(distanceText, sizeof(distanceText), "%.1fm", distance);
        const ImVec2 distanceSize = ImGui::GetFont()->CalcTextSizeA(
            12.0f * s * a, FLT_MAX, 0.0f, distanceText);
        draw->AddText(nullptr, 12.0f * s * a,
                      ImVec2(contentRight - distanceSize.x, statusY),
                      IM_COL32(185, 190, 202, static_cast<int>(220.0f * a)), distanceText);
    }

    if (showHealth_)
    {
        const float barH = 13.0f * s * a;
        const ImVec2 barMin(contentX, boxMax.y - 12.0f * s * a - barH);
        const ImVec2 barMax(contentRight, barMin.y + barH);
        draw->AddRectFilled(barMin, barMax,
                            IM_COL32(100, 100, 110, static_cast<int>(170.0f * a)),
                            8.0f * s * a);

        const float healthPct = std::clamp(health_ / maxHealth_, 0.0f, 1.0f);
        const ImVec2 healthMax(barMin.x + (barMax.x - barMin.x) * healthPct, barMax.y);
        ImVec4 start = theme::Accent;
        ImVec4 end = theme::AccentTri ? theme::Accent3 : theme::Accent2;
        start.w *= a;
        end.w *= a;
        if (healthPct > 0.01f)
        {
            draw->PushClipRect(barMin, healthMax, true);
            draw->AddRectFilledMultiColor(barMin, barMax,
                ImGui::ColorConvertFloat4ToU32(start), ImGui::ColorConvertFloat4ToU32(end),
                ImGui::ColorConvertFloat4ToU32(end), ImGui::ColorConvertFloat4ToU32(start));
            draw->PopClipRect();
        }
        char healthText[32]{};
        std::snprintf(healthText, sizeof(healthText), "%.0f / %.0f", health_, maxHealth_);
        draw->AddText(nullptr, 10.0f * s * a,
                      ImVec2(barMin.x + 5.0f * s * a, barMin.y + 1.0f * s * a),
                      IM_COL32(255, 255, 255, static_cast<int>(240.0f * a)), healthText);
    }
}

void TargetHUD::drawSettings()
{
    const char* modes[] = {"Azyre", "Rise"};
    widgets::CCombo("Mode##th", &mode_, modes, 2);
    if (mode_ == 1)
    {
        const char* backgrounds[] = {"Glass", "Tint", "Solid"};
        widgets::CCombo("Background Mode##th", &riseBackground_, backgrounds, 3);
        widgets::Checkbox("Particles##th", &riseParticles_);
        widgets::Checkbox("Follow Player##th", &riseFollowPlayer_);

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const ImVec2 size(340.0f * scale_, 48.0f * scale_);
        const ImVec2 center(display.x * 0.5f - size.x * 0.5f,
                            display.y * 0.5f - size.y * 0.5f);
        hud::Registry::get().report("TargetHUD", center, size);
        if (hud::Element* element = hud::Registry::get().find("TargetHUD"))
        {
            const float maxX = std::max(0.0f, display.x - element->size.x);
            const float maxY = std::max(0.0f, display.y - element->size.y);
            element->pos.x = std::clamp(element->pos.x, 0.0f, maxX);
            element->pos.y = std::clamp(element->pos.y, 0.0f, maxY);
            bool positionChanged = widgets::CSlider("Position X##th", &element->pos.x,
                                                     0.0f, maxX, "%.0f px");
            positionChanged |= widgets::CSlider("Position Y##th", &element->pos.y,
                                                 0.0f, maxY, "%.0f px");
            if (positionChanged)
                hud::Registry::get().save();
        }
    }
    widgets::CSlider("Font Size##th", &fontSize_, 12.0f, 32.0f, "%.0f px");
    widgets::CSlider("Scale##th", &scale_, 0.5f, 2.0f, "%.1f");
    // La vida del remoto no llega por red: el HUD la estima con el dano real
    // de cada golpe enviado (arma en mano) o con un valor fijo calibrado.
    const char* damageSources[] = {"Weapon", "Custom"};
    widgets::CCombo("Damage Source##th", &damageMode_, damageSources, 2);
    if (damageMode_ == 1)
        widgets::CSlider("Damage Per Hit##th", &customDamage_, 0.5f, 20.0f, "%.1f");
    if (mode_ == 0)
    {
        widgets::Checkbox("Health##th", &showHealth_);
        widgets::Checkbox("Status##th", &showStatus_);
        widgets::Checkbox("Distance##th", &showDistance_);
    }
}

MC_REGISTER_MODULE(TargetHUD);

} // namespace mc
