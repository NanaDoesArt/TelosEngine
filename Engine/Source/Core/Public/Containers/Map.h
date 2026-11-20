#pragma once
#include <EASTL/unordered_map.h>
#include <cstdint>

template <typename KeyType, typename ValueType>
class TMap
{
public:
    TMap() = default;

    void Add(const KeyType& Key, const ValueType& Value) { Data[Key] = Value; }
    void Add(const KeyType& Key, ValueType&& Value) { Data[Key] = eastl::move(Value); }

    ValueType& FindOrAdd(const KeyType& Key) { return Data[Key]; }

    ValueType* Find(const KeyType& Key)
    {
        auto It = Data.find(Key);
        return (It != Data.end()) ? &It->second : nullptr;
    }

    const ValueType* Find(const KeyType& Key) const
    {
        auto It = Data.find(Key);
        return (It != Data.end()) ? &It->second : nullptr;
    }

    bool Contains(const KeyType& Key) const { return Data.find(Key) != Data.end(); }

    bool Remove(const KeyType& Key) { return Data.erase(Key) > 0; }

    void Empty() { Data.clear(); }
    int32_t Num() const { return static_cast<int32_t>(Data.size()); }
    bool IsEmpty() const { return Data.empty(); }

    ValueType& operator[](const KeyType& Key) { return Data[Key]; }

    auto begin() { return Data.begin(); }
    auto end() { return Data.end(); }
    auto begin() const { return Data.begin(); }
    auto end() const { return Data.end(); }

private:
    eastl::unordered_map<KeyType, ValueType> Data;
};