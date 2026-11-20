#pragma once
#include <EASTL/string.h>
#include <EASTL/functional.h>
#include <cstdint>

class FString
{
public:
    FString() = default;
    FString(const char* InStr) : Data(InStr ? InStr : "") {}
    FString(const eastl::string& InStr) : Data(InStr) {}

    void Append(const FString& Other) { Data.append(Other.Data); }
    void Append(const char* Other)
    {
        if (Other)
        {
            Data.append(Other);
        }
    }

    int32_t Len() const { return static_cast<int32_t>(Data.length()); }
    bool IsEmpty() const { return Data.empty(); }
    void Empty() { Data.clear(); }

    bool Contains(const char* SubStr) const
    {
        return SubStr && Data.find(SubStr) != eastl::string::npos;
    }

    const char* operator*() const { return Data.c_str(); }
    const char* CStr() const { return Data.c_str(); }

    FString operator+(const FString& Other) const { return FString(Data + Other.Data); }
    FString& operator+=(const FString& Other)
    {
        Data += Other.Data;
        return *this;
    }

    bool operator==(const FString& Other) const { return Data == Other.Data; }
    bool operator!=(const FString& Other) const { return Data != Other.Data; }
    bool operator==(const char* Other) const { return Data == (Other ? Other : ""); }

    const eastl::string& GetInternal() const { return Data; }

private:
    eastl::string Data;
};

// Enables TMap<FString, T> hashing out of the box
namespace eastl
{
    template <>
    struct hash<FString>
    {
        size_t operator()(const FString& Str) const
        {
            return eastl::hash<eastl::string>()(Str.GetInternal());
        }
    };
}