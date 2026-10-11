#include "Scripting.hpp"

#include "Config/Config.hpp"
#include "Features/Friends.hpp"
#include "Framework/Log.hpp"
#include "GUI/Widgets.hpp"
#include "Input/KeyNames.hpp"
#include "Modules/ModuleManager.hpp"
#include "SDK/Camera.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Classes/MoveInputHandler.hpp"
#include "SDK/Game.hpp"

#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

namespace mc::scripts {

namespace {

constexpr const char* kRegKey = "azyre_module";

struct SettingDecl
{
    std::string id;
    std::string label;
    bool isFloat = true;
    bool isCombo = false;
    std::vector<std::string> options;
    double minv = 0.0;
    double maxv = 1.0;
    double fVal = 0.0;
    bool bVal = false;
};

struct ScriptMeta
{
    std::string name;
    std::string description;
    Category category = Category::Misc;
    int keybind = 0;
    std::vector<SettingDecl> defs;
};

// ======================================================================
// Bindings expuestas a Luau
// ======================================================================

void pushStr(lua_State* L, const char* k, const char* v)
{
    lua_pushstring(L, v);
    lua_rawsetfield(L, -2, k);
}

void pushNum(lua_State* L, const char* k, double v)
{
    lua_pushnumber(L, v);
    lua_rawsetfield(L, -2, k);
}

void pushBool(lua_State* L, const char* k, bool v)
{
    lua_pushboolean(L, v);
    lua_rawsetfield(L, -2, k);
}

static ImU32 optColor(lua_State* L, int start, ImU32 def)
{
    int top = lua_gettop(L);
    if (top >= start + 2 && lua_isnumber(L, start) && lua_isnumber(L, start + 1) &&
        lua_isnumber(L, start + 2))
    {
        int r = (int)lua_tonumber(L, start);
        int g = (int)lua_tonumber(L, start + 1);
        int b = (int)lua_tonumber(L, start + 2);
        int a = 255;
        if (top >= start + 3 && lua_isnumber(L, start + 3))
            a = (int)lua_tonumber(L, start + 3);
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;
        if (a < 0) a = 0; if (a > 255) a = 255;
        return IM_COL32(r, g, b, a);
    }
    return def;
}

static int l_game_localPlayer(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
    {
        lua_pushnil(L);
        return 1;
    }
    LocalPlayer p = g.localPlayer();
    if (!p.valid())
    {
        lua_pushnil(L);
        return 1;
    }

    Vec3 pos = p.pos();
    Vec3 vel = p.velocity();
    lua_createtable(L, 0, 12);
    pushStr(L, "name", p.name().c_str());
    pushNum(L, "x", pos.x);
    pushNum(L, "y", pos.y);
    pushNum(L, "z", pos.z);
    pushNum(L, "vx", vel.x);
    pushNum(L, "vy", vel.y);
    pushNum(L, "vz", vel.z);
    pushNum(L, "yaw", p.yaw());
    pushNum(L, "pitch", p.pitch());
    pushNum(L, "health", (double)p.health());
    pushNum(L, "height", p.height());
    pushBool(L, "onGround", p.onGround());
    pushNum(L, "slot", (double)p.hotbarSlot());
    pushBool(L, "leftClick", p.leftClickFlag());
    pushBool(L, "usingItem", p.isUsingItem());
    pushNum(L, "hurtTime", (double)p.hurtTime());
    pushBool(L, "isHurt", p.isHurt());
    Vec3 eye = p.eyePos();
    pushNum(L, "eyeX", eye.x);
    pushNum(L, "eyeY", eye.y);
    pushNum(L, "eyeZ", eye.z);
    return 1;
}

static int l_game_players(lua_State* L)
{
    Game& g = Game::get();
    const auto& list = g.remotePlayers();
    lua_createtable(L, (int)list.size(), 0);
    int i = 1;
    for (const Actor& a : list)
    {
        if (!a.valid())
            continue;
        Vec3 pos = a.pos();
        lua_createtable(L, 0, 9);
        pushStr(L, "name", a.name().c_str());
        pushNum(L, "x", pos.x);
        pushNum(L, "y", pos.y);
        pushNum(L, "z", pos.z);
        pushNum(L, "yaw", a.yaw());
        pushNum(L, "pitch", a.pitch());
        pushNum(L, "health", (double)a.health());
        pushNum(L, "height", a.height());
        pushBool(L, "onGround", a.onGround());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

static int l_game_setOnGround(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
        return 0;
    LocalPlayer p = g.localPlayer();
    if (!p.valid())
        return 0;
    if (!lua_isboolean(L, 1))
        return 0;
    p.setOnGround(lua_toboolean(L, 1));
    return 0;
}

static int l_game_setVelocity(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
        return 0;
    LocalPlayer p = g.localPlayer();
    if (!p.valid())
        return 0;
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3))
        return 0;
    p.setVelocity({(float)lua_tonumber(L, 1), (float)lua_tonumber(L, 2),
                   (float)lua_tonumber(L, 3)});
    return 0;
}

static int l_game_setPos(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
        return 0;
    LocalPlayer p = g.localPlayer();
    if (!p.valid())
        return 0;
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3))
        return 0;
    p.setPos({(float)lua_tonumber(L, 1), (float)lua_tonumber(L, 2),
              (float)lua_tonumber(L, 3)});
    return 0;
}

static int l_game_setYaw(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
        return 0;
    LocalPlayer p = g.localPlayer();
    if (!p.valid())
        return 0;
    if (!lua_isnumber(L, 1))
        return 0;
    p.setYaw((float)lua_tonumber(L, 1));
    return 0;
}

static int l_game_setPitch(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
        return 0;
    LocalPlayer p = g.localPlayer();
    if (!p.valid())
        return 0;
    if (!lua_isnumber(L, 1))
        return 0;
    float pitch = (float)lua_tonumber(L, 1);
    if (pitch < -90.0f)
        pitch = -90.0f;
    if (pitch > 90.0f)
        pitch = 90.0f;
    p.setPitch(pitch);
    return 0;
}

static int l_game_setVelocityXZ(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
        return 0;
    LocalPlayer p = g.localPlayer();
    if (!p.valid())
        return 0;
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2))
        return 0;
    p.setVelocityXZ({(float)lua_tonumber(L, 1), 0.0f, (float)lua_tonumber(L, 2)});
    return 0;
}

static int l_game_hotbar(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
    {
        lua_pushnil(L);
        return 1;
    }
    LocalPlayer p = g.localPlayer();
    if (!p.valid())
    {
        lua_pushnil(L);
        return 1;
    }
    Inventory inv = p.inventory();
    lua_createtable(L, 9, 1);
    for (int i = 0; i < 9; ++i)
    {
        ItemInstance it(inv.hotbarItem(i));
        lua_createtable(L, 0, 3);
        pushBool(L, "valid", it.valid());
        pushNum(L, "count", (double)it.count());
        pushBool(L, "block", it.isBlockItem());
        lua_rawseti(L, -2, i + 1);
    }
    pushNum(L, "selected", (double)p.hotbarSlot());
    return 1;
}

static int l_game_setHotbarSlot(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
        return 0;
    LocalPlayer p = g.localPlayer();
    if (!p.valid() || !lua_isnumber(L, 1))
        return 0;
    int slot = (int)lua_tonumber(L, 1);
    if (slot < 0)
        slot = 0;
    if (slot > 8)
        slot = 8;
    p.setHotbarSlot(slot);
    return 0;
}

static int l_game_camera(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
    {
        lua_pushnil(L);
        return 1;
    }
    LocalPlayer p = g.localPlayer();
    if (!p.valid())
    {
        lua_pushnil(L);
        return 1;
    }
    CameraView cam = getCameraView(p, g.client());
    lua_createtable(L, 0, 5);
    pushNum(L, "x", cam.pos.x);
    pushNum(L, "y", cam.pos.y);
    pushNum(L, "z", cam.pos.z);
    pushNum(L, "yaw", cam.yaw);
    pushNum(L, "pitch", cam.pitch);
    return 1;
}

static int l_game_worldToScreen(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound() || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3))
    {
        lua_pushnil(L);
        return 1;
    }
    LocalPlayer p = g.localPlayer();
    if (!p.valid())
    {
        lua_pushnil(L);
        return 1;
    }
    CameraView cam = getCameraView(p, g.client());
    float fov = readGameFov();
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    Vec3 proj;
    Vec3 w = {(float)lua_tonumber(L, 1), (float)lua_tonumber(L, 2),
              (float)lua_tonumber(L, 3)};
    if (!worldToScreen(w, cam.pos, cam.yaw, cam.pitch, fov, disp.x, disp.y, proj))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, proj.x);
    lua_pushnumber(L, proj.y);
    return 2;
}

static int l_game_click(lua_State* L)
{
    Game& g = Game::get();
    if (!g.localFound())
        return 0;
    LocalPlayer p = g.localPlayer();
    MoveInputHandler input(p.moveInputAddr());
    if (!input.valid())
        return 0;
    if (lua_isboolean(L, 1) && lua_toboolean(L, 1))
        input.clickFast();
    else
        input.click();
    return 0;
}

static int l_modules_toggle(lua_State* L)
{
    if (!lua_isstring(L, 1) || !lua_isboolean(L, 2))
        return 0;
    Module* m = ModuleManager::get().find(lua_tostring(L, 1));
    if (m)
        m->setEnabled(lua_toboolean(L, 2));
    return 0;
}

static int l_modules_enabled(lua_State* L)
{
    if (!lua_isstring(L, 1))
    {
        lua_pushnil(L);
        return 1;
    }
    Module* m = ModuleManager::get().find(lua_tostring(L, 1));
    if (m)
        lua_pushboolean(L, m->enabled());
    else
        lua_pushnil(L);
    return 1;
}

static int l_modules_list(lua_State* L)
{
    auto& all = ModuleManager::get().all();
    lua_createtable(L, (int)all.size(), 0);
    int i = 1;
    for (auto& m : all)
    {
        lua_createtable(L, 0, 3);
        pushStr(L, "name", m->name().c_str());
        pushBool(L, "enabled", m->enabled());
        pushStr(L, "category", categoryName(m->category()));
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

static int l_ui_notify(lua_State* L)
{
    const char* s = lua_tostring(L, 1);
    if (s && s[0])
        ModuleManager::get().notifyInfo(s);
    return 0;
}

static int l_draw_text(lua_State* L)
{
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isstring(L, 3))
        return 0;
    float x = (float)lua_tonumber(L, 1);
    float y = (float)lua_tonumber(L, 2);
    ImU32 col = optColor(L, 4, IM_COL32(240, 242, 245, 255));
    ImGui::GetForegroundDrawList()->AddText(ImVec2(x, y), col, lua_tostring(L, 3));
    return 0;
}

static int l_draw_rect(lua_State* L)
{
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4))
        return 0;
    float x1 = (float)lua_tonumber(L, 1);
    float y1 = (float)lua_tonumber(L, 2);
    float x2 = (float)lua_tonumber(L, 3);
    float y2 = (float)lua_tonumber(L, 4);
    bool filled = true;
    int colorStart = 5;
    if (lua_isboolean(L, 5))
    {
        filled = lua_toboolean(L, 5);
        colorStart = 6;
    }
    ImU32 col = optColor(L, colorStart, IM_COL32(255, 255, 255, 70));
    if (filled)
        ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), col, 2.0f);
    else
        ImGui::GetForegroundDrawList()->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), col, 2.0f, 0, 1.0f);
    return 0;
}

static int l_draw_line(lua_State* L)
{
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4))
        return 0;
    float x1 = (float)lua_tonumber(L, 1);
    float y1 = (float)lua_tonumber(L, 2);
    float x2 = (float)lua_tonumber(L, 3);
    float y2 = (float)lua_tonumber(L, 4);
    ImU32 col = optColor(L, 5, IM_COL32(240, 242, 245, 255));
    ImGui::GetForegroundDrawList()->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), col, 1.5f);
    return 0;
}

static int l_draw_width(lua_State* L)
{
    lua_pushnumber(L, ImGui::GetIO().DisplaySize.x);
    return 1;
}

static int l_draw_height(lua_State* L)
{
    lua_pushnumber(L, ImGui::GetIO().DisplaySize.y);
    return 1;
}

static int l_draw_circle(lua_State* L)
{
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3))
        return 0;
    float x = (float)lua_tonumber(L, 1);
    float y = (float)lua_tonumber(L, 2);
    float r = (float)lua_tonumber(L, 3);
    ImU32 col = optColor(L, 4, IM_COL32(240, 242, 245, 255));
    int seg = 24;
    if (lua_isnumber(L, 8))
        seg = (int)lua_tonumber(L, 8);
    if (seg < 3)
        seg = 3;
    ImGui::GetForegroundDrawList()->AddCircle(ImVec2(x, y), r, col, seg, 1.5f);
    return 0;
}

static int l_draw_filledCircle(lua_State* L)
{
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3))
        return 0;
    float x = (float)lua_tonumber(L, 1);
    float y = (float)lua_tonumber(L, 2);
    float r = (float)lua_tonumber(L, 3);
    ImU32 col = optColor(L, 4, IM_COL32(76, 201, 255, 200));
    int seg = 24;
    if (lua_isnumber(L, 8))
        seg = (int)lua_tonumber(L, 8);
    if (seg < 3)
        seg = 3;
    ImGui::GetForegroundDrawList()->AddCircleFilled(ImVec2(x, y), r, col, seg);
    return 0;
}

static int l_draw_textSize(lua_State* L)
{
    if (!lua_isstring(L, 1))
    {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        return 2;
    }
    ImVec2 ts = ImGui::CalcTextSize(lua_tostring(L, 1));
    lua_pushnumber(L, ts.x);
    lua_pushnumber(L, ts.y);
    return 2;
}

// ---- input ----

int parseKeyCode(lua_State* L, int idx)
{
    if (lua_type(L, idx) == LUA_TNUMBER)
        return (int)lua_tonumber(L, idx);
    const char* s = lua_tostring(L, idx);
    if (!s || !s[0])
        return 0;
    return input::keyCodeFromName(s);
}

static int l_input_isDown(lua_State* L)
{
    int vk = parseKeyCode(L, 1);
    lua_pushboolean(L, vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0);
    return 1;
}

static int l_input_mouseDown(lua_State* L)
{
    int btn = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : 0;
    int vk = VK_LBUTTON;
    if (btn == 1)
        vk = VK_RBUTTON;
    else if (btn == 2)
        vk = VK_MBUTTON;
    lua_pushboolean(L, (GetAsyncKeyState(vk) & 0x8000) != 0);
    return 1;
}

static int l_input_mouseX(lua_State* L)
{
    lua_pushnumber(L, ImGui::GetIO().MousePos.x);
    return 1;
}

static int l_input_mouseY(lua_State* L)
{
    lua_pushnumber(L, ImGui::GetIO().MousePos.y);
    return 1;
}

// ---- friends ----

static int l_friends_isFriend(lua_State* L)
{
    const char* n = lua_tostring(L, 1);
    lua_pushboolean(L, n && friends::isFriend(n));
    return 1;
}

static int l_friends_toggle(lua_State* L)
{
    const char* n = lua_tostring(L, 1);
    if (!n || !n[0])
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, friends::toggle(n));
    return 1;
}

static int l_friends_list(lua_State* L)
{
    auto& set = friends::list();
    lua_createtable(L, (int)set.size(), 0);
    int i = 1;
    for (const auto& n : set)
    {
        lua_pushstring(L, n.c_str());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// ---- chat ----

static int l_chat_display(lua_State* L)
{
    const char* s = lua_tostring(L, 1);
    bool ok = false;
    if (s && s[0])
        ok = chathook::display(s);
    lua_pushboolean(L, ok);
    return 1;
}

void makeGroup(lua_State* L, const char* gname,
               std::initializer_list<std::pair<const char*, lua_CFunction>> fns)
{
    lua_createtable(L, 0, (int)fns.size());
    for (const auto& p : fns)
    {
        lua_pushcfunction(L, p.second, p.first);
        lua_rawsetfield(L, -2, p.first);
    }
    lua_setglobal(L, gname);
}

void installBindings(lua_State* L)
{
    makeGroup(L, "game",
             {{"localPlayer", l_game_localPlayer},
              {"players", l_game_players},
              {"setOnGround", l_game_setOnGround},
              {"setVelocity", l_game_setVelocity},
              {"setVelocityXZ", l_game_setVelocityXZ},
              {"setPos", l_game_setPos},
              {"setYaw", l_game_setYaw},
              {"setPitch", l_game_setPitch},
              {"hotbar", l_game_hotbar},
              {"setHotbarSlot", l_game_setHotbarSlot},
              {"camera", l_game_camera},
              {"worldToScreen", l_game_worldToScreen},
              {"click", l_game_click}});
    makeGroup(L, "modules",
              {{"toggle", l_modules_toggle}, {"enabled", l_modules_enabled}, {"list", l_modules_list}});
    makeGroup(L, "ui", {{"notify", l_ui_notify}});
    makeGroup(L, "input",
             {{"isDown", l_input_isDown},
              {"mouseDown", l_input_mouseDown},
              {"mouseX", l_input_mouseX},
              {"mouseY", l_input_mouseY}});
    makeGroup(L, "friends",
              {{"isFriend", l_friends_isFriend},
               {"toggle", l_friends_toggle},
               {"list", l_friends_list}});
    makeGroup(L, "chat", {{"display", l_chat_display}});
    makeGroup(L, "draw",
             {{"text", l_draw_text},
              {"rect", l_draw_rect},
              {"line", l_draw_line},
              {"width", l_draw_width},
              {"height", l_draw_height},
              {"circle", l_draw_circle},
              {"filledCircle", l_draw_filledCircle},
              {"textSize", l_draw_textSize}});
}

// ======================================================================
// Carga de archivos
// ======================================================================

std::string strField(lua_State* L, int idx, const char* k, const char* def)
{
    lua_getfield(L, idx, k);
    std::string out = def ? def : "";
    if (lua_type(L, -1) == LUA_TSTRING)
    {
        const char* s = lua_tostring(L, -1);
        if (s)
            out = s;
    }
    lua_pop(L, 1);
    return out;
}

void parseSettingsDefs(lua_State* L, int modIdx, std::vector<SettingDecl>& defs)
{
    lua_getfield(L, modIdx, "settings");
    if (lua_type(L, -1) != LUA_TTABLE)
    {
        lua_pop(L, 1);
        return;
    }

    for (int i = 1;; ++i)
    {
        lua_pushinteger(L, i);
        lua_rawget(L, -2);
        if (lua_type(L, -1) == LUA_TNIL)
        {
            lua_pop(L, 1);
            break;
        }
        if (lua_type(L, -1) == LUA_TTABLE)
        {
            SettingDecl d;
            d.id = strField(L, -1, "id", ("s" + std::to_string(i)).c_str());
            d.label = strField(L, -1, "label", d.id.c_str());
            std::string type = strField(L, -1, "type", "slider");
            d.isCombo = (type == "combo" || type == "list");
            d.isFloat = !d.isCombo && (type != "checkbox" && type != "bool" && type != "boolean");

            if (d.isCombo)
            {
                lua_getfield(L, -1, "options");
                if (lua_type(L, -1) == LUA_TTABLE)
                {
                    for (int oi = 1;; ++oi)
                    {
                        lua_pushinteger(L, oi);
                        lua_rawget(L, -2);
                        if (lua_type(L, -1) == LUA_TNIL)
                        {
                            lua_pop(L, 1);
                            break;
                        }
                        const char* os = lua_tostring(L, -1);
                        if (os)
                            d.options.push_back(os);
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
                if (d.options.empty())
                {
                    d.isCombo = false;
                    d.isFloat = true;
                }
            }

            lua_getfield(L, -1, "min");
            if (lua_isnumber(L, -1))
                d.minv = lua_tonumber(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "max");
            if (lua_isnumber(L, -1))
                d.maxv = lua_tonumber(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "value");
            if (lua_isnumber(L, -1))
                d.fVal = lua_tonumber(L, -1);
            else if (lua_isboolean(L, -1))
                d.bVal = lua_toboolean(L, -1);
            lua_pop(L, 1);

            if (d.isFloat && d.maxv <= d.minv)
                d.maxv = d.minv + 1.0;
            if (d.fVal < d.minv)
                d.fVal = d.minv;
            if (d.fVal > d.maxv)
                d.fVal = d.maxv;
            if (d.isCombo)
            {
                if (d.fVal < 1.0)
                    d.fVal = 1.0;
                if (d.fVal > (double)d.options.size())
                    d.fVal = 1.0;
            }
            defs.push_back(std::move(d));
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

std::string fileBaseName(const std::string& path)
{
    size_t slash = path.find_last_of("\\/");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);
    return name;
}

bool loadFileToState(const std::string& path, lua_State*& outL, ScriptMeta& meta, std::string& err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        err = "cannot open file";
        return false;
    }
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (src.empty())
    {
        err = "empty file";
        return false;
    }

    lua_State* L = luaL_newstate();
    if (!L)
    {
        err = "cannot create VM";
        return false;
    }
    luaL_openlibs(L);

    // Sandbox: sin debug; os en Luau solo expone time/clock/date (lectura).
    lua_pushnil(L);
    lua_setglobal(L, "debug");

    installBindings(L);

    std::string bc;
    try
    {
        bc = Luau::compile(src);
    }
    catch (const Luau::CompileError& e)
    {
        err = e.what();
        lua_close(L);
        return false;
    }

    std::string chunk = fileBaseName(path);
    if (luau_load(L, chunk.c_str(), bc.data(), bc.size(), 0) != 0)
    {
        const char* e = lua_tostring(L, -1);
        err = std::string("load: ") + (e ? e : "?");
        lua_pop(L, 1);
        lua_close(L);
        return false;
    }

    if (lua_pcall(L, 0, 1, 0) != 0)
    {
        const char* e = lua_tostring(L, -1);
        err = e ? e : "run error";
        lua_pop(L, 1);
        lua_close(L);
        return false;
    }

    if (lua_type(L, -1) != LUA_TTABLE)
    {
        err = "script must return a module table";
        lua_pop(L, 1);
        lua_close(L);
        return false;
    }

    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegKey);

    meta.name = strField(L, -1, "name", chunk.c_str());
    meta.description = strField(L, -1, "description", "");
    // Los scripts viven siempre en su propia pestaña; el campo "category"
    // del script se ignora.
    meta.category = Category::Scripting;

    meta.keybind = 0;
    lua_getfield(L, -1, "keybind");
    if (lua_type(L, -1) == LUA_TSTRING)
    {
        const char* s = lua_tostring(L, -1);
        if (s && s[0])
            meta.keybind = (int)(unsigned char)std::toupper((unsigned char)s[0]);
    }
    else if (lua_isnumber(L, -1))
    {
        meta.keybind = (int)lua_tonumber(L, -1);
    }
    lua_pop(L, 1);

    parseSettingsDefs(L, -1, meta.defs);

    lua_pop(L, 1);
    outL = L;
    return true;
}

// ======================================================================
// ScriptModule: adaptador que convierte un script en un modulo nativo
// ======================================================================

class ScriptModule : public Module
{
public:
    ScriptModule(const std::string& file, lua_State* L, std::vector<SettingDecl>&& defs,
                 const std::string& name, const std::string& desc, Category cat, int keybind)
        : Module(name, desc, cat, keybind), file_(file), L_(L), defs_(std::move(defs))
    {
        if (!defs_.empty())
            markHasSettings();
    }

    ~ScriptModule() override
    {
        closeVm();
    }

    void onEnable() override { callField("onEnable"); }
    void onDisable() override { callField("onDisable"); }
    void onTick() override { callField("onTick"); }
    void onRender() override { callField("onRender"); }
    void onCommand(const std::string& cmd) override { callField("onCommand", cmd.c_str()); }
    void onShutdown() override { closeVm(); }

    void drawSettings() override
    {
        for (auto& d : defs_)
        {
            std::string lbl = d.label + "##" + d.id;
            if (d.isCombo)
            {
                int sel = (int)d.fVal;
                if (sel < 1)
                    sel = 1;
                if (sel > (int)d.options.size())
                    sel = 1;
                if (widgets::CCombo(lbl.c_str(), &sel, d.options))
                    d.fVal = sel;
            }
            else if (d.isFloat)
            {
                float v = (float)d.fVal;
                if (widgets::CSlider(lbl.c_str(), &v, (float)d.minv, (float)d.maxv, "%.2f"))
                    d.fVal = v;
            }
            else
            {
                ImGui::Checkbox(lbl.c_str(), &d.bVal);
            }
        }
    }

    nlohmann::json saveSettings() const override
    {
        nlohmann::json j = nlohmann::json::object();
        for (const auto& d : defs_)
        {
            if (d.isFloat)
                j[d.id] = d.fVal;
            else
                j[d.id] = d.bVal;
        }
        return j;
    }

    void loadSettings(const nlohmann::json& j) override
    {
        for (auto& d : defs_)
        {
            if (!j.contains(d.id))
                continue;
            try
            {
                if (d.isFloat)
                    d.fVal = j[d.id].get<double>();
                else
                    d.bVal = j[d.id].get<bool>();
            }
            catch (...)
            {
            }
        }
    }

    const std::string& file() const { return file_; }
    bool errored() const { return errored_; }
    const std::string& lastError() const { return lastError_; }

    void closeVm()
    {
        if (L_)
        {
            lua_close(L_);
            L_ = nullptr;
        }
    }

    void swapState(lua_State* newL, std::vector<SettingDecl>&& newDefs)
    {
        for (auto& nd : newDefs)
        {
            for (const auto& od : defs_)
            {
                if (od.id == nd.id && od.isFloat == nd.isFloat && od.isCombo == nd.isCombo)
                {
                    nd.fVal = od.fVal;
                    nd.bVal = od.bVal;
                    break;
                }
            }
        }
        closeVm();
        L_ = newL;
        defs_ = std::move(newDefs);
        errored_ = false;
        lastError_.clear();
    }

private:
    void pushSettingsGlobal()
    {
        if (!L_)
            return;
        lua_createtable(L_, 0, (int)defs_.size());
        for (const auto& d : defs_)
        {
            if (d.isFloat || d.isCombo)
                lua_pushnumber(L_, d.fVal);
            else
                lua_pushboolean(L_, d.bVal);
            lua_rawsetfield(L_, -2, d.id.c_str());
        }
        lua_setglobal(L_, "settings");
    }

    void callField(const char* field, const char* strArg = nullptr)
    {
        if (!L_ || errored_)
            return;

        lua_getfield(L_, LUA_REGISTRYINDEX, kRegKey);
        if (lua_type(L_, -1) != LUA_TTABLE)
        {
            lua_pop(L_, 1);
            return;
        }
        lua_getfield(L_, -1, field);
        if (lua_type(L_, -1) != LUA_TFUNCTION)
        {
            lua_pop(L_, 2);
            return;
        }

        pushSettingsGlobal();
        int nargs = 0;
        if (strArg)
        {
            lua_pushstring(L_, strArg);
            nargs = 1;
        }

        if (lua_pcall(L_, nargs, 0, 0) != 0)
        {
            const char* e = lua_tostring(L_, -1);
            lastError_ = e ? e : "unknown error";
            lua_pop(L_, 2);
            if (!errored_)
            {
                errored_ = true;
                MC_LOG_ERROR("[Script] %s (%s): %s", name().c_str(), file_.c_str(),
                             lastError_.c_str());
                ModuleManager::get().notifyInfo(name() + " script error");
            }
            setEnabled(false);
            return;
        }
        lua_pop(L_, 1);
    }

    std::string file_;
    lua_State* L_ = nullptr;
    std::vector<SettingDecl> defs_;
    std::string lastError_;
    bool errored_ = false;
};

// ======================================================================
// Manager
// ======================================================================

std::vector<ScriptModule*> g_modules;
std::vector<std::string> g_files;
std::string g_lastLoadError;

std::string scriptsDirPath()
{
    std::string base = config::baseDir();
    if (base.empty())
        return {};
    std::string dir = base + "\\Scripts";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

void loadOne(const std::string& path, const std::string& fname)
{
    if (std::find(g_files.begin(), g_files.end(), path) != g_files.end())
        return;

    lua_State* L = nullptr;
    ScriptMeta meta;
    std::string err;
    if (!loadFileToState(path, L, meta, err))
    {
        MC_LOG_ERROR("[Script] %s: %s", fname.c_str(), err.c_str());
        g_lastLoadError = fname + ": " + err;
        return;
    }

    auto mod = std::make_unique<ScriptModule>(path, L, std::move(meta.defs), meta.name,
                                               meta.description, meta.category, meta.keybind);
    ScriptModule* raw = mod.get();
    ModuleManager::get().add(std::move(mod));
    g_modules.push_back(raw);
    g_files.push_back(path);
    MC_LOG("[Script] Loaded: %s (%s)", meta.name.c_str(), fname.c_str());
}

} // namespace

void loadAll()
{
    std::string dir = scriptsDirPath();
    if (dir.empty())
    {
        MC_LOG("[Script] Scripts directory unavailable");
        return;
    }

    std::string pattern = dir + "\\*.lua";
    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        MC_LOG("[Script] No .lua files in %s", dir.c_str());
        return;
    }
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        std::string fname = fd.cFileName;
        loadOne(dir + "\\" + fname, fname);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

void reload()
{
    for (ScriptModule* m : g_modules)
    {
        lua_State* L = nullptr;
        ScriptMeta meta;
        std::string err;
        if (!loadFileToState(m->file(), L, meta, err))
        {
            MC_LOG_ERROR("[Script] Reload failed for %s: %s", m->file().c_str(), err.c_str());
            g_lastLoadError = err;
            continue;
        }
        m->swapState(L, std::move(meta.defs));
        MC_LOG("[Script] Reloaded: %s", m->name().c_str());
    }
    loadAll();
}

std::vector<ScriptInfo> list()
{
    std::vector<ScriptInfo> out;
    for (ScriptModule* m : g_modules)
        out.push_back({m->name(), m->file(), m->errored()});
    return out;
}

bool remove(const std::string& name)
{
    for (size_t i = 0; i < g_modules.size(); ++i)
    {
        ScriptModule* m = g_modules[i];
        bool byName = (m->name() == name);
        bool byFile = (m->file() == name) || (fileBaseName(m->file()) == name);
        if (!byName && !byFile)
            continue;

        std::string path = m->file();
        if (!ModuleManager::get().remove(m->name()))
            return false;

        g_modules.erase(g_modules.begin() + i);
        auto it = std::find(g_files.begin(), g_files.end(), path);
        if (it != g_files.end())
            g_files.erase(it);

        if (DeleteFileA(path.c_str()))
        {
            MC_LOG("[Script] Removed: %s (%s)", m->name().c_str(), path.c_str());
        }
        else
        {
            MC_LOG_ERROR("[Script] Removed module %s but file delete failed: %s",
                         m->name().c_str(), path.c_str());
        }
        return true;
    }
    return false;
}

void shutdown()
{
    for (ScriptModule* m : g_modules)
        m->closeVm();
    g_modules.clear();
    g_files.clear();
}

int count()
{
    return (int)g_modules.size();
}

int errorCount()
{
    int n = 0;
    for (ScriptModule* m : g_modules)
        if (m->errored())
            ++n;
    if (!g_lastLoadError.empty())
        ++n;
    return n;
}

const char* lastError()
{
    for (ScriptModule* m : g_modules)
        if (m->errored())
            return m->lastError().c_str();
    if (!g_lastLoadError.empty())
        return g_lastLoadError.c_str();
    return "";
}

}
