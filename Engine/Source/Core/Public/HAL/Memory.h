#pragma once
#include "CoreAPI.h"
#include <cstddef>
#include <cstdint>


struct Core_API FMemory
{
    static void* Malloc(size_t Count, uint32_t Alignment = 0);
    static void* Realloc(void* Original, size_t Count, uint32_t Alignment = 0);
    static void Free(void* Original);
    static void Memzero(void* Dest, size_t Count);
    static void Memcpy(void* Dest, const void* Src, size_t Count);
};

inline void* operator new[](size_t size, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
    return FMemory::Malloc(size);
}

inline void* operator new[](size_t size, size_t alignment, size_t /*alignmentOffset*/, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
    return FMemory::Malloc(size, static_cast<uint32_t>(alignment));
}