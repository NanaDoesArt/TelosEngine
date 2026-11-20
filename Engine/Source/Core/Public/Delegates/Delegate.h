#pragma once
#include "Containers/Array.h"
#include "Templates/SmartPointer.h"
#include <EASTL/functional.h>
#include <cstdint>
#include <type_traits>

struct FDelegateHandle
{
    uint64_t ID = 0;

    bool IsValid() const { return ID != 0; }
    void Reset() { ID = 0; }

    bool operator==(const FDelegateHandle& Other) const { return ID == Other.ID; }
    bool operator!=(const FDelegateHandle& Other) const { return ID != Other.ID; }

    static FDelegateHandle Generate()
    {
        static uint64_t NextID = 1;
        return FDelegateHandle{ NextID++ };
    }
};

// ==============================================================================
// Single-Cast Delegate: TDelegate<Ret(Args...)>
// ==============================================================================
template <typename Signature>
class TDelegate;

template <typename RetType, typename... ParamTypes>
class TDelegate<RetType(ParamTypes...)>
{
public:
    TDelegate() = default;

    template <typename Functor>
    void BindLambda(Functor&& InFunctor)
    {
        Callback = eastl::forward<Functor>(InFunctor);
    }

    void BindStatic(RetType (*InFunc)(ParamTypes...))
    {
        Callback = InFunc;
    }

    template <typename UserClass>
    void BindRaw(UserClass* InObject, RetType (UserClass::*InMethod)(ParamTypes...))
    {
        Callback = [InObject, InMethod](ParamTypes... Args) -> RetType
        {
            return (InObject->*InMethod)(eastl::forward<ParamTypes>(Args)...);
        };
    }

    template <typename UserClass>
    void BindSP(const TSharedPtr<UserClass>& InObject, RetType (UserClass::*InMethod)(ParamTypes...))
    {
        TWeakPtr<UserClass> WeakObj = InObject;
        Callback = [WeakObj, InMethod](ParamTypes... Args) -> RetType
        {
            if (TSharedPtr<UserClass> Pinned = WeakObj.Pin())
            {
                return (Pinned.Get()->*InMethod)(eastl::forward<ParamTypes>(Args)...);
            }
            if constexpr (!std::is_void_v<RetType>)
            {
                return RetType{};
            }
        };
    }

    bool IsBound() const { return static_cast<bool>(Callback); }
    void Unbind() { Callback = nullptr; }

    RetType Execute(ParamTypes... Args) const
    {
        return Callback(eastl::forward<ParamTypes>(Args)...);
    }

    RetType ExecuteIfBound(ParamTypes... Args) const
    {
        if (IsBound())
        {
            return Callback(eastl::forward<ParamTypes>(Args)...);
        }
        if constexpr (!std::is_void_v<RetType>)
        {
            return RetType{};
        }
    }

private:
    eastl::function<RetType(ParamTypes...)> Callback;
};

// ==============================================================================
// Multi-Cast Delegate: TMulticastDelegate<Args...>
// ==============================================================================
template <typename... ParamTypes>
class TMulticastDelegate
{
public:
    using FDelegate = TDelegate<void(ParamTypes...)>;

    FDelegateHandle Add(FDelegate&& InDelegate)
    {
        FDelegateHandle Handle = FDelegateHandle::Generate();
        Bindings.Add({ Handle, eastl::move(InDelegate) });
        return Handle;
    }

    template <typename Functor>
    FDelegateHandle AddLambda(Functor&& InFunctor)
    {
        FDelegate Del;
        Del.BindLambda(eastl::forward<Functor>(InFunctor));
        return Add(eastl::move(Del));
    }

    template <typename UserClass>
    FDelegateHandle AddRaw(UserClass* InObject, void (UserClass::*InMethod)(ParamTypes...))
    {
        FDelegate Del;
        Del.BindRaw(InObject, InMethod);
        return Add(eastl::move(Del));
    }

    template <typename UserClass>
    FDelegateHandle AddSP(const TSharedPtr<UserClass>& InObject, void (UserClass::*InMethod)(ParamTypes...))
    {
        FDelegate Del;
        Del.BindSP(InObject, InMethod);
        return Add(eastl::move(Del));
    }

    bool Remove(FDelegateHandle& Handle)
    {
        if (!Handle.IsValid())
        {
            return false;
        }

        for (int32_t i = 0; i < Bindings.Num(); ++i)
        {
            if (Bindings[i].Handle == Handle)
            {
                Bindings.RemoveAtSwap(i);
                Handle.Reset();
                return true;
            }
        }
        return false;
    }

    void Clear() { Bindings.Empty(); }
    bool IsBound() const { return !Bindings.IsEmpty(); }

    void Broadcast(ParamTypes... Args) const
    {
        // Iterate over a snapshot copy so listeners can safely unbind themselves during Broadcast
        TArray<FBindingEntry> Snapshot = Bindings;
        for (const FBindingEntry& Entry : Snapshot)
        {
            Entry.Delegate.ExecuteIfBound(Args...);
        }
    }

private:
    struct FBindingEntry
    {
        FDelegateHandle Handle;
        FDelegate Delegate;
    };

    TArray<FBindingEntry> Bindings;
};

template <typename OwningType, typename... ParamTypes>
class TEvent : public TMulticastDelegate<ParamTypes...>
{
    friend OwningType;

private:
    // Inherit Broadcast() as PRIVATE so only OwningType can trigger the event!
    using TMulticastDelegate<ParamTypes...>::Broadcast;
};

#define DECLARE_DELEGATE(DelegateName) using DelegateName = TDelegate<void()>;
#define DECLARE_DELEGATE_OneParam(DelegateName, Param1) using DelegateName = TDelegate<void(Param1)>;
#define DECLARE_DELEGATE_TwoParams(DelegateName, Param1, Param2) using DelegateName = TDelegate<void(Param1, Param2)>;

#define DECLARE_MULTICAST_DELEGATE(DelegateName) using DelegateName = TMulticastDelegate<>;
#define DECLARE_MULTICAST_DELEGATE_OneParam(DelegateName, Param1) using DelegateName = TMulticastDelegate<Param1>;
#define DECLARE_MULTICAST_DELEGATE_TwoParams(DelegateName, Param1, Param2) using DelegateName = TMulticastDelegate<Param1, Param2>;

#define DECLARE_EVENT(OwningType, EventName) using EventName = TEvent<OwningType>;
#define DECLARE_EVENT_OneParam(OwningType, EventName, Param1) using EventName = TEvent<OwningType, Param1>;
#define DECLARE_EVENT_TwoParams(OwningType, EventName, Param1, Param2) using EventName = TEvent<OwningType, Param1, Param2>;

