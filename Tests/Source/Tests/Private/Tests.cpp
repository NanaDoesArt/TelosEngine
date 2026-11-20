#include "Tests.h"
#include "Core.h"
#include "Object/Object.h"
#include "Templates/WeakObjectPtr.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include <stdexcept>
#include <cstdlib>

namespace
{
    void VerifyCheck(bool bCondition, const char* TestName, const char* Detail)
    {
        if (!bCondition)
        {
            VENGINE_LOG(LogCore, Fatal, "[CoreTests] FAILED ({}): {}", TestName, Detail);
            std::abort();
        }
        VENGINE_LOG(LogCore, Info, "[CoreTests] PASSED ({}): {}", TestName, Detail);
    }

    class FTestLeafEntity : public RObject
    {
        RCLASS(FTestLeafEntity)
    public:
        FTestLeafEntity() = default;

        // RObject::operator new/delete are protected so only the registry can construct objects.
        // A hand-written registration still needs a factory, and a member of the derived class
        // is allowed to call them.
        static RObject* MakeInstance() { return new FTestLeafEntity(); }
        static void DeleteInstance(RObject* Ptr) { delete static_cast<FTestLeafEntity*>(Ptr); }
    };

    // Exercises nested NewObject<T>() inside an object's constructor. The object being built is
    // the construction site, so the child is owned by it without any special syntax.
    class FNestedConstructorEntity : public RObject
    {
        RCLASS(FNestedConstructorEntity)
    public:
        FNestedConstructorEntity()
        {
            // The construction site is this object, so NewObject<T>() parents the child to it.
            NestedChild = NewObject<FTestLeafEntity>();
        }

        FTestLeafEntity* NestedChild = nullptr;
    };

    class FThrowingCdoTest : public RObject
    {
    public:
        using Super = RObject;
        using ThisClass = FThrowingCdoTest;
        static const char* StaticClassName() { return "FThrowingCdoTest"; }
        const char* GetClassName() const override { return "FThrowingCdoTest"; }

        FThrowingCdoTest()
        {
            throw std::runtime_error("Simulated CDO constructor failure");
        }
    };
}

class FTestsModule : public IModuleInterface
{
public:
    bool StartupModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Tests] Starting Core & Object Lifecycle Test Suite...");
        FObjectRegistry& Registry = FObjectRegistry::Get();

        // 1. Startup Creation & Weak-Pointer Invalidation
        {
            FTestLeafEntity* Temp = NewObject<FTestLeafEntity>();
            VerifyCheck(Temp != nullptr, "StartupCreation", "NewObject<FTestLeafEntity>() succeeded in StartupModule().");

            TWeakObjectPtr<FTestLeafEntity> WeakTemp = Temp;
            VerifyCheck(WeakTemp.IsValid() && WeakTemp.Get() == Temp, "WeakPtrValid", "TWeakObjectPtr resolves live object.");

            Destroy(Temp);
            VerifyCheck(!WeakTemp.IsValid() && WeakTemp.Get() == nullptr, "WeakPtrInvalidated", "TWeakObjectPtr invalidated immediately on Destroy().");
        }

        // 2. Parent-Child Ownership & Cascade Cleanup
        {
            FTestLeafEntity* Parent = NewObject<FTestLeafEntity>();
            VerifyCheck(Parent != nullptr, "ParentCreation", "Parent creation succeeded.");

            FTestLeafEntity* Child = NewObject<FTestLeafEntity>(Parent);
            VerifyCheck(Child != nullptr && Child->GetOuter() == Parent, "ParentChildOwnership", "NewObject<T>(Parent) parented child to Parent.");
            VerifyCheck(Parent->GetChildren().Num() == 1, "ParentChildrenList", "Parent recorded 1 owned child.");

            TWeakObjectPtr<FTestLeafEntity> WeakParent = Parent;
            TWeakObjectPtr<FTestLeafEntity> WeakChild = Child;

            Destroy(Parent);
            VerifyCheck(!WeakParent.IsValid() && !WeakChild.IsValid(), "CascadeDestroy", "Destroying Parent destroyed Child and invalidated both weak pointers.");
        }

        // 3. Nested Construction Inside A Constructor (Owned By The Construction Site)
        {
            const FClassInfo* LeafClass = Registry.FindClass(FTestLeafEntity::StaticClassName());
            const FClassInfo* OuterClass = Registry.FindClass(FNestedConstructorEntity::StaticClassName());
            VerifyCheck(LeafClass != nullptr && OuterClass != nullptr, "NestedClassesRegistered", "Both nested-construction classes are registered.");

            const int32_t LeafCountBefore = LeafClass->LiveInstanceCount;
            const int32_t OuterCountBefore = OuterClass->LiveInstanceCount;

            FNestedConstructorEntity* OuterObj = NewObject<FNestedConstructorEntity>();
            VerifyCheck(OuterObj != nullptr, "NestedCtorOuter", "Outer object constructed.");
            VerifyCheck(OuterObj->NestedChild != nullptr && OuterObj->NestedChild->GetOuter() == OuterObj, "NestedCtorChild", "Child constructed inside Outer constructor via NewObject().");
            VerifyCheck(OuterObj->NestedChild->IsA<FTestLeafEntity>(), "NestedCtorExactType", "Nested child recorded exact FTestLeafEntity class rather than Outer class.");
            VerifyCheck(
                LeafClass->LiveInstanceCount == LeafCountBefore + 1 &&
                OuterClass->LiveInstanceCount == OuterCountBefore + 1,
                "NestedCtorAccounting",
                "Parent and child incremented their own class counts."
            );

            Destroy(OuterObj);
            VerifyCheck(
                LeafClass->LiveInstanceCount == LeafCountBefore &&
                OuterClass->LiveInstanceCount == OuterCountBefore,
                "NestedCtorAccountingRestored",
                "Both class counts returned to their original values."
            );
        }

        // 4. Throwing CDO Constructor Rollback & Live Count Accounting
        {
            bool bCaughtException = false;
            try
            {
                Registry.RegisterClass<FThrowingCdoTest>("Tests_Tests");
            }
            catch (const std::runtime_error&)
            {
                bCaughtException = true;
            }

            VerifyCheck(bCaughtException, "ThrowingCdoCaught", "Throwing CDO constructor exception propagated.");
            VerifyCheck(Registry.FindClass("FThrowingCdoTest") == nullptr, "ThrowingCdoRolledBack", "Failed CDO registration rolled back class metadata.");

            FTestLeafEntity* PostThrow = NewObject<FTestLeafEntity>();
            VerifyCheck(PostThrow != nullptr, "ScopedStateRestored", "Normal NewObject() works after throwing CDO rollback.");
            Destroy(PostThrow);

            int32_t LiveCount = 0;
            const bool bHasLiveInstances = Registry.HasLiveInstancesForModule("Tests_Tests", &LiveCount);
            VerifyCheck(!bHasLiveInstances && LiveCount == 0, "LiveCountRestored", "Temporary objects left no live-instance count behind.");
        }

        // 5. Duplicate Registrations: header copies are ignored, real collisions rejected
        {
            // Every module that includes a header gets its own copy of the registrar. The second
            // copy must be accepted and ignored, not treated as an error.
            const bool bDuplicateIgnored = Registry.RegisterClass<RObject>("Tests_Tests");
            VerifyCheck(bDuplicateIgnored, "DuplicateIgnored", "Re-registering an identical class is accepted.");

            const FClassInfo* RobjectClass = Registry.FindClass("RObject");
            VerifyCheck(
                RobjectClass != nullptr && RobjectClass->OwningModule != "Tests_Tests",
                "DuplicateDidNotTakeOwnership",
                "The original owner of 'RObject' kept ownership."
            );

            // Same name, different base class: two different classes fighting over one name.
            const bool bCollisionRejected = Registry.RegisterClass(
                "FTestLeafEntity",
                "FObjectScope",
                "Tests_Tests",
                &FTestLeafEntity::MakeInstance,
                &FTestLeafEntity::DeleteInstance
            );
            VerifyCheck(!bCollisionRejected, "NameCollisionRejected", "A same-name/different-base collision is rejected.");
        }

        // 6. Releasing a module's objects destroys its whole scope subtree
        {
            FTestLeafEntity* Scoped = NewObject<FTestLeafEntity>();
            VerifyCheck(Scoped != nullptr, "ScopeCreate", "Module-scoped object created.");

            TWeakObjectPtr<FTestLeafEntity> WeakScoped = Scoped;

            const bool bReleased = FModuleManager::Get().ReleaseModuleObjects("Tests_Tests");
            VerifyCheck(bReleased, "ScopeRelease", "Releasing the module's objects succeeded with no survivors.");
            VerifyCheck(!WeakScoped.IsValid(), "ScopeReleaseDestroyed", "Releasing the module scope destroyed the object it owned.");
        }

        // 7. An object that escaped its module scope keeps the module's classes registered
        {
            RObject* ForeignRoot = FModuleManager::Get().GetOrCreateModuleScope("Tests_EscapeHatch");
            VerifyCheck(ForeignRoot != nullptr, "EscapeRoot", "Foreign scope created.");

            FTestLeafEntity* Escaped = NewObject<FTestLeafEntity>(ForeignRoot);
            VerifyCheck(Escaped != nullptr && Escaped->GetOuter() == ForeignRoot, "EscapeParented", "Object parented outside its own module scope.");

            const bool bReleased = FModuleManager::Get().ReleaseModuleObjects("Tests_Tests");
            VerifyCheck(!bReleased, "EscapeBlocksRelease", "ReleaseModuleObjects() reports survivors when an object escaped the scope.");

            TWeakObjectPtr<FTestLeafEntity> WeakEscaped = Escaped;
            const bool bForeignReleased = FModuleManager::Get().ReleaseModuleObjects("Tests_EscapeHatch");
            VerifyCheck(bForeignReleased && !WeakEscaped.IsValid(), "EscapeCleanup", "Destroying the foreign scope destroyed the escaped object.");
        }

        VENGINE_LOG(LogCore, Info, "[Tests] All in-module assertions PASSED!");
        return true;
    }

    void ShutdownModule() override
    {
        // Confirm FailingModule's prefix class was cleaned up during deferred shutdown
        VerifyCheck(
            FObjectRegistry::Get().FindClass("FFailingModValidPrefix") == nullptr,
            "FailingModuleCleanupVerified",
            "FailingModule prefix class was unregistered cleanly during deferred shutdown."
        );
        VENGINE_LOG(LogCore, Info, "[Tests] Test suite finished cleanly!");
    }
};

IMPLEMENT_MODULE(FTestsModule)