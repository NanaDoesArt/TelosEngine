#pragma once
#include <mutex>
#include <atomic>
#include <cstdint>

class FMutex
{
public:
    FMutex() = default;
    FMutex(const FMutex&) = delete;
    FMutex& operator=(const FMutex&) = delete;

    void Lock() { InternalMutex.lock(); }
    bool TryLock() { return InternalMutex.try_lock(); }
    void Unlock() { InternalMutex.unlock(); }

    std::mutex& GetInternal() { return InternalMutex; }

private:
    std::mutex InternalMutex;
};

// Automatically locks on construction and unlocks when leaving scope { }
class FScopeLock
{
public:
    explicit FScopeLock(FMutex& InMutex)
        : SynchObject(InMutex)
    {
        SynchObject.Lock();
    }

    ~FScopeLock()
    {
        SynchObject.Unlock();
    }

    FScopeLock(const FScopeLock&) = delete;
    FScopeLock& operator=(const FScopeLock&) = delete;

private:
    FMutex& SynchObject;
};

template <typename T>
using TAtomic = std::atomic<T>;
