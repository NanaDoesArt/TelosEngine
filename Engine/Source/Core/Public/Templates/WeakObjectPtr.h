#pragma once
#include "Object/Object.h"
#include <cstdint>
#include "Object/ObjectRegistry.h"

template <typename T>
class TWeakObjectPtr
{
    static_assert(std::is_base_of_v<RObject, T>, "TWeakObjectPtr<T> requires T to inherit from RObject!");

public:
    TWeakObjectPtr() = default;
    TWeakObjectPtr(std::nullptr_t) : ObjectUniqueID(0) {}
    TWeakObjectPtr(const T* InObject)
    {
        ObjectUniqueID = InObject ? InObject->GetUniqueID() : 0;
    }

    TWeakObjectPtr& operator=(const T* InObject)
    {
        ObjectUniqueID = InObject ? InObject->GetUniqueID() : 0;
        return *this;
    }

    T* Get() const
    {
        if (ObjectUniqueID == 0)
        {
            return nullptr;
        }
        RObject* Found = FObjectRegistry::Get().FindInstance(ObjectUniqueID);
        return Cast<T>(Found);
    }

    bool IsValid() const { return Get() != nullptr; }
    explicit operator bool() const { return IsValid(); }
    void Reset() { ObjectUniqueID = 0; }

    T* operator->() const { return Get(); }
    T& operator*() const { return *Get(); }

    bool operator==(const TWeakObjectPtr& Other) const { return ObjectUniqueID == Other.ObjectUniqueID; }
    bool operator!=(const TWeakObjectPtr& Other) const { return ObjectUniqueID != Other.ObjectUniqueID; }

private:
    uint64_t ObjectUniqueID = 0;
};
