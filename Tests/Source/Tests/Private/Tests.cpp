#include "Tests.h"
#include "Core.h"
#include "Object/Object.h"
#include "Templates/WeakObjectPtr.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include "InterfaceTest.h"

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

    class FTestLeafEntity : public Object
    {
        RCLASS(FTestLeafEntity)
    public:
        FTestLeafEntity() = default;

        // RObject::operator new/delete are protected so only the registry can construct objects.
        // A hand-written registration still needs a factory, and a member of the derived class
        // is allowed to call them.
        static Object* MakeInstance() { return new FTestLeafEntity(); }
        static void DeleteInstance(Object* Ptr) { delete static_cast<FTestLeafEntity*>(Ptr); }
    };

    // Exercises nested NewObject<T>() inside an object's constructor. The object being built is
    // the construction site, so the child is owned by it without any special syntax.
    class FNestedConstructorEntity : public Object
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

    class FThrowingCdoTest : public Object
    {
    public:
        using Super = Object;
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

    class FTestDevice : public Object, public ITestSensor, public ITestNamed
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

    class FTestVehicle : public Object, public ITestSensor
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

    // Two interfaces with the same simple name in different namespaces. The registry key is the
    // compiler's qualified name, so one class can implement both and each cast reaches its own
    // subobject. With simple names the second interface was silently dropped as a "duplicate"
    // and Cast<Editor::IInteractable> handed back the Gameplay subobject.
    namespace Gameplay
    {
        class IInteractable
        {
        public:
            RINTERFACE(IInteractable)
            virtual ~IInteractable() = default;
            virtual const char* Touch() const = 0;
        };
    }

    namespace Editor
    {
        class IInteractable
        {
        public:
            RINTERFACE(IInteractable)
            virtual ~IInteractable() = default;
            virtual const char* Gizmo() const = 0;
        };
    }

    class FTestDualNamed :
        public Object,
        public Gameplay::IInteractable,
        public Editor::IInteractable
    {
        RCLASS(FTestDualNamed)
        RIMPLEMENTS(Gameplay::IInteractable, Editor::IInteractable)
    public:
        const char* Touch() const override { return "gameplay"; }
        const char* Gizmo() const override { return "editor"; }
    };

    // The smallest hierarchy that can prove a base class refuses to unregister while a subclass
    // is still registered. Both belong to the test module, so the module's own unregistration
    // pass is what retries the base after the subclass is gone.
    class FTestUnloadBase : public Object
    {
        RCLASS(FTestUnloadBase)
    public:
        FTestUnloadBase() = default;
    };

    class FTestUnloadDerived : public FTestUnloadBase
    {
        RCLASS(FTestUnloadDerived)
    public:
        FTestUnloadDerived() = default;
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
            const EClassRegistrationResult DuplicateResult =
                Registry.RegisterClass<Object>("Tests_Tests");

            VerifyCheck(
                DuplicateResult == EClassRegistrationResult::AlreadyRegistered,
                "DuplicateIgnored",
                "Re-registering an identical class is accepted."
            );

            const FClassInfo* RobjectClass = Registry.FindClass("RObject");
            VerifyCheck(
                RobjectClass != nullptr && RobjectClass->OwningModule != "Tests_Tests",
                "DuplicateDidNotTakeOwnership",
                "The original owner of 'RObject' kept ownership."
            );

            // Same name, different base class: two different classes fighting over one name.
            const EClassRegistrationResult CollisionResult = Registry.RegisterClass(
                "FTestLeafEntity",
                "FObjectScope",
                "Tests_Tests",
                &FTestLeafEntity::MakeInstance,
                &FTestLeafEntity::DeleteInstance,
                static_cast<uint32_t>(sizeof(FTestLeafEntity)),
                static_cast<uint32_t>(alignof(FTestLeafEntity))
            );

            VerifyCheck(
                CollisionResult == EClassRegistrationResult::Failed,
                "NameCollisionRejected",
                "A same-name/different-base collision is rejected."
            );
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
            Object* ForeignRoot = FModuleManager::Get().GetOrCreateModuleScope("Tests_EscapeHatch");
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

            Object* AsObject = Device;
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

            Object* AsObject = Sports;
            VerifyCheck(Sports->IsA<FTestVehicle>() && Sports->IsA<FTestCar>() && Sports->IsA<Object>(), "IsAWalkedUpChain", "IsA<> is true for every ancestor.");
            VerifyCheck(Cast<FTestCar>(AsObject) == static_cast<FTestCar*>(Sports), "ClassCastFromBase", "Class cast from RObject* reaches the leaf's ancestor subobject.");
            VerifyCheck(Cast<FTestSportsCar>(AsObject) == Sports, "ClassCastExactLeaf", "Class cast to the exact leaf class succeeds.");

            FTestCar* Car = NewObject<FTestCar>();
            VerifyCheck(Cast<FTestSportsCar>(static_cast<Object*>(Car)) == nullptr, "DowncastRejected", "Casting a real FTestCar to FTestSportsCar returns nullptr.");
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
        //10. Interface test
        {
    constexpr const char* ModuleName = "Tests_InterfaceLifetimeTest";
    constexpr const char* ClassName = "FInterfaceLifetimeProbe";

    FModuleManager& Modules = FModuleManager::Get();

    auto VerifyLoadedInterface = [&]()
    {
        const FClassInfo* Info = Registry.FindClass(ClassName);

        VerifyCheck(
            Info != nullptr,
            "InterfaceLifetimeClassRegistered",
            "The fixture class is registered."
        );

        const FImplementedInterface* Interface =
            Info ? Info->FindInterface(
                IInterfaceLifetimeProbe::StaticInterfaceName()
            ) : nullptr;

        VerifyCheck(
            Interface != nullptr && Interface->GetAddress != nullptr,
            "InterfaceLifetimeThunkRegistered",
            "The class metadata contains a valid interface thunk."
        );

        const Object* CDO = Registry.GetCDO(ClassName);

        VerifyCheck(
            CDO != nullptr,
            "InterfaceLifetimeCDO",
            "The fixture class default object exists."
        );

        const IInterfaceLifetimeProbe* Probe =
            Cast<IInterfaceLifetimeProbe>(CDO);

        VerifyCheck(
            Probe != nullptr,
            "InterfaceLifetimeCast",
            "The CDO successfully casts to the interface."
        );

        VerifyCheck(
            Probe && Probe->GetProbeValue() == 0x1234,
            "InterfaceLifetimeDispatch",
            "The interface thunk reaches the correct virtual implementation."
        );
    };

    // First load.
    VerifyCheck(
        Modules.LoadModule(ModuleName) != nullptr,
        "InterfaceLifetimeFirstLoad",
        "The fixture module loads successfully."
    );

    VerifyLoadedInterface();

    // Unload must unregister the class and its thunk before FreeDllHandle().
    VerifyCheck(
        Modules.UnloadModule(ModuleName),
        "InterfaceLifetimeFirstUnload",
        "The fixture module unloads successfully."
    );

    VerifyCheck(
        !Modules.IsModuleLoaded(ModuleName),
        "InterfaceLifetimeUnloaded",
        "The module is no longer loaded."
    );

    VerifyCheck(
        Registry.FindClass(ClassName) == nullptr,
        "InterfaceLifetimeMetadataRemoved",
        "The fixture class metadata was removed during unload."
    );

    // Load the same DLL again. This creates a new registration and a new thunk.
    VerifyCheck(
        Modules.LoadModule(ModuleName) != nullptr,
        "InterfaceLifetimeSecondLoad",
        "The fixture module loads again successfully."
    );

    VerifyLoadedInterface();

    VerifyCheck(
        Modules.UnloadModule(ModuleName),
        "InterfaceLifetimeSecondUnload",
        "The fixture module unloads successfully a second time."
    );

    VerifyCheck(
        Registry.FindClass(ClassName) == nullptr,
        "InterfaceLifetimeSecondCleanup",
        "The second unload also removes the class metadata."
    );
}

        // 11. Namespaced interfaces: same simple name, different types
        {
            VerifyCheck(
                std::strcmp(Gameplay::IInteractable::StaticInterfaceName(), "Gameplay::IInteractable") == 0 &&
                std::strcmp(Editor::IInteractable::StaticInterfaceName(), "Editor::IInteractable") == 0,
                "InterfaceNamesAreQualified",
                "Interfaces are named by the compiler's qualified name, not by the authored simple name."
            );

            FTestDualNamed* Dual = NewObject<FTestDualNamed>();
            VerifyCheck(Dual != nullptr, "DualNamedCreated", "Object implementing two same-named interfaces created.");

            Gameplay::IInteractable* GameplaySide = Cast<Gameplay::IInteractable>(Dual);
            Editor::IInteractable* EditorSide = Cast<Editor::IInteractable>(Dual);

            VerifyCheck(
                GameplaySide != nullptr && EditorSide != nullptr,
                "SameNamedInterfacesBothResolve",
                "Both interfaces resolve even though both are called IInteractable."
            );
            VerifyCheck(
                static_cast<void*>(GameplaySide) != static_cast<void*>(EditorSide),
                "SameNamedInterfacesDifferentSubobjects",
                "The two cast results are different subobjects, not one pointer reinterpreted."
            );
            VerifyCheck(
                std::strcmp(GameplaySide->Touch(), "gameplay") == 0 &&
                std::strcmp(EditorSide->Gizmo(), "editor") == 0,
                "SameNamedInterfacesDispatch",
                "Each cast dispatches to the implementation of the interface it was cast to."
            );

            const FClassInfo* Info = Registry.FindClass(FTestDualNamed::StaticClassName());
            const FImplementedInterface* GameplayEntry =
                Info ? Info->FindInterface(Gameplay::IInteractable::StaticInterfaceName()) : nullptr;
            const FImplementedInterface* EditorEntry =
                Info ? Info->FindInterface(Editor::IInteractable::StaticInterfaceName()) : nullptr;

            VerifyCheck(
                Info != nullptr &&
                Info->Interfaces.Num() == 2 &&
                GameplayEntry != nullptr &&
                EditorEntry != nullptr &&
                GameplayEntry != EditorEntry &&
                GameplayEntry->Name != EditorEntry->Name,
                "SameNamedInterfacesBothRegistered",
                "The class metadata holds two entries whose names differ only by namespace."
            );

            Destroy(Dual);
        }

        // 12. Unregistration order: a base class cannot leave while a subclass is registered
        {
            const FClassInfo* BaseBefore = Registry.FindClass(FTestUnloadBase::StaticClassName());
            const FClassInfo* DerivedBefore = Registry.FindClass(FTestUnloadDerived::StaticClassName());
            VerifyCheck(
                BaseBefore != nullptr && DerivedBefore != nullptr,
                "UnloadOrderClassesRegistered",
                "Both classes of the unregistration-order pair are registered."
            );

            VerifyCheck(
                !Registry.UnregisterClass<FTestUnloadBase>("Tests_Tests"),
                "BaseBlockedBySubclass",
                "The base class refuses to unregister while its subclass is still registered."
            );
            VerifyCheck(
                Registry.FindClass(FTestUnloadBase::StaticClassName()) == BaseBefore,
                "RefusedUnregisterChangesNothing",
                "A refused unregistration leaves the base class metadata in place."
            );

            VerifyCheck(
                Registry.UnregisterClass<FTestUnloadDerived>("Tests_Tests"),
                "SubclassUnregistered",
                "The subclass unregisters when nothing derives from it."
            );
            VerifyCheck(
                Registry.UnregisterClass<FTestUnloadBase>("Tests_Tests"),
                "BaseUnregisteredAfterSubclass",
                "The base class unregisters once the subclass is gone."
            );
            VerifyCheck(
                Registry.FindClass(FTestUnloadBase::StaticClassName()) == nullptr &&
                Registry.FindClass(FTestUnloadDerived::StaticClassName()) == nullptr,
                "UnloadOrderClassesGone",
                "Both classes are gone and the registry no longer knows them."
            );
        }

        // 13. Layout fingerprint: two modules that compiled one class differently are refused
        {
            const FClassInfo* Vehicle = Registry.FindClass(FTestVehicle::StaticClassName());
            VerifyCheck(
                Vehicle != nullptr && Vehicle->ClassSize != 0 && Vehicle->ClassAlignment != 0,
                "LayoutFingerprintRecorded",
                "The registering translation unit's sizeof/alignof were stored with the class."
            );

            const EClassRegistrationResult Mismatch = Registry.RegisterClass(
                FTestVehicle::StaticClassName(),
                "RObject",
                "Tests_LayoutMismatch",
                []() -> Object* { return nullptr; },
                [](Object*) {},
                Vehicle->ClassSize + 8,
                Vehicle->ClassAlignment
            );

            VerifyCheck(
                Mismatch == EClassRegistrationResult::Failed,
                "LayoutMismatchRejected",
                "A registration with the same name and base but a different layout is rejected."
            );
            VerifyCheck(
                Registry.FindClass(FTestVehicle::StaticClassName()) == Vehicle,
                "LayoutMismatchKeepsOriginal",
                "The rejected duplicate did not replace the original metadata."
            );

            const EClassRegistrationResult Repeat = Registry.RegisterClass(
                FTestVehicle::StaticClassName(),
                "RObject",
                "Tests_LayoutMismatch",
                []() -> Object* { return nullptr; },
                [](Object*) {},
                Vehicle->ClassSize,
                Vehicle->ClassAlignment
            );

            VerifyCheck(
                Repeat == EClassRegistrationResult::AlreadyRegistered,
                "LayoutMatchAcceptedAsDuplicate",
                "The same class with the same layout is still accepted as an idempotent duplicate."
            );
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