#pragma once
#include "CoreAPI.h"
#include "HAL/ThreadSafe.h"
#include "Templates/SmartPointer.h"
#include "Delegates/Delegate.h"
#include <cstdint>
#include <thread>

DECLARE_DELEGATE(FAsyncTaskDelegate)

class Core_API FThreadPool
{
    public:
    enum class EPoolState : uint8_t
    {
        Stopped,
        Running,
        Stopping
    };

    static FThreadPool& Get();

    // Must be called from the owning (Main/Launch) thread
    void Init(uint32_t NumThreads = 0);

    // Transitions Running -> Stopping, drains all workers outside the lock, then sets Stopped
    void Shutdown();

    // Returns false unless State == Running
    bool Submit(FAsyncTaskDelegate Task);

    template <typename Functor>
    bool SubmitLambda(Functor&& InFunctor)
    {
        FAsyncTaskDelegate Del;
        Del.BindLambda(eastl::forward<Functor>(InFunctor));
        return Submit(eastl::move(Del));
    }

    // Prohibited from pool worker threads to prevent self-deadlock
    void WaitForAll();
    uint32_t GetThreadCount() const;

    private:
    FThreadPool() = default;
    ~FThreadPool();

    struct FImpl;
    mutable FMutex PoolMutex;
    TSharedPtr<FImpl> Impl;
    EPoolState State = EPoolState::Stopped;
    std::thread::id OwningThreadID;
};