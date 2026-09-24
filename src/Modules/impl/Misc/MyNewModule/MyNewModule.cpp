#include "MyNewModule.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Log.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

MyNewModule::MyNewModule()
    : Module("MyNewModule", "Plantilla: como crear un modulo nuevo", Category::Misc, VK_F6)
{
    markHasSettings();
}

void MyNewModule::onEnable()
{
    MC_LOG("[Module] MyNewModule enabled");
}

void MyNewModule::onDisable()
{
    MC_LOG("[Module] MyNewModule disabled");
}

void MyNewModule::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    if (exampleFlag_)
    {
        LocalPlayer local = game.localPlayer();
        Vec3 vel = local.velocity();
        (void)vel;
    }
}

void MyNewModule::onRender()
{
    if (!exampleFlag_)
        return;

    ImGui::GetBackgroundDrawList()->AddText(ImVec2(20, 200), IM_COL32(255, 255, 0, 255),
                                            "MyNewModule active");
}

void MyNewModule::drawSettings()
{
    widgets::CSlider("Example value", &exampleValue_, 0.0f, 10.0f);
    ImGui::Checkbox("Example flag", &exampleFlag_);
}

MC_REGISTER_MODULE(MyNewModule);

}
