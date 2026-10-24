#include "FakeLag.hpp"
#include "GUI/Widgets.hpp"

#include "GUI/Menu.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

namespace {

// Enforce at least one release condition (both 0 = hold forever = Blink
// duplicado): both clamped to 0 in loadSettings get restored to this.
constexpr int kDefaultDepth = 6;

} // namespace

FakeLag::FakeLag()
    : Module("FakeLag", "Delays your movement packets and releases them in bursts: you advance smoothly while others see rubber-banding",
             Category::Movement, 0)
{
    markHasSettings();
}

void FakeLag::releaseCycle(ULONGLONG now)
{
    // Drain (rafaga FIFO al server) y arranque de un ciclo nuevo. El gap
    // entre deactivate/activate deja pasar a lo sumo 1 paquete en vuelo
    // del game thread: comportamiento aceptado del fakelag (gap natural).
    blink::deactivate();
    cycleStart_ = GetTickCount64();
    blink::activate();
}

bool FakeLag::releaseConditionsMet() const
{
    if (!blink::active())
        return false;

    size_t n = blink::pendingCount();
    if (maxPackets_ > 0 && n >= (size_t)maxPackets_)
        return true;
    if (holdMs_ > 0.0f)
    {
        ULONGLONG now = GetTickCount64();
        if (now - cycleStart_ >= (ULONGLONG)holdMs_)
            return true;
    }
    return false;
}

void FakeLag::onEnable()
{
    // El hook de sendToServer debe estar vivo para capturar (el pequeño
    // install() es idempotente igual que en Blink).
    chathook::install();

    // El buffer es compartido con Blink: dos manejarlo desincroniza los
    // ciclos mutuamente. Refuse-and-report en vez de pelear por el buffer.
    Module* blinkMod = ModuleManager::get().find("Blink");
    if (blinkMod && blinkMod->enabled())
    {
        chathook::display("§7[§cFakeLag§7]§f Disabled: the Blink module owns the packet buffer — toggle Blink off first.");
        setEnabled(false);
        return;
    }

    cycleStart_ = GetTickCount64();
    blink::activate();
}

void FakeLag::onDisable()
{
    blink::deactivate(); // drena el buffer pendiente
}

void FakeLag::onShutdown()
{
    blink::deactivate();
}

void FakeLag::onTick()
{
    if (releaseConditionsMet())
        releaseCycle(GetTickCount64());
}

void FakeLag::onHurt(void* actor, void* source, float& damage, bool& cancel)
{
    if (releaseOnDamage_ && blink::active() && blink::pendingCount() > 0)
        releaseCycle(GetTickCount64());
}

void FakeLag::drawSettings()
{
    widgets::CSlider("Hold time (ms)##fl", &holdMs_, 0.0f, 2000.0f, "%.0f ms");
    widgets::CSlider("Depth##fl", &maxPackets_, 0, 60);
    widgets::Checkbox("Release on damage##fl", &releaseOnDamage_);

    ImGui::TextDisabled("0 ms = depth only / 0 depth = time only.");
    ImGui::TextDisabled("Retained: %zu packets", blink::pendingCount());
    ImGui::TextDisabled("Shares the Blink buffer: both at once is refused.");
}

nlohmann::json FakeLag::saveSettings() const
{
    return nlohmann::json{
        {"holdMs", holdMs_},
        {"maxPackets", maxPackets_},
        {"releaseOnDamage", releaseOnDamage_},
    };
}

void FakeLag::loadSettings(const nlohmann::json& j)
{
    holdMs_ = j.value("holdMs", holdMs_);
    if (holdMs_ < 0.0f)
        holdMs_ = 0.0f;
    if (holdMs_ > 2000.0f)
        holdMs_ = 2000.0f;

    maxPackets_ = j.value("maxPackets", maxPackets_);
    if (maxPackets_ < 0)
        maxPackets_ = 0;
    if (maxPackets_ > 60)
        maxPackets_ = 60;

    // Ambos a 0 = retencion indefinida (= un Blink duplicado): restaurar el
    // default de profundidad para que siempre haya un disparador.
    if (maxPackets_ == 0 && holdMs_ <= 0.0f)
        maxPackets_ = kDefaultDepth;

    releaseOnDamage_ = j.value("releaseOnDamage", releaseOnDamage_);
}

MC_REGISTER_MODULE(FakeLag);

}
