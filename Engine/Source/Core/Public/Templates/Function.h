#pragma once
#include <EASTL/functional.h>
#include <cstddef>
#include <utility>

template <typename Signature>
class TFunction;

template <typename RetType, typename... ParamTypes>
class TFunction<RetType(ParamTypes...)>
{
public:
    TFunction() = default;
    TFunction(std::nullptr_t) noexcept : Callable(nullptr) {}

    TFunction(const TFunction&) = default;
    TFunction& operator=(const TFunction&) = default;
    TFunction(TFunction&&) noexcept = default;
    TFunction& operator=(TFunction&&) noexcept = default;

    template <typename Functor, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Functor>, TFunction>>>
    TFunction(Functor&& InFunctor)
        : Callable(eastl::forward<Functor>(InFunctor))
    {
    }

    template <typename Functor, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Functor>, TFunction>>>
    TFunction& operator=(Functor&& InFunctor)
    {
        Callable = eastl::forward<Functor>(InFunctor);
        return *this;
    }

    TFunction& operator=(std::nullptr_t) noexcept
    {
        Callable = nullptr;
        return *this;
    }

    bool IsBound() const noexcept { return static_cast<bool>(Callable); }
    explicit operator bool() const noexcept { return IsBound(); }
    void Reset() noexcept { Callable = nullptr; }

    RetType operator()(ParamTypes... Args) const
    {
        return Callable(eastl::forward<ParamTypes>(Args)...);
    }

    bool operator==(std::nullptr_t) const noexcept { return !IsBound(); }
    bool operator!=(std::nullptr_t) const noexcept { return IsBound(); }

private:
    eastl::function<RetType(ParamTypes...)> Callable;
};