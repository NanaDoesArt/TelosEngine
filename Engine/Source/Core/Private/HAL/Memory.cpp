#include "HAL/Memory.h"
#include <mimalloc.h>
#include <cstring>

void* FMemory::Malloc(size_t Count, uint32_t Alignment)
{
    return Alignment > 0 ? mi_malloc_aligned(Count, Alignment) : mi_malloc(Count);
}

void* FMemory::Realloc(void* Original, size_t Count, uint32_t Alignment)
{
    return Alignment > 0 ? mi_realloc_aligned(Original, Count, Alignment) : mi_realloc(Original, Count);
}

void FMemory::Free(void* Original)
{
    mi_free(Original);
}

void FMemory::Memzero(void* Dest, size_t Count)
{
    std::memset(Dest, 0, Count);
}

void FMemory::Memcpy(void* Dest, const void* Src, size_t Count)
{
    std::memcpy(Dest, Src, Count);
}