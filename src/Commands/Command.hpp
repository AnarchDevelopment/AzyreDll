#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace mc {

class Command
{
public:
    Command(std::string name, std::string usage, std::string description)
        : name_(std::move(name))
        , usage_(std::move(usage))
        , description_(std::move(description))
    {
    }
    virtual ~Command() = default;

    virtual void execute(const std::vector<std::string>& args) = 0;

    const std::string& name() const { return name_; }
    const std::string& usage() const { return usage_; }
    const std::string& description() const { return description_; }

    bool matches(const std::string& token) const
    {
        if (equalsLower(name_, token))
            return true;
        for (const auto& a : aliases_)
        {
            if (equalsLower(a, token))
                return true;
        }
        return false;
    }

protected:
    void addAlias(std::string alias) { aliases_.push_back(std::move(alias)); }

private:
    static bool equalsLower(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
                return false;
        }
        return true;
    }

    std::string name_;
    std::string usage_;
    std::string description_;
    std::vector<std::string> aliases_;
};

}

#include "Commands/CommandManager.hpp"
