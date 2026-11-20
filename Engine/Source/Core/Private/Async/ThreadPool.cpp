#include "Async/ThreadPool.h"
#include "Logging/Log.h"
#include <BS_thread_pool.hpp>
#include <algorithm>

namespace
{
    thread_local bool GIsPoolWorkerThread = false;
}

struct FThreadPool::FImpl
{
    BS::thread_pool Pool;

    explicit FImpl(uint32_t ThreadCount)
        : Pool(ThreadCount, []() { GIsPoolWorkerThread = true; })
    {
    }
};

FThreadPool& FThreadPool::Get()
{
    static FThreadPool Instance;
    return Instance;
}

FThreadPool::~FThreadPool()
{
    Shutdown();
}

void FThreadPool::Init(uint32_t NumThreads)
{
    FScopeLock Lock(PoolMutex);
    if (State != EPoolState::Stopped)
    {
        VENGINE_LOG(LogCore, Warning, "[ThreadPool] Init() ignored because pool is not in Stopped state.");
        return;
    }

    OwningThreadID = std::this_thread::get_id();

    if (NumThreads == 0)
    {
        const uint32_t HardwareCores = std::thread::hardware_concurrency();
        NumThreads = std::max(1u, HardwareCores > 1 ? HardwareCores - 1 : 1u);
    }

    Impl = MakeShared<FImpl>(NumThreads);
    State = EPoolState::Running;
    VENGINE_LOG(LogCore, Info, "[ThreadPool] Initialized with {} worker threads.", NumThreads);
}

void FThreadPool::Shutdown()
{
    TSharedPtr<FImpl> LocalPool;
    {
        FScopeLock Lock(PoolMutex);
        if (State == EPoolState::Stopped)
        {
            return;
        }
        if (GIsPoolWorkerThread || std::this_thread::get_id() != OwningThreadID)
        {
            VENGINE_LOG(LogCore, Fatal, "[ThreadPool] Shutdown() must only be called from the owning thread!");
        }
        if (State == EPoolState::Stopping)
        {
            return;
        }

        State = EPoolState::Stopping;
        LocalPool = Impl;
    }

    // Drain outside the mutex while keeping LocalPool alive so overlapping Init() cannot race
    if (LocalPool)
    {
        LocalPool->Pool.wait();
    }

    {
        FScopeLock Lock(PoolMutex);
        Impl.Reset();
        State = EPoolState::Stopped;
    }

    VENGINE_LOG(LogCore, Info, "[ThreadPool] Drained and shut down cleanly.");
}

bool FThreadPool::Submit(FAsyncTaskDelegate Task)
{
    FScopeLock Lock(PoolMutex);
    if (State != EPoolState::Running || !Impl)
    {
        VENGINE_LOG(LogCore, Warning, "[ThreadPool] Rejected task submission (pool is not Running).");
        return false;
    }

    Impl->Pool.detach_task([MovedTask = eastl::move(Task)]()
    {
        MovedTask.ExecuteIfBound();
    });
    return true;
}

void FThreadPool::WaitForAll()
{
    if (GIsPoolWorkerThread)
    {
        VENGINE_LOG(LogCore, Fatal, "[ThreadPool] Deadlock prevented: worker thread called WaitForAll()!");
    }

    TSharedPtr<FImpl> LocalPool;
    {
        FScopeLock Lock(PoolMutex);
        if (State == EPoolState::Running)
        {
            LocalPool = Impl;
        }
    }

    if (LocalPool)
    {
        LocalPool->Pool.wait();
    }
}

uint32_t FThreadPool::GetThreadCount() const
{
    FScopeLock Lock(PoolMutex);
    return (State == EPoolState::Running && Impl) ? static_cast<uint32_t>(Impl->Pool.get_thread_count()) : 0;
}