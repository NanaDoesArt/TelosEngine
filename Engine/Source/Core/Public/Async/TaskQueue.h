#pragma once
#include "HAL/ThreadSafe.h"
#include "Containers/Array.h"
#include "Delegates/Delegate.h"

DECLARE_DELEGATE(FQueuedTask)

class FTaskQueue
{
public:
    // Called from ANY thread (Worker, Game, Render) to enqueue work safely
    void Enqueue(FQueuedTask Task)
    {
        FScopeLock Lock(QueueMutex);
        PendingTasks.Add(eastl::move(Task));
    }

    template <typename Functor>
    void EnqueueLambda(Functor&& InFunctor)
    {
        FQueuedTask Del;
        Del.BindLambda(eastl::forward<Functor>(InFunctor));
        Enqueue(eastl::move(Del));
    }

    // Called by the owning thread (e.g., Render Thread at start of frame) to execute all queued updates
    void ProcessAll()
    {
        TArray<FQueuedTask> TasksToRun;
        {
            FScopeLock Lock(QueueMutex);
            if (PendingTasks.IsEmpty())
            {
                return;
            }
            // Swap buffers quickly so the lock is held for nanoseconds!
            TasksToRun = eastl::move(PendingTasks);
            PendingTasks.Empty();
        }

        for (const FQueuedTask& Task : TasksToRun)
        {
            Task.ExecuteIfBound();
        }
    }

private:
    FMutex QueueMutex;
    TArray<FQueuedTask> PendingTasks;
};