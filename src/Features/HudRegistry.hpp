#pragma once

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>

namespace mc::hud {

struct Element
{
    std::string id;
    ImVec2 pos = ImVec2(0, 0);
    ImVec2 size = ImVec2(0, 0);
    bool visible = false;
};

class Registry
{
public:
    static Registry& get()
    {
        static Registry instance;
        return instance;
    }

    void setConfigPath(const std::string& path) { configPath_ = path; }

    // El modulo llama esto cada frame para reportar su rect y recibir la pos
    ImVec2 report(const std::string& id, const ImVec2& defaultPos, const ImVec2& size)
    {
        ensureLoaded();

        // Buscar elemento existente
        Element* e = find(id);
        if (!e)
        {
            // Crear nuevo — usar posicion guardada si existe, sino el default
            Element ne;
            ne.id = id;
            ne.pos = defaultPos;

            auto it = savedPos_.find(id);
            if (it != savedPos_.end())
                ne.pos = ImVec2(it->second.first, it->second.second);

            ne.size = size;
            ne.visible = true;
            elements_.push_back(ne);
            return ne.pos;
        }

        e->size = size;
        e->visible = true;
        return e->pos;
    }

    Element* find(const std::string& id)
    {
        for (auto& e : elements_)
            if (e.id == id)
                return &e;
        return nullptr;
    }

    Element* hitTest(const ImVec2& mouse)
    {
        for (auto& e : elements_)
        {
            if (!e.visible || e.size.x <= 0)
                continue;
            if (mouse.x >= e.pos.x && mouse.x <= e.pos.x + e.size.x &&
                mouse.y >= e.pos.y && mouse.y <= e.pos.y + e.size.y)
                return &e;
        }
        return nullptr;
    }

    std::vector<Element>& all() { return elements_; }

    void save()
    {
        if (configPath_.empty())
            return;

        nlohmann::json j;
        for (auto& e : elements_)
        {
            if (e.id.empty())
                continue;
            j[e.id] = {{"x", e.pos.x}, {"y", e.pos.y}};
        }

        // Tambien guardar elementos que ya no estan visibles pero tuvieron
        // posicion guardada (para que no se pierdan al cambiar de modo)
        for (auto& [id, pos] : savedPos_)
        {
            if (!j.contains(id))
                j[id] = {{"x", pos.first}, {"y", pos.second}};
        }

        std::ofstream out(configPath_);
        if (out.is_open())
        {
            out << j.dump(4);
            out.close();
        }
    }

    void reset()
    {
        savedPos_.clear();
        for (auto& e : elements_)
        {
            e.pos = ImVec2(-1, -1); // Marcar para que report() use el default
        }
        // Forzar re-default en el proximo report()
        for (auto& e : elements_)
        {
            if (e.pos.x == -1)
                e.pos = ImVec2(0, 0); // Se sobreescriira por report()
        }
    }

private:
    void ensureLoaded()
    {
        if (loaded_)
            return;
        loaded_ = true;

        if (configPath_.empty())
            return;

        std::ifstream in(configPath_);
        if (!in.is_open())
            return;
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto j = nlohmann::json::parse(text, nullptr, false);
        if (!j.is_object())
            return;

        for (auto it = j.begin(); it != j.end(); ++it)
        {
            try
            {
                float x = it.value().value("x", 0.0f);
                float y = it.value().value("y", 0.0f);
                savedPos_[it.key()] = {x, y};
            }
            catch (...)
            {
            }
        }
    }

    std::string configPath_;
    std::vector<Element> elements_;
    std::unordered_map<std::string, std::pair<float, float>> savedPos_;
    bool loaded_ = false;
};

}
