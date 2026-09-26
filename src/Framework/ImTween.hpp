// ImTween - vendored con fix: el miembro tweens es por VALOR (el original
// usaba una referencia rvalue que colgaba tras la factory) y el constructor
// es publico.
#pragma once
#include <vector>
#include <tuple>
#include <type_traits>
#include <optional>
#include <functional>

template <typename T = float>
class ImTween
{
public:
    enum TweenType
    {
        PingPong
    };

    ImTween(std::vector<std::tuple<T, T, T*>>&& tweens)
        : tweens(std::move(tweens))
    {
    }

    /*
     * accepts unlimited number of values in form of std::tuple<T, T, T*> { min, max, value }
     */
    template <typename U = float, typename... Values>
    constexpr static auto Tween(Values... values) -> ImTween
    {
        static_assert(sizeof...(values) > 0, "No values to animate");
        static_assert((std::is_same_v<std::tuple<U, U, U*>, Values> && ...), "Values must be of type std::tuple<T, T, T*>");

        std::vector<std::tuple<U, U, U*>> tweens;

        (tweens.push_back(values), ...);

        return ImTween<U>(std::move(tweens));
    }

    auto OfType(TweenType&& type) -> ImTween&
    {
        this->type = type;

        return *this;
    }

    auto Speed(float&& speed) -> ImTween&
    {
        this->speed = speed;

        return *this;
    }

    auto When(std::function<bool()>&& condition) -> ImTween&
    {
        this->condition = condition;

        return *this;
    }

    auto OnComplete(std::function<void()>&& onComplete) -> ImTween&
    {
        this->onComplete = onComplete;

        return *this;
    }

    auto Tick()
    {
        for (auto&& tween : tweens)
        {
            auto&& min = std::get<0>(tween);
            auto&& max = std::get<1>(tween);
            auto&& value = *std::get<2>(tween);

            if (this->condition && this->condition() && value < max)
            {
                value += speed;

                if (value > max)
                    value = max;
            }
            else if ((this->type == PingPong) && (!this->condition || !this->condition()) && value > min)
            {
                value -= speed;

                if (value < min)
                    value = min;
            }

            if (this->onComplete.has_value() && value == max)
                this->onComplete.value()();
        }
    }

private:
    TweenType type;
    float speed = 0.5f;
    std::optional<std::function<void()>> onComplete;
    std::function<bool()> condition;

    std::vector<std::tuple<T, T, T*>> tweens = {};
};
