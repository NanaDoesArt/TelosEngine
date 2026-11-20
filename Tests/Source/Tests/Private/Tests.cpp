#include "Tests.h"
#include "Core.h"
#include "Object/Object.h"
#include "Templates/WeakObjectPtr.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include <stdexcept>
#include <cstdlib>
#include <cstring>

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
    
    // Interfaces are plain abstract classes. RINTERFACE gives one a name the registry can compare;
    // RIMPLEMENTS on the implementing class makes Cast<TInterface>() possible.
    class ITestSensor
    {
    public:
        RINTERFACE(ITestSensor)
        virtual ~ITestSensor() = default;
        virtual int32_t Reading() const = 0;
    };

    class ITestNamed
    {
    public:
        RINTERFACE(ITestNamed)
        virtual ~ITestNamed() = default;
        virtual const char* Label() const = 0;
    };

    class FTestDevice : public RObject, public ITestSensor, public ITestNamed
    {
        RCLASS(FTestDevice)
        RIMPLEMENTS(ITestSensor, ITestNamed)
    public:
        int32_t Reading() const override { return 77; }
        const char* Label() const override { return "device"; }
    };

    // No RIMPLEMENTS: the parent's list is inherited, and the cast still resolves.
    class FTestDeviceChild : public FTestDevice
    {
        RCLASS(FTestDeviceChild)
    public:
        int32_t Reading() const override { return 88; }
    };
    
    // Three-level class chain. The middle class adds nothing; the leaf adds an interface of its
    // own while its RIMPLEMENTS *shadows* the parent's list, to prove the parent's interfaces stay
    // reachable through the class chain.
    class ITestSolid
    {
    public:
        RINTERFACE(ITestSolid)
        virtual ~ITestSolid() = default;
        virtual const char* Solid() const = 0;
    };

    class FTestVehicle : public RObject, public ITestSensor
    {
        RCLASS(FTestVehicle)
        RIMPLEMENTS(ITestSensor)
    public:
        int32_t Reading() const override { return 1; }
        virtual const char* Kind() const { return "vehicle"; }
    };

    class FTestCar : public FTestVehicle
    {
        RCLASS(FTestCar)
    public:
        const char* Kind() const override { return "car"; }
    };

    class FTestSportsCar : public FTestCar, public ITestSolid
    {
        RCLASS(FTestSportsCar)
        RIMPLEMENTS(ITestSolid)
    public:
        int32_t Reading() const override { return 3; }
        const char* Solid() const override { return "sports"; }
        const char* Kind() const override { return "sports car"; }
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
        
        // 8. Interfaces: RObject* -> interface* needs a real pointer adjustment
        {
            FTestDevice* Device = NewObject<FTestDevice>();
            VerifyCheck(Device != nullptr, "InterfaceDeviceCreated", "Object implementing two interfaces created.");

            ITestSensor* Sensor = Cast<ITestSensor>(Device);
            ITestNamed* Named = Cast<ITestNamed>(Device);
            VerifyCheck(Sensor != nullptr && Sensor->Reading() == 77, "InterfaceCastFirst", "Cast to the first interface resolved and dispatched.");
            VerifyCheck(Named != nullptr && std::strcmp(Named->Label(), "device") == 0, "InterfaceCastSecond", "Cast to the second interface resolved and dispatched.");
            VerifyCheck(
                static_cast<void*>(Sensor) != static_cast<void*>(Named),
                "InterfaceAddressesDiffer",
                "The two interfaces sit at different addresses, so the thunk performs a real adjustment."
            );

            RObject* AsObject = Device;
            VerifyCheck(Cast<ITestSensor>(AsObject) == Sensor, "InterfaceCastFromBase", "Casting from RObject* reaches the same interface subobject.");
            VerifyCheck(Cast<FTestDevice>(AsObject) == Device, "ClassCastStillWorks", "Class casts still work after the interface change.");

            FTestLeafEntity* Plain = NewObject<FTestLeafEntity>();
            VerifyCheck(Cast<ITestSensor>(Plain) == nullptr, "InterfaceNotImplemented", "An object without the interface casts to nullptr.");

            FTestDeviceChild* Child = NewObject<FTestDeviceChild>();
            ITestSensor* ChildSensor = Cast<ITestSensor>(Child);
            VerifyCheck(
                ChildSensor != nullptr && ChildSensor->Reading() == 88,
                "InterfaceInherited",
                "A subclass inherits its parent's interfaces and dispatches to its own override."
            );

            Destroy(Child);
            Destroy(Plain);
            Destroy(Device);
        }
        
         // 9. Subclassing RCLASS classes: class chain, virtual dispatch, interface inheritance
        {
            const FClassInfo* VehicleClass = Registry.FindClass("FTestVehicle");
            const FClassInfo* CarClass = Registry.FindClass("FTestCar");
            const FClassInfo* SportsClass = Registry.FindClass("FTestSportsCar");
            VerifyCheck(
                SportsClass != nullptr && SportsClass->SuperClass == CarClass && CarClass->SuperClass == VehicleClass,
                "ClassChainLinked",
                "Three-level class chain resolved through FClassInfo::SuperClass."
            );

            FTestSportsCar* Sports = NewObject<FTestSportsCar>();
            VerifyCheck(Sports != nullptr && std::strcmp(Sports->GetClassName(), "FTestSportsCar") == 0, "SubclassCreated", "Leaf class constructed.");

            FTestVehicle* AsVehicle = Cast<FTestVehicle>(Sports);
            FTestCar* AsCar = Cast<FTestCar>(Sports);
            VerifyCheck(
                AsVehicle != nullptr && AsCar != nullptr && AsCar == static_cast<FTestCar*>(AsVehicle),
                "UpcastThroughChain",
                "Casts to both ancestors resolve to the same subobject."
            );
            VerifyCheck(std::strcmp(AsVehicle->Kind(), "sports car") == 0, "VirtualDispatchThroughBase", "Calling through the base pointer dispatches to the leaf override.");

            RObject* AsObject = Sports;
            VerifyCheck(Sports->IsA<FTestVehicle>() && Sports->IsA<FTestCar>() && Sports->IsA<RObject>(), "IsAWalkedUpChain", "IsA<> is true for every ancestor.");
            VerifyCheck(Cast<FTestCar>(AsObject) == static_cast<FTestCar*>(Sports), "ClassCastFromBase", "Class cast from RObject* reaches the leaf's ancestor subobject.");
            VerifyCheck(Cast<FTestSportsCar>(AsObject) == Sports, "ClassCastExactLeaf", "Class cast to the exact leaf class succeeds.");

            FTestCar* Car = NewObject<FTestCar>();
            VerifyCheck(Cast<FTestSportsCar>(static_cast<RObject*>(Car)) == nullptr, "DowncastRejected", "Casting a real FTestCar to FTestSportsCar returns nullptr.");
            VerifyCheck(Cast<FTestVehicle>(Car) == static_cast<FTestVehicle*>(Car), "SiblingUpcast", "An instance of the middle class casts up to its base.");

            // The leaf's RIMPLEMENTS shadows the parent's list. The parent's interface must still be
            // found on the leaf by walking the class chain, and dispatch to the leaf override.
            ITestSensor* LeafSensor = Cast<ITestSensor>(Sports);
            ITestSolid* LeafSolid = Cast<ITestSolid>(Sports);
            VerifyCheck(LeafSensor != nullptr && LeafSensor->Reading() == 3, "InheritedInterfaceFromChain", "Interface declared by the base class resolves on the leaf and dispatches to the leaf override.");
            VerifyCheck(LeafSolid != nullptr && std::strcmp(LeafSolid->Solid(), "sports") == 0, "OwnInterfaceOnLeaf", "Interface declared by the leaf resolves on the leaf.");
            VerifyCheck(Cast<ITestSolid>(Car) == nullptr, "InterfaceNotOnSibling", "An interface added by the leaf is not visible on its parent.");
            VerifyCheck(
                LeafSensor == static_cast<ITestSensor*>(Sports),
                "InheritedInterfaceSameAddress",
                "The inherited interface resolves to the same subobject the compiler would compute."
            );

            Destroy(Car);
            Destroy(Sports);
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