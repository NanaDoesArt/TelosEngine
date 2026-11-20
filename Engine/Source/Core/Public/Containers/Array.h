#pragma once
#include <EASTL/vector.h>
#include <cstdint>
#include <initializer_list>

template <typename T>
class TArray
{
public:
    TArray() = default;
    TArray(std::initializer_list<T> InitList) : Data(InitList) {}

    void Add(const T& Item) { Data.push_back(Item); }
    void Add(T&& Item) { Data.push_back(eastl::move(Item)); }

    template <typename... Args>
    T& Emplace(Args&&... args)
    {
        return Data.emplace_back(eastl::forward<Args>(args)...);
    }

    void RemoveAt(int32_t Index)
    {
        if (IsValidIndex(Index))
        {
            Data.erase(Data.begin() + Index);
        }
    }

    void RemoveAtSwap(int32_t Index)
    {
        if (IsValidIndex(Index))
        {
            Data[Index] = eastl::move(Data.back());
            Data.pop_back();
        }
    }

    bool Remove(const T& Item)
    {
        auto It = eastl::find(Data.begin(), Data.end(), Item);
        if (It != Data.end())
        {
            Data.erase(It);
            return true;
        }
        return false;
    }

    bool Contains(const T& Item) const
    {
        return eastl::find(Data.begin(), Data.end(), Item) != Data.end();
    }

    void Reserve(int32_t Capacity) { Data.reserve(static_cast<size_t>(Capacity)); }
    void Empty(int32_t Slack = 0)
    {
        Data.clear();
        if (Slack > 0)
        {
            Data.reserve(static_cast<size_t>(Slack));
        }
    }

    int32_t Num() const { return static_cast<int32_t>(Data.size()); }
    bool IsEmpty() const { return Data.empty(); }
    bool IsValidIndex(int32_t Index) const { return Index >= 0 && Index < Num(); }

    T* GetData() { return Data.data(); }
    const T* GetData() const { return Data.data(); }

    T& operator[](int32_t Index) { return Data[static_cast<size_t>(Index)]; }
    const T& operator[](int32_t Index) const { return Data[static_cast<size_t>(Index)]; }

    // Range-based for loop support
    auto begin() { return Data.begin(); }
    auto end() { return Data.end(); }
    auto begin() const { return Data.begin(); }
    auto end() const { return Data.end(); }

private:
    eastl::vector<T> Data;
};